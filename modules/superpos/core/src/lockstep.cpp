#include "superpos/lockstep.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <bit>
#include <cstring>

namespace superpos {
namespace {
constexpr std::byte delta_kind{1}, table_kind{2};
constexpr std::size_t capacity = lockstep_history_ticks;
std::size_t varuint_size(std::uint64_t value) noexcept { std::size_t n=1; while(value>=128){value>>=7;++n;} return n; }
bool valid_width(std::size_t bytes) noexcept { return bytes && bytes<=lockstep_input_bytes; }
template<class Table> bool equal(const Table& a,const Table& b,std::size_t bytes) noexcept { return std::memcmp(a.data(),b.data(),bytes)==0; }
}

Result<InputHistory> InputHistory::create(std::size_t input_bytes) noexcept {
    if(!valid_width(input_bytes))return fail(Error::InvalidArgument);
    return InputHistory(input_bytes);
}
Status InputHistory::record(Tick tick,std::span<const std::byte> input) noexcept {
    if(input.size()!=input_bytes_ || !tick)return fail(Error::InvalidArgument);
    if(any_ && tick!=newest_+1)return fail(Error::InvalidArgument);
    if(count_==capacity)return fail(Error::CapacityExceeded);
    std::memcpy(inputs_[tick%capacity].data(),input.data(),input.size());
    if(!count_)oldest_=tick;
    newest_=tick;any_=true;++count_;return {};
}
Status InputHistory::acknowledge(Tick received) noexcept {
    if(!any_ || !count_)return {};
    if(received>newest_)return fail(Error::ProtocolViolation);
    while(count_ && oldest_<=received){++oldest_;--count_;}
    return {};
}
std::size_t InputHistory::discard_oldest(std::size_t count) noexcept {
    count=std::min(count,count_);oldest_+=count;count_-=count;return count;
}
Result<std::size_t> InputHistory::encode(Tick command_acknowledged,std::size_t max_ticks,std::span<std::byte> output,Tick command_held) const noexcept {
    // The newest unacknowledged inputs that fit: on a saturated uplink, resending an
    // ever-growing backlog would only delay the acknowledgements further.
    std::size_t ticks=std::min(count_,max_ticks);
    // The held tick travels as its distance past the acknowledgement (0: no gap).
    const Tick gap=command_held>command_acknowledged?command_held-command_acknowledged:0;
    const auto fixed=1+varuint_size(command_acknowledged)+varuint_size(gap)+varuint_size(newest_+1)+varuint_size(input_bytes_);
    while(ticks && fixed+varuint_size(ticks)+ticks*input_bytes_>output.size())--ticks;
    if(fixed+varuint_size(ticks)+ticks*input_bytes_>output.size())return fail(Error::Truncated);
    const auto first=ticks?newest_-ticks+1:newest_+1;
    Writer writer(output);std::array<std::byte,1> version{std::byte{lockstep_wire_version}};
    if(!writer.raw(version)||!writer.varuint(command_acknowledged)||!writer.varuint(gap)||!writer.varuint(first)||!writer.varuint(ticks)||!writer.varuint(input_bytes_))return fail(Error::Truncated);
    for(std::size_t i=0;i<ticks;++i)if(!writer.raw(std::span(inputs_[(first+i)%capacity]).first(input_bytes_)))return fail(Error::Truncated);
    return writer.size();
}

Result<InputPlayout> InputPlayout::create(PlayoutConfig config) noexcept {
    if(!valid_width(config.input_bytes) || !config.minimum_target || config.minimum_target>config.initial_target ||
        config.initial_target>config.maximum_target || config.maximum_target>=capacity || !config.relax_ticks)return fail(Error::InvalidArgument);
    return InputPlayout(config);
}
Status InputPlayout::accept(std::span<const std::byte> batch) noexcept {
    Reader reader(batch);
    auto version=reader.raw(1);
    if(!version || ((*version)[0]!=std::byte{lockstep_wire_version} && (*version)[0]!=std::byte{1}))return fail(Error::Unsupported);
    const bool with_gap=(*version)[0]==std::byte{lockstep_wire_version};
    auto acknowledged=reader.varuint();
    Result<std::uint64_t> gap=std::uint64_t{0};
    if(with_gap)gap=reader.varuint();
    auto first=reader.varuint(),count=reader.varuint(),width=reader.varuint();
    if(!acknowledged||!gap||!first||!count||!width)return fail(Error::NonCanonical);
    if(*gap>lockstep_max_command_history)return fail(Error::ProtocolViolation);
    if(*width!=config_.input_bytes || *count>capacity || !*first)return fail(Error::ProtocolViolation);
    auto payload=reader.raw(*count*config_.input_bytes);if(!payload||!reader.empty())return fail(Error::NonCanonical);
    command_acknowledged_=std::max(command_acknowledged_,*acknowledged);
    // The latest report wins (a rejoined client's held tick may go back down).
    command_held_=*acknowledged+*gap;
    if(!started_ && *count){next_=*first;received_=*first-1;started_=true;calm_=0;}
    // A client back from an outage (its history discarded what we never received)
    // can land beyond the window: resynchronize forward, skipping the lost ticks,
    // instead of rejecting its stream as late forever.
    if(started_ && *count && *first+*count-1>=next_+capacity) {
        const Tick target=*first+*count-capacity;
        for(Tick tick=next_;tick<target && tick<next_+capacity;++tick)occupied_[tick%capacity]=false;
        skipped_+=target-next_;next_=target;received_=std::max(received_,next_-1);
    }
    for(std::size_t i=0;i<*count;++i) {
        const Tick tick=*first+i;
        if(tick<next_){++late_;continue;}
        if(tick-next_>=capacity){++late_;continue;}
        auto& slot=occupied_[tick%capacity];
        if(slot){++duplicates_;continue;}
        std::memcpy(inputs_[tick%capacity].data(),payload->data()+i*config_.input_bytes,config_.input_bytes);
        slot=true;received_=std::max(received_,tick);
    }
    return {};
}
Result<std::size_t> InputPlayout::consume(std::span<PlayoutInput,2> output) noexcept {
    if(!started_)return std::size_t{0};
    const std::size_t depth=received_>=next_?static_cast<std::size_t>(received_-next_+1):0;
    if(!playing_) {
        if(depth<target_)return std::size_t{0};
        playing_=true;calm_=0;
    }
    if(!depth) {
        playing_=false;++starvations_;target_=std::min(target_+3,config_.maximum_target);calm_=0;
        return std::size_t{0};
    }
    if(++calm_>=config_.relax_ticks && target_>config_.minimum_target){--target_;calm_=0;}
    const std::size_t steps=depth>target_+config_.catch_up_margin?2:1;
    std::size_t produced=0;
    for(std::size_t step=0;step<steps;++step) {
        // A tick lost beyond the sender's redundancy window is skipped, never invented.
        while(next_<=received_ && !occupied_[next_%capacity]){++next_;++skipped_;}
        if(next_>received_)break;
        auto& stored=consumed_[produced];
        std::memcpy(stored.data(),inputs_[next_%capacity].data(),config_.input_bytes);
        occupied_[next_%capacity]=false;
        output[produced]={next_,std::span<const std::byte>(stored).first(config_.input_bytes)};
        ++next_;++produced;
    }
    return produced;
}
PlayoutStatus InputPlayout::status() const noexcept {
    const std::size_t depth=started_&&received_>=next_?static_cast<std::size_t>(received_-next_+1):0;
    return {received_,started_&&next_?next_-1:0,command_acknowledged_,depth,target_,starvations_,duplicates_,late_,skipped_,playing_,command_held_};
}
double InputPlayout::pace_advice() const noexcept {
    const auto state=status();
    if(!started_)return 0.0;
    const double difference=static_cast<double>(state.target)-static_cast<double>(state.buffered);
    return std::clamp(difference/static_cast<double>(std::max<std::size_t>(state.target,1)),-1.0,1.0);
}

namespace {
std::uint64_t zigzag(Tick value,Tick previous) noexcept {
    const auto delta=static_cast<std::int64_t>(value-previous);
    return (static_cast<std::uint64_t>(delta)<<1)^static_cast<std::uint64_t>(delta>>63);
}
Tick unzigzag(std::uint64_t value,Tick previous) noexcept {
    const auto delta=static_cast<std::int64_t>(value>>1)^-static_cast<std::int64_t>(value&1);
    return previous+static_cast<Tick>(delta);
}
Status valid_stream(const CommandStreamConfig& config) noexcept {
    if(!valid_width(config.input_bytes) || !config.slots || config.slots>lockstep_slots ||
        !config.history_ticks || config.history_ticks>lockstep_max_command_history ||
        config.pending_batches>lockstep_max_pending_batches || (config.pending_batches && (config.pending_batch_bytes<8 || config.pending_batch_bytes>4096)))return fail(Error::InvalidArgument);
    return {};
}
// Unless capped, every retained tick may change every slot.
std::size_t stream_changes(const CommandStreamConfig& config) noexcept {
    return config.change_capacity?config.change_capacity:config.history_ticks*config.slots;
}
// Wire v3 bit IO: least significant bit first within each byte.
class BitWriter {
public:
    explicit BitWriter(std::span<std::byte> out) noexcept : out_(out) {}
    bool bits(std::uint32_t value,unsigned count) noexcept {
        for(unsigned i=0;i<count;++i,++position_) {
            if(position_/8>=out_.size())return false;
            if((value>>i)&1u)out_[position_/8]|=std::byte(1u<<(position_%8));
        }
        return true;
    }
    // Elias gamma of n >= 1: N zero bits, a one bit, then the low N bits of n.
    bool gamma(std::uint32_t n) noexcept {
        const auto width=static_cast<unsigned>(std::bit_width(n))-1;
        return bits(0,width) && bits(1,1) && bits(n&((1u<<width)-1u),width);
    }
    std::size_t bytes() const noexcept { return (position_+7)/8; }
private:
    std::span<std::byte> out_;
    std::size_t position_{};
};
class BitReader {
public:
    explicit BitReader(std::span<const std::byte> in) noexcept : in_(in) {}
    bool bits(std::uint32_t& value,unsigned count) noexcept {
        value=0;
        for(unsigned i=0;i<count;++i,++position_) {
            if(position_/8>=in_.size())return false;
            value|=((std::to_integer<std::uint32_t>(in_[position_/8])>>(position_%8))&1u)<<i;
        }
        return true;
    }
    bool gamma(std::uint32_t& n) noexcept {
        unsigned width=0;std::uint32_t bit=0;
        for(;;) { if(!bits(bit,1))return false; if(bit)break; if(++width>16)return false; }
        std::uint32_t low=0;if(!bits(low,width))return false;
        n=(1u<<width)|low;return true;
    }
    std::size_t bytes() const noexcept { return (position_+7)/8; }
    // Canonical encodings pad the final byte with zero bits.
    bool padding_clear() const noexcept {
        return position_%8==0 || (std::to_integer<unsigned>(in_[position_/8])>>(position_%8))==0;
    }
private:
    std::span<const std::byte> in_;
    std::size_t position_{};
};
std::uint32_t zigzag8(std::uint8_t now,std::uint8_t before) noexcept {
    const auto delta=static_cast<std::int8_t>(static_cast<std::uint8_t>(now-before));
    return static_cast<std::uint8_t>((static_cast<std::uint8_t>(delta)<<1)^static_cast<std::uint8_t>(delta>>7));
}
std::uint8_t unzigzag8(std::uint32_t code,std::uint8_t before) noexcept {
    const auto delta=static_cast<std::uint8_t>((code>>1)^(0u-(code&1u)));
    return static_cast<std::uint8_t>(before+delta);
}
// Largest encoded tick: count varuint plus, per slot, a 17-bit gap, a 16-bit mask
// and sixteen 9-bit values.
constexpr std::size_t max_tick_bytes=3+(lockstep_slots*(17+16+16*9)+7)/8;
}

Result<CommandEncoder> CommandEncoder::create(Allocator& allocator,CommandStreamConfig config) noexcept {
    if(auto valid=valid_stream(config);!valid)return fail(valid.error());
    // Encoded history budget: four bytes per change entry (a bit-packed change
    // averages about two), never less than one worst-case tick.
    config.change_capacity=std::max(stream_changes(config)*4,max_tick_bytes);
    CommandEncoder encoder(allocator,config);
    if(!encoder.ring_.resize(config.change_capacity) || !encoder.meta_.resize(config.history_ticks*sizeof(TickMeta)) ||
        !encoder.processed_.resize(config.history_ticks*config.slots*sizeof(Tick)))return fail(Error::OutOfMemory);
    return encoder;
}
Status CommandEncoder::begin(Tick tick) noexcept {
    if(open_any_ || !tick || (sealed_any_ && tick!=newest_+1))return fail(Error::InvalidArgument);
    staged_=table_;
    if(sealed_any_)std::memcpy(staged_processed_.data(),processed()+(newest_%config_.history_ticks)*config_.slots,config_.slots*sizeof(Tick));
    open_=tick;open_any_=true;return {};
}
Status CommandEncoder::set(std::uint16_t slot,std::span<const std::byte> input,Tick processed_tick) noexcept {
    if(!open_any_ || slot>=config_.slots || input.size()!=config_.input_bytes)return fail(Error::InvalidArgument);
    std::memcpy(staged_[slot].data(),input.data(),input.size());staged_processed_[slot]=processed_tick;return {};
}
Status CommandEncoder::end() noexcept {
    if(!open_any_)return fail(Error::InvalidArgument);
    const auto history=config_.history_ticks,capacity=config_.change_capacity,width=config_.input_bytes;
    std::size_t changed=0;
    for(std::size_t slot=0;slot<config_.slots;++slot)changed+=!equal(staged_[slot],table_[slot],width);
    // Encode the tick once: count, then bit-packed slot gaps, masks and deltas.
    std::array<std::byte,max_tick_bytes> scratch{};
    Writer count_writer(scratch);if(!count_writer.varuint(changed))return fail(Error::Truncated);
    BitWriter bits{std::span(scratch).subspan(count_writer.size())};
    std::size_t previous=0,changed_bytes=0;bool any=false;
    for(std::size_t slot=0;slot<config_.slots;++slot) {
        std::uint32_t mask=0;
        for(std::size_t i=0;i<width;++i)if(staged_[slot][i]!=table_[slot][i])mask|=1u<<i;
        if(!mask)continue;
        if(!bits.gamma(static_cast<std::uint32_t>(any?slot-previous:slot+1)) || !bits.bits(mask,static_cast<unsigned>(width)))return fail(Error::Truncated);
        for(std::size_t i=0;i<width;++i) {
            if(!(mask&(1u<<i)))continue;
            const auto code=zigzag8(std::to_integer<std::uint8_t>(staged_[slot][i]),std::to_integer<std::uint8_t>(table_[slot][i]));
            if(!(code<16?bits.bits(0,1)&&bits.bits(code,4):bits.bits(1,1)&&bits.bits(code,8)))return fail(Error::Truncated);
            ++changed_bytes;
        }
        previous=slot;any=true;
    }
    const std::size_t length=count_writer.size()+bits.bytes();
    if(length>capacity)return fail(Error::CapacityExceeded);
    // Evict the oldest encoded ticks until the ring holds this one.
    while(ticks_ && (ticks_==history || used_+length>capacity)) {
        const auto& oldest=meta()[(newest_-ticks_+1)%history];
        head_=(head_+oldest.bytes)%capacity;used_-=oldest.bytes;--ticks_;
    }
    if(!ticks_){head_=0;used_=0;}
    auto& tick=meta()[open_%history];
    tick.first=(head_+used_)%capacity;tick.count=changed;tick.bytes=length;
    auto* ring=ring_.bytes().data();
    const std::size_t split=std::min(length,capacity-tick.first);
    std::memcpy(ring+tick.first,scratch.data(),split);
    std::memcpy(ring,scratch.data()+split,length-split);
    used_+=length;
    totals_.changed_bytes+=changed_bytes;++totals_.ticks;totals_.changes+=changed;totals_.encoded_bytes+=length;
    tick.cumulative=totals_.encoded_bytes;
    std::memcpy(processed()+(open_%history)*config_.slots,staged_processed_.data(),config_.slots*sizeof(Tick));
    table_=staged_;newest_=open_;sealed_any_=true;open_any_=false;++ticks_;return {};
}
std::uint64_t CommandEncoder::backlog_bytes(Tick after) const noexcept {
    if(!ticks_ || after>=newest_)return 0;
    if(after+1<oldest())return std::numeric_limits<std::uint64_t>::max();
    const auto& next=meta()[(after+1)%config_.history_ticks];
    return totals_.encoded_bytes-(next.cumulative-next.bytes);
}
std::size_t CommandEncoder::fit_forward(Tick first,Tick last,std::size_t body,std::size_t capacity,std::uint32_t recipient) const noexcept {
    const auto history=config_.history_ticks;
    const bool with_processed=recipient!=lockstep_no_recipient;
    const std::size_t fixed=2+varuint_size(first)+varuint_size(with_processed?std::uint64_t{recipient}+1:0);
    Tick previous=with_processed&&last>=first?processed()[(last%history)*config_.slots+recipient]:0;
    std::size_t ticks=last>=first?static_cast<std::size_t>(last-first+1):0;
    for(Tick tick=first+ticks;tick<=newest_;++tick) {
        std::size_t next=meta()[tick%history].bytes;
        Tick value=0;
        if(with_processed){value=processed()[(tick%history)*config_.slots+recipient];next+=varuint_size(zigzag(value,previous));}
        if(fixed+varuint_size(ticks+1)+body+next>capacity)break;
        body+=next;++ticks;previous=value;
    }
    return ticks;
}
Result<std::size_t> CommandEncoder::write(Tick first,std::size_t ticks,std::span<std::byte> output,std::uint32_t recipient) const noexcept {
    const auto history=config_.history_ticks,capacity=config_.change_capacity;
    const bool with_processed=recipient!=lockstep_no_recipient;
    Writer writer(output);std::array<std::byte,2> head{std::byte{lockstep_command_wire_version},delta_kind};
    if(!writer.raw(head)||!writer.varuint(first)||!writer.varuint(ticks)||!writer.varuint(with_processed?std::uint64_t{recipient}+1:0))return fail(Error::Truncated);
    Tick previous=0;
    const auto ring=std::span<const std::byte>(ring_.bytes());
    for(Tick tick=first;tick<first+ticks;++tick) {
        const auto& sealed=meta()[tick%history];
        if(with_processed) {
            const Tick value=processed()[(tick%history)*config_.slots+recipient];
            if(!writer.varuint(zigzag(value,previous)))return fail(Error::Truncated);
            previous=value;
        }
        // Whole pre-encoded tick, possibly wrapping around the ring.
        const std::size_t split=std::min(sealed.bytes,capacity-sealed.first);
        if(!writer.raw(ring.subspan(sealed.first,split))||!writer.raw(ring.subspan(0,sealed.bytes-split)))return fail(Error::Truncated);
    }
    return writer.size();
}
Result<std::size_t> CommandEncoder::encode(Tick after,std::span<std::byte> output,std::uint32_t recipient) const noexcept {
    if(!sealed_any_)return fail(Error::NotReady);
    if(recipient!=lockstep_no_recipient && recipient>=config_.slots)return fail(Error::InvalidArgument);
    if(after+1<oldest())return fail(Error::StaleEpoch);
    // Size whole ticks into the output: a batch never splits a tick.
    const Tick first=std::min(after+1,newest_+1);
    return write(first,fit_forward(first,first-1,0,output.size(),recipient),output,recipient);
}
Result<std::size_t> CommandEncoder::encode_window(Tick acknowledged,Tick sent,std::span<std::byte> output,std::uint32_t recipient,std::size_t redundant) const noexcept {
    if(!sealed_any_)return fail(Error::NotReady);
    if(recipient!=lockstep_no_recipient && recipient>=config_.slots)return fail(Error::InvalidArgument);
    if(acknowledged+1<oldest())return fail(Error::StaleEpoch);
    const bool with_processed=recipient!=lockstep_no_recipient;
    const auto history=config_.history_ticks;
    auto processed_at=[&](Tick tick) noexcept { return with_processed?processed()[(tick%history)*config_.slots+recipient]:Tick{0}; };
    auto delta=[&](Tick tick,Tick previous) noexcept { return with_processed?varuint_size(zigzag(processed_at(tick),previous)):std::size_t{0}; };
    const std::size_t recipient_bytes=varuint_size(with_processed?std::uint64_t{recipient}+1:0);
    // Fresh ticks first: everything after max(sent, acknowledged) that fits.
    const Tick fresh=std::min(std::max(sent,acknowledged)+1,newest_+1);
    std::size_t ticks=fit_forward(fresh,fresh-1,0,output.size(),recipient);
    if(!ticks && fresh<=newest_)return write(fresh,0,output,recipient);
    Tick first=ticks?fresh:newest_+1;
    const Tick last=first+ticks-1;
    // Body bytes of [first, last], the first tick's processed tick absolute.
    std::size_t body=0;
    for(Tick tick=first;tick<=last && ticks;++tick)body+=meta()[tick%history].bytes+delta(tick,tick==first?0:processed_at(tick-1));
    // Then already-sent, unacknowledged ticks back toward the acknowledgement.
    for(std::size_t extra=0;extra<redundant && first>acknowledged+1 && first>1;++extra) {
        const Tick earlier=first-1;
        std::size_t grown=body+meta()[earlier%history].bytes+delta(earlier,0);
        if(ticks)grown=grown-delta(first,0)+delta(first,processed_at(earlier));
        if(2+varuint_size(earlier)+varuint_size(ticks+1)+recipient_bytes+grown>output.size())break;
        body=grown;first=earlier;++ticks;
    }
    return write(first,ticks,output,recipient);
}
Result<std::size_t> CommandEncoder::encode_table(std::span<std::byte> output) const noexcept {
    if(!sealed_any_)return fail(Error::NotReady);
    Writer writer(output);std::array<std::byte,2> head{std::byte{lockstep_command_wire_version},table_kind};
    if(!writer.raw(head)||!writer.varuint(newest_)||!writer.varuint(config_.slots)||!writer.varuint(config_.input_bytes))return fail(Error::Truncated);
    for(std::size_t slot=0;slot<config_.slots;++slot)if(!writer.raw(std::span(table_[slot]).first(config_.input_bytes)))return fail(Error::Truncated);
    return writer.size();
}

void CommandStream::reset(Tick keyframe,std::uint64_t now) noexcept {
    acknowledged_=sent_=keyframe;acknowledged_at_=reset_at_=now;started_=true;
}
Result<std::size_t> CommandStream::next(const CommandEncoder& encoder,Tick acknowledged,std::uint64_t now,std::uint64_t srtt,
    std::span<std::byte> output,std::uint32_t recipient,Tick held) noexcept {
    if(!started_)reset(acknowledged,now);
    if(acknowledged>acknowledged_){acknowledged_=acknowledged;acknowledged_at_=now;}
    // A parked-beyond-the-gap client jumps its acknowledgement past what we
    // resent, so the stream skips ahead instead of repeating it.
    sent_=std::max(sent_,acknowledged_);
    // Nothing outstanding: the stall clock idles (a slow keyframe is not a stall).
    if(sent_==acknowledged_)acknowledged_at_=now;
    const std::uint64_t stall=std::max(policy_.minimum_stall_us,2*srtt+policy_.ack_cadence_us);
    if(sent_>acknowledged_ && now>=acknowledged_at_ && now-acknowledged_at_>stall) {
        sent_=acknowledged_;acknowledged_at_=now;++rewinds_;clean_=0;
    }
    // The recipient holds ticks past its acknowledgement: the batch carrying the next
    // one is missing. It is lost (not merely reordered) once enough later ticks are
    // held or the gap outlived the reorder window; then resend from the acknowledgement,
    // once per RTT for the same gap.
    if(held>acknowledged_) {
        if(gap_!=acknowledged_){gap_=acknowledged_;gap_since_=now;}
    } else {
        gap_=0;
    }
    const std::uint64_t reorder=std::max(policy_.minimum_reorder_us,srtt/4);
    const bool lost=held>acknowledged_ && (held-acknowledged_>policy_.reorder_ticks || (now>=gap_since_ && now-gap_since_>=reorder));
    const std::uint64_t guard=std::max(policy_.minimum_repair_us,srtt+srtt/4);
    if(lost && sent_>acknowledged_ && (repaired_!=acknowledged_ || now<repaired_at_ || now-repaired_at_>=guard)) {
        sent_=acknowledged_;repaired_=acknowledged_;repaired_at_=now;acknowledged_at_=now;++repairs_;clean_=0;
    }
    // A backlog costlier than a keyframe: advise the owner to resync this recipient.
    const bool settled=now>=reset_at_ && now-reset_at_>=policy_.resync_holdoff_us;
    if(policy_.resync_backlog_bytes && settled && encoder.backlog_bytes(acknowledged_)>policy_.resync_backlog_bytes) {
        ++resyncs_;return fail(Error::StaleEpoch);
    }
    const bool behind=encoder.newest()>acknowledged_+policy_.catch_up_ticks;
    auto size=encoder.encode_window(acknowledged_,sent_,output,recipient,behind?0:redundancy());
    if(!size)return size;
    Reader reader(output.first(*size));(void)reader.raw(2);
    auto first=reader.varuint(),ticks=reader.varuint();
    if(first && ticks && *ticks)sent_=std::max(sent_,*first+*ticks-1);
    ++batches_;++clean_;
    return size;
}

Result<CommandDecoder> CommandDecoder::create(Allocator& allocator,CommandStreamConfig config) noexcept {
    if(auto valid=valid_stream(config);!valid)return fail(valid.error());
    config.change_capacity=stream_changes(config);
    CommandDecoder decoder(allocator,config);
    if(!decoder.changes_.resize(config.change_capacity*sizeof(Change)) || !decoder.meta_.resize(config.history_ticks*sizeof(TickMeta)) ||
        !decoder.parked_bytes_.resize(config.pending_batches*config.pending_batch_bytes))return fail(Error::OutOfMemory);
    return decoder;
}
Status CommandDecoder::load_table(std::span<const std::byte> bytes) noexcept {
    Reader reader(bytes);auto head=reader.raw(2);
    if(!head || (*head)[0]!=std::byte{lockstep_command_wire_version} || (*head)[1]!=table_kind)return fail(Error::Unsupported);
    auto tick=reader.varuint(),slots=reader.varuint(),width=reader.varuint();
    if(!tick||!slots||!width)return fail(Error::NonCanonical);
    if(*slots!=config_.slots || *width!=config_.input_bytes)return fail(Error::ProtocolViolation);
    auto payload=reader.raw(config_.slots*config_.input_bytes);if(!payload||!reader.empty())return fail(Error::NonCanonical);
    for(std::size_t slot=0;slot<config_.slots;++slot)std::memcpy(table_[slot].data(),payload->data()+slot*config_.input_bytes,config_.input_bytes);
    latest_=table_;applied_=newest_=*tick;processed_tick_=0;
    ticks_=tick_head_=change_head_=change_count_=0;synchronized_=true;
    drain();return {};
}
Status CommandDecoder::accept(std::span<const std::byte> bytes) noexcept {
    auto accepted=accept_contiguous(bytes);
    if(accepted){drain();return accepted;}
    // A validated batch beyond a gap waits for its repair instead of being lost.
    if(accepted.error()==Error::Unsupported && park(bytes))return {};
    return accepted;
}
Tick CommandDecoder::held() const noexcept {
    Tick newest=newest_;
    for(const auto& batch:parked_)if(batch.occupied)newest=std::max(newest,batch.last);
    return newest;
}
std::size_t CommandDecoder::deferred() const noexcept {
    std::size_t count=0;for(const auto& batch:parked_)count+=batch.occupied;return count;
}
bool CommandDecoder::park(std::span<const std::byte> bytes) noexcept {
    if(!synchronized_ || !config_.pending_batches || bytes.size()>config_.pending_batch_bytes)return false;
    Reader reader(bytes);auto head=reader.raw(2);auto first=reader.varuint(),ticks=reader.varuint();
    if(!head || (*head)[0]!=std::byte{lockstep_command_wire_version} || (*head)[1]!=delta_kind || !first || !ticks || !*ticks || *first<=newest_+1)return false;
    const Tick last=*first+*ticks-1;
    Parked* chosen=nullptr;
    for(std::size_t i=0;i<config_.pending_batches;++i) {
        auto& batch=parked_[i];
        if(batch.occupied && batch.first<=*first && batch.last>=last)return true; // already covered
        if(!batch.occupied){if(!chosen || chosen->occupied)chosen=&batch;}
        else if(!chosen || (chosen->occupied && batch.first>chosen->first))chosen=&batch;
    }
    // Full: the farthest-future batch yields to a nearer one; otherwise drop this one.
    if(chosen->occupied && chosen->first<=*first)return true;
    const auto index=static_cast<std::size_t>(chosen-parked_.data());
    std::memcpy(parked_bytes_.bytes().data()+index*config_.pending_batch_bytes,bytes.data(),bytes.size());
    *chosen={true,*first,last,bytes.size()};
    return true;
}
void CommandDecoder::drain() noexcept {
    for(bool progressed=true;progressed;) {
        progressed=false;
        for(std::size_t i=0;i<config_.pending_batches;++i) {
            auto& batch=parked_[i];
            if(!batch.occupied)continue;
            if(batch.last<=newest_){batch={};continue;}
            if(batch.first>newest_+1)continue;
            auto applied=accept_contiguous(std::span<const std::byte>(parked_bytes_.bytes()).subspan(i*config_.pending_batch_bytes,batch.size));
            if(!applied && applied.error()==Error::CapacityExceeded)return; // retry after the simulation drains
            batch={};progressed=progressed||bool(applied);
        }
    }
}
Status CommandDecoder::accept_contiguous(std::span<const std::byte> bytes) noexcept {
    Reader reader(bytes);auto head=reader.raw(2);
    if(!head || (*head)[0]!=std::byte{lockstep_command_wire_version} || (*head)[1]!=delta_kind)return fail(Error::Unsupported);
    auto first=reader.varuint(),ticks=reader.varuint(),recipient=reader.varuint();
    if(!first||!ticks||!recipient)return fail(Error::NonCanonical);
    if(!*first || *recipient>config_.slots)return fail(Error::ProtocolViolation);
    const bool with_processed=*recipient!=0;
    const auto width=static_cast<unsigned>(config_.input_bytes);
    // One tick's bit-packed changes; `apply` patches each change onto latest_ and
    // queues it, otherwise the tick is only validated (or skipped as already held).
    auto tick_changes=[&](Reader& tick_reader,std::uint64_t count,bool apply) noexcept -> Status {
        BitReader bits(tick_reader.remaining());
        std::uint64_t slot=0;
        for(std::uint64_t c=0;c<count;++c) {
            std::uint32_t gap=0,mask=0;
            if(!bits.gamma(gap))return fail(Error::NonCanonical);
            slot=c?slot+gap:gap-1;
            if(slot>=config_.slots || !bits.bits(mask,width) || !mask)return fail(Error::NonCanonical);
            std::array<std::byte,lockstep_input_bytes> row{};
            if(apply)row=latest_[slot];
            for(unsigned b=0;b<width;++b) {
                if(!(mask&(1u<<b)))continue;
                std::uint32_t wide=0,code=0;
                if(!bits.bits(wide,1) || !bits.bits(code,wide?8u:4u) || (wide?code<16:code==0))return fail(Error::NonCanonical);
                if(apply)row[b]=std::byte(unzigzag8(code,std::to_integer<std::uint8_t>(row[b])));
            }
            if(apply) {
                latest_[slot]=row;
                auto& change=changes()[(change_head_+change_count_)%config_.change_capacity];
                change.slot=static_cast<std::uint16_t>(slot);change.bytes=row;++change_count_;
            }
        }
        if(!bits.padding_clear() || !tick_reader.raw(bits.bytes()))return fail(Error::NonCanonical);
        return {};
    };
    // Validate the complete batch before buffering anything.
    Reader check=reader;std::size_t fresh=0,fresh_changes=0;
    for(std::uint64_t i=0;i<*ticks;++i) {
        if(with_processed && !check.varuint())return fail(Error::NonCanonical);
        auto count=check.varuint();if(!count || *count>config_.slots)return fail(Error::NonCanonical);
        if(auto valid=tick_changes(check,*count,false);!valid)return valid;
        if(*first+i>newest_ || !synchronized_){++fresh;fresh_changes+=*count;}
    }
    if(!check.empty())return fail(Error::NonCanonical);
    if(!synchronized_) {
        if(*first!=1)return fail(Error::Unsupported); // needs a keyframe table first
        latest_={};table_={};applied_=newest_=0;synchronized_=true;
    }
    if(*ticks && *first+*ticks-1<=newest_)return {};
    if(*ticks && *first>newest_+1)return fail(Error::Unsupported);
    if(ticks_+fresh>config_.history_ticks || change_count_+fresh_changes>config_.change_capacity)return fail(Error::CapacityExceeded);
    Tick processed_value=0;
    for(std::uint64_t i=0;i<*ticks;++i) {
        const Tick tick=*first+i;
        if(with_processed)processed_value=unzigzag(*reader.varuint(),processed_value);
        auto count=reader.varuint();
        const bool keep=tick>newest_;
        if(keep)meta()[(tick_head_+ticks_)%config_.history_ticks]={(change_head_+change_count_)%config_.change_capacity,static_cast<std::size_t>(*count),processed_value};
        // Deltas need the previous tick's values, so held ticks are only skipped.
        (void)tick_changes(reader,*count,keep);
        if(keep){newest_=tick;++ticks_;}
    }
    return {};
}
Result<Tick> CommandDecoder::advance() noexcept {
    if(!ticks_)return fail(Error::Busy);
    const auto& tick=meta()[tick_head_];
    for(std::size_t i=0;i<tick.count;++i) {
        const auto& change=changes()[(tick.first+i)%config_.change_capacity];
        std::memcpy(table_[change.slot].data(),change.bytes.data(),config_.input_bytes);
    }
    processed_tick_=tick.processed;
    change_head_=(change_head_+tick.count)%config_.change_capacity;change_count_-=tick.count;
    tick_head_=(tick_head_+1)%config_.history_ticks;--ticks_;return ++applied_;
}
std::span<const std::byte> CommandDecoder::input(std::uint16_t slot) const noexcept {
    if(slot>=config_.slots)return {};
    return std::span<const std::byte>(table_[slot]).first(config_.input_bytes);
}
}
