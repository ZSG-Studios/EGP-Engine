#include "superpos/lockstep.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
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
Result<std::size_t> InputHistory::encode(Tick command_acknowledged,std::size_t max_ticks,std::span<std::byte> output) const noexcept {
    const auto first=count_?oldest_:newest_+1;
    std::size_t ticks=std::min(count_,max_ticks);
    const auto fixed=1+varuint_size(command_acknowledged)+varuint_size(first)+varuint_size(input_bytes_);
    while(ticks && fixed+varuint_size(ticks)+ticks*input_bytes_>output.size())--ticks;
    if(fixed+varuint_size(ticks)+ticks*input_bytes_>output.size())return fail(Error::Truncated);
    Writer writer(output);std::array<std::byte,1> version{std::byte{lockstep_wire_version}};
    if(!writer.raw(version)||!writer.varuint(command_acknowledged)||!writer.varuint(first)||!writer.varuint(ticks)||!writer.varuint(input_bytes_))return fail(Error::Truncated);
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
    auto version=reader.raw(1);if(!version || (*version)[0]!=std::byte{lockstep_wire_version})return fail(Error::Unsupported);
    auto acknowledged=reader.varuint(),first=reader.varuint(),count=reader.varuint(),width=reader.varuint();
    if(!acknowledged||!first||!count||!width)return fail(Error::NonCanonical);
    if(*width!=config_.input_bytes || *count>capacity || !*first)return fail(Error::ProtocolViolation);
    auto payload=reader.raw(*count*config_.input_bytes);if(!payload||!reader.empty())return fail(Error::NonCanonical);
    command_acknowledged_=std::max(command_acknowledged_,*acknowledged);
    if(!started_ && *count){next_=*first;received_=*first-1;started_=true;calm_=0;}
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
    return {received_,started_&&next_?next_-1:0,command_acknowledged_,depth,target_,starvations_,duplicates_,late_,skipped_,playing_};
}
double InputPlayout::pace_advice() const noexcept {
    const auto state=status();
    if(!started_)return 0.0;
    const double difference=static_cast<double>(state.target)-static_cast<double>(state.buffered);
    return std::clamp(difference/static_cast<double>(std::max<std::size_t>(state.target,1)),-1.0,1.0);
}

Result<CommandEncoder> CommandEncoder::create(std::size_t input_bytes,std::size_t slots) noexcept {
    if(!valid_width(input_bytes) || !slots || slots>lockstep_slots)return fail(Error::InvalidArgument);
    return CommandEncoder(input_bytes,slots);
}
Status CommandEncoder::begin(Tick tick) noexcept {
    if(open_any_ || !tick || (sealed_any_ && tick!=newest_+1))return fail(Error::InvalidArgument);
    staged_=table_;open_=tick;open_any_=true;return {};
}
Status CommandEncoder::set(std::uint16_t slot,std::span<const std::byte> input) noexcept {
    if(!open_any_ || slot>=slots_ || input.size()!=input_bytes_)return fail(Error::InvalidArgument);
    std::memcpy(staged_[slot].data(),input.data(),input.size());return {};
}
Status CommandEncoder::end() noexcept {
    if(!open_any_)return fail(Error::InvalidArgument);
    std::size_t changed=0;
    for(std::size_t slot=0;slot<slots_;++slot)changed+=!equal(staged_[slot],table_[slot],input_bytes_);
    if(changed>change_capacity)return fail(Error::CapacityExceeded);
    // Evict the oldest sealed ticks until both history rings can hold this tick.
    while(ticks_ && (ticks_==capacity || change_count_+changed>change_capacity)) {
        const auto oldest=(newest_-ticks_+1)%capacity;
        change_head_=(change_head_+tick_count_[oldest])%change_capacity;change_count_-=tick_count_[oldest];--ticks_;
    }
    const auto index=open_%capacity;tick_first_[index]=(change_head_+change_count_)%change_capacity;tick_count_[index]=changed;
    for(std::size_t slot=0;slot<slots_;++slot) if(!equal(staged_[slot],table_[slot],input_bytes_)) {
        auto& change=changes_[(change_head_+change_count_)%change_capacity];
        change.slot=static_cast<std::uint16_t>(slot);change.bytes=staged_[slot];++change_count_;
    }
    table_=staged_;newest_=open_;sealed_any_=true;open_any_=false;++ticks_;return {};
}
Result<std::size_t> CommandEncoder::encode(Tick after,std::span<std::byte> output) const noexcept {
    if(!sealed_any_)return fail(Error::NotReady);
    const Tick oldest=newest_-ticks_+1;
    if(after+1<oldest)return fail(Error::StaleEpoch);
    const Tick first=std::min(after+1,newest_+1);
    // Size whole ticks into the output: a batch never splits a tick.
    std::size_t ticks=0,size=0;
    auto tick_size=[&](Tick tick) noexcept {
        const auto index=tick%capacity;std::size_t bytes=varuint_size(tick_count_[index]);
        for(std::size_t i=0;i<tick_count_[index];++i)bytes+=varuint_size(changes_[(tick_first_[index]+i)%change_capacity].slot)+input_bytes_;
        return bytes;
    };
    for(Tick tick=first;tick<=newest_;++tick) {
        const auto next=tick_size(tick);
        if(2+varuint_size(first)+varuint_size(ticks+1)+size+next>output.size())break;
        size+=next;++ticks;
    }
    Writer writer(output);std::array<std::byte,2> head{std::byte{lockstep_wire_version},delta_kind};
    if(!writer.raw(head)||!writer.varuint(first)||!writer.varuint(ticks))return fail(Error::Truncated);
    for(Tick tick=first;tick<first+ticks;++tick) {
        const auto index=tick%capacity;
        if(!writer.varuint(tick_count_[index]))return fail(Error::Truncated);
        for(std::size_t i=0;i<tick_count_[index];++i) {
            const auto& change=changes_[(tick_first_[index]+i)%change_capacity];
            if(!writer.varuint(change.slot)||!writer.raw(std::span(change.bytes).first(input_bytes_)))return fail(Error::Truncated);
        }
    }
    return writer.size();
}
Result<std::size_t> CommandEncoder::encode_table(std::span<std::byte> output) const noexcept {
    if(!sealed_any_)return fail(Error::NotReady);
    Writer writer(output);std::array<std::byte,2> head{std::byte{lockstep_wire_version},table_kind};
    if(!writer.raw(head)||!writer.varuint(newest_)||!writer.varuint(slots_)||!writer.varuint(input_bytes_))return fail(Error::Truncated);
    for(std::size_t slot=0;slot<slots_;++slot)if(!writer.raw(std::span(table_[slot]).first(input_bytes_)))return fail(Error::Truncated);
    return writer.size();
}

Result<CommandDecoder> CommandDecoder::create(std::size_t input_bytes,std::size_t slots) noexcept {
    if(!valid_width(input_bytes) || !slots || slots>lockstep_slots)return fail(Error::InvalidArgument);
    return CommandDecoder(input_bytes,slots);
}
Status CommandDecoder::load_table(std::span<const std::byte> bytes) noexcept {
    Reader reader(bytes);auto head=reader.raw(2);
    if(!head || (*head)[0]!=std::byte{lockstep_wire_version} || (*head)[1]!=table_kind)return fail(Error::Unsupported);
    auto tick=reader.varuint(),slots=reader.varuint(),width=reader.varuint();
    if(!tick||!slots||!width)return fail(Error::NonCanonical);
    if(*slots!=slots_ || *width!=input_bytes_)return fail(Error::ProtocolViolation);
    auto payload=reader.raw(slots_*input_bytes_);if(!payload||!reader.empty())return fail(Error::NonCanonical);
    for(std::size_t slot=0;slot<slots_;++slot)std::memcpy(table_[slot].data(),payload->data()+slot*input_bytes_,input_bytes_);
    applied_=newest_=*tick;ticks_=tick_head_=change_head_=change_count_=0;synchronized_=true;return {};
}
Status CommandDecoder::accept(std::span<const std::byte> bytes) noexcept {
    Reader reader(bytes);auto head=reader.raw(2);
    if(!head || (*head)[0]!=std::byte{lockstep_wire_version} || (*head)[1]!=delta_kind)return fail(Error::Unsupported);
    auto first=reader.varuint(),ticks=reader.varuint();
    if(!first||!ticks)return fail(Error::NonCanonical);
    if(!*first)return fail(Error::ProtocolViolation);
    // Validate the complete batch before buffering anything.
    Reader check=reader;std::size_t fresh=0,fresh_changes=0;
    for(std::uint64_t i=0;i<*ticks;++i) {
        auto changes=check.varuint();if(!changes || *changes>slots_)return fail(Error::NonCanonical);
        for(std::uint64_t c=0;c<*changes;++c) {
            auto slot=check.varuint();if(!slot || *slot>=slots_)return fail(Error::NonCanonical);
            if(!check.raw(input_bytes_))return fail(Error::NonCanonical);
        }
        if(*first+i>newest_ || !synchronized_){++fresh;fresh_changes+=*changes;}
    }
    if(!check.empty())return fail(Error::NonCanonical);
    if(!synchronized_) {
        if(*first!=1)return fail(Error::Unsupported); // needs a keyframe table first
        applied_=newest_=0;synchronized_=true;
    }
    if(*ticks && *first+*ticks-1<=newest_)return {};
    if(*ticks && *first>newest_+1)return fail(Error::Unsupported);
    if(ticks_+fresh>capacity || change_count_+fresh_changes>change_capacity)return fail(Error::CapacityExceeded);
    for(std::uint64_t i=0;i<*ticks;++i) {
        const Tick tick=*first+i;auto changes=reader.varuint();
        const bool keep=tick>newest_;
        const auto index=(tick_head_+ticks_)%capacity;
        if(keep){tick_first_[index]=(change_head_+change_count_)%change_capacity;tick_count_[index]=*changes;}
        for(std::uint64_t c=0;c<*changes;++c) {
            auto slot=reader.varuint();auto input=reader.raw(input_bytes_);
            if(keep) {
                auto& change=changes_[(change_head_+change_count_)%change_capacity];
                change.slot=static_cast<std::uint16_t>(*slot);std::memcpy(change.bytes.data(),input->data(),input_bytes_);++change_count_;
            }
        }
        if(keep){newest_=tick;++ticks_;}
    }
    return {};
}
Result<Tick> CommandDecoder::advance() noexcept {
    if(!ticks_)return fail(Error::Busy);
    const auto index=tick_head_;
    for(std::size_t i=0;i<tick_count_[index];++i) {
        const auto& change=changes_[(tick_first_[index]+i)%change_capacity];
        std::memcpy(table_[change.slot].data(),change.bytes.data(),input_bytes_);
    }
    change_head_=(change_head_+tick_count_[index])%change_capacity;change_count_-=tick_count_[index];
    tick_head_=(tick_head_+1)%capacity;--ticks_;return ++applied_;
}
std::span<const std::byte> CommandDecoder::input(std::uint16_t slot) const noexcept {
    if(slot>=slots_)return {};
    return std::span<const std::byte>(table_[slot]).first(input_bytes_);
}
}
