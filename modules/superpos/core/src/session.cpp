#include "superpos/session.hpp"
#include "superpos/codec.hpp"
#include <optional>
#include <thread>
#include <new>
#include <utility>
#include <cstring>
#include <atomic>
#include <algorithm>

namespace superpos {
namespace {
Result<std::uint64_t> new_session_instance() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto current=next.load(std::memory_order_relaxed);
    for(;;) {
        if(current==UINT64_MAX)return fail(Error::CounterExhausted);
        if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current;
    }
}
Status write_manifest(Writer& w,const CapabilityManifest& c) noexcept {
    if(!w.varuint(c.format_version) || !w.varuint(c.protocol_version) || !w.varuint(static_cast<unsigned>(c.digest)) ||
        !w.raw(c.schemas) || !w.raw(c.simulation) || !w.varuint(c.maximum_encoded_bytes) || !w.varuint(c.maximum_decoded_bytes) ||
        !w.varuint(c.history_ticks) || !w.varuint(static_cast<std::uint64_t>(c.capabilities)) || !w.varuint(static_cast<unsigned>(c.recovery)))return fail(Error::ProtocolViolation);
    return {};
}
Result<CapabilityManifest> read_manifest(Reader& r) noexcept {
    auto format=r.varuint(),protocol=r.varuint(),digest=r.varuint(); auto schemas=r.raw(32),simulation=r.raw(32);
    auto encoded=r.varuint(),decoded=r.varuint(),history=r.varuint(),flags=r.varuint(),recovery=r.varuint();
    if(!format || !protocol || !digest || !schemas || !simulation || !encoded || !decoded || !history || !flags || !recovery)return fail(Error::ProtocolViolation);
    if(*digest>UINT8_MAX || *encoded>UINT32_MAX || *decoded>UINT32_MAX || *history>UINT32_MAX || *recovery>UINT8_MAX)return fail(Error::ProtocolViolation);
    CapabilityManifest result; result.format_version=*format; result.protocol_version=*protocol; result.digest=static_cast<DigestAlgorithm>(*digest);
    std::memcpy(result.schemas.data(),schemas->data(),32); std::memcpy(result.simulation.data(),simulation->data(),32);
    result.maximum_encoded_bytes=static_cast<std::uint32_t>(*encoded); result.maximum_decoded_bytes=static_cast<std::uint32_t>(*decoded);
    result.history_ticks=static_cast<std::uint32_t>(*history); result.capabilities=static_cast<Capability>(*flags); result.recovery=static_cast<RecoveryGrade>(*recovery);
    return result;
}
bool same_manifest(const CapabilityManifest& a,const CapabilityManifest& b) noexcept {
    return a.format_version==b.format_version && a.protocol_version==b.protocol_version && a.digest==b.digest && a.schemas==b.schemas && a.simulation==b.simulation &&
        a.maximum_encoded_bytes==b.maximum_encoded_bytes && a.maximum_decoded_bytes==b.maximum_decoded_bytes && a.history_ticks==b.history_ticks && a.capabilities==b.capabilities && a.recovery==b.recovery;
}
}
struct Session::Impl {
    TransportProvider* transport; bool split_carriers{}; SessionConfig config; std::thread::id owner; std::uint64_t instance{};
    Buffer tx,rx,control_tx,control_rx; std::array<DeliverySlot,8> tx_slots{},rx_slots{},control_tx_slots{},control_rx_slots{};
    std::array<std::optional<DeliverySender>,32> senders;
    std::array<std::optional<DeliveryReceiver>,32> receivers;
    std::optional<CapabilityManifest> remote_capabilities;
    AdmittedCapabilities admitted{};
    bool hello_received{},hello_acknowledged{},ack_hello{},failed{},have_tick{},hello_sent{};
    Tick hello_at{},started_at{},last_tick{}; std::uint8_t next_channel{};
    struct Receipt {
        bool active{},have_stage{},have_fragments{}; std::uint64_t message{};
        DeliveryStage stage{}; std::uint8_t channel{}; FragmentReceipt fragments{};
    };
    // The eight Control rows are unavailable to application ACK backlogs.
    // Prefix placement also emits Control receipts before application rows.
    std::array<Receipt,72> receipts{};
    Impl(Allocator& a,TransportProvider& t,SessionConfig c) noexcept:
        transport(&t),config(c),owner(std::this_thread::get_id()),tx(a),rx(a),control_tx(a),control_rx(a) {}
    bool owned() const noexcept { return owner==std::this_thread::get_id(); }
    std::span<Receipt> receipt_pool(std::uint8_t channel) noexcept {
        return config.channel_purposes[channel]==ChannelPurpose::Control?std::span(receipts).first(8):std::span(receipts).subspan(8);
    }
    std::span<const Receipt> receipt_pool(std::uint8_t channel) const noexcept {
        return config.channel_purposes[channel]==ChannelPurpose::Control?std::span(receipts).first(8):std::span(receipts).subspan(8);
    }
    bool receipt_capacity(std::uint8_t channel,std::uint64_t id) const noexcept {
        for(const auto& r:receipt_pool(channel)) if(!r.active || (r.channel==channel && r.message==id))return true;
        return false;
    }
    Status receipt(std::uint8_t channel,std::uint64_t id,DeliveryStage stage) noexcept {
        for(auto& r:receipt_pool(channel)) if(r.active && r.channel==channel && r.message==id) {
            if(!r.have_stage || stage>r.stage)r.stage=stage; r.have_stage=true;
            if(stage==DeliveryStage::Applied)r.have_fragments=false; return {};
        }
        for(auto& r:receipt_pool(channel)) if(!r.active) { r={}; r.active=r.have_stage=true; r.message=id; r.stage=stage; r.channel=channel; return {}; }
        return fail(Error::CapacityExceeded);
    }
    Status progress(const FragmentReceipt& fragments) noexcept {
        Receipt* chosen=nullptr;
        for(auto& r:receipt_pool(static_cast<std::uint8_t>(fragments.channel)))if(r.active && r.channel==fragments.channel && r.message==fragments.message) { chosen=&r; break; }
        if(!chosen)for(auto& r:receipt_pool(static_cast<std::uint8_t>(fragments.channel)))if(!r.active) { chosen=&r; r={}; r.active=true; r.message=fragments.message; r.channel=static_cast<std::uint8_t>(fragments.channel); break; }
        if(!chosen)return fail(Error::CapacityExceeded);
        if(chosen->have_stage && chosen->stage==DeliveryStage::Applied)return {};
        chosen->fragments=fragments; chosen->have_fragments=true; return {};
    }
    Status acknowledge(std::uint8_t channel,std::uint64_t message) noexcept {
        auto& receiver=*receivers[channel];
        if(auto fragments=receiver.fragment_acknowledgement(message);fragments) {
            auto queued=progress(*fragments); if(!queued && queued.error()!=Error::CapacityExceeded)return queued;
        }
        if(auto stage=receiver.acknowledgement(message);stage) {
            auto queued=receipt(channel,message,*stage); if(!queued && queued.error()!=Error::CapacityExceeded)return queued;
        } return {};
    }
    Status hello(std::span<const std::byte> bytes,bool ack) noexcept {
        Reader r(bytes);auto version=r.varuint();if(!version||*version!=Session::hello_wire_version)return fail(Error::Unsupported);
        auto session=r.u64(),epoch=r.u64(),peer=r.u64(); auto fingerprint=r.raw(32); auto channels=r.varuint();
        if(!session || !epoch || !peer || !fingerprint || !channels || *channels<1 || *channels>32)return fail(Error::ProtocolViolation);
        auto modes=r.raw(static_cast<std::size_t>(*channels));
        auto purposes=r.raw(static_cast<std::size_t>(*channels));
        auto routes=r.raw(static_cast<std::size_t>(*channels));
        if(!modes||!purposes||!routes)return fail(Error::ProtocolViolation);
        for(std::size_t i=0;i<*channels;++i){auto cap=r.varuint();if(!cap)return fail(Error::ProtocolViolation);if((*purposes)[i]!=std::byte(static_cast<unsigned>(config.channel_purposes[i]))||(*routes)[i]!=std::byte(static_cast<unsigned>(config.channel_carriers[i]))||*cap!=config.channel_message_bytes[i])return fail(Error::Unsupported);}
        auto maximum=r.varuint(),fragment_bytes=r.varuint(),messages=r.varuint();
        auto remote=read_manifest(r);
        if(!maximum || !fragment_bytes || !messages || !remote || !r.empty())return fail(Error::ProtocolViolation);
        if(*session!=config.session_id || *epoch!=config.epoch || *peer!=config.remote_peer)return fail(Error::AuthenticationFailed);
        if(std::memcmp(fingerprint->data(),config.schema_fingerprint.data(),32)!=0)return fail(Error::IncompatibleSchema);
        if(*channels!=config.logical_channels)return fail(Error::Unsupported);
        if(*maximum!=config.limits.max_message_bytes || *fragment_bytes!=config.limits.fragment_payload_bytes || *messages!=config.limits.max_messages)return fail(Error::Unsupported);
        for(std::size_t i=0;i<*channels;++i) if((*modes)[i]!=std::byte(static_cast<unsigned>(config.channel_modes[i])))return fail(Error::Unsupported);
        if(remote->schemas!=config.schema_fingerprint)return fail(Error::IncompatibleSchema);
        auto compatible=admit_capabilities(config.capabilities,*remote,config.admission); if(!compatible)return fail(compatible.error());
        if(compatible->maximum_encoded_bytes<config.limits.max_message_bytes || compatible->maximum_decoded_bytes<config.limits.max_message_bytes)return fail(Error::Unsupported);
        // Both HELLO directions and every retry describe one immutable admission.
        // Authorization for hidden-state transfer is exclusively local policy;
        // a remote declaration cannot grant it over the wire.
        if(remote_capabilities && !same_manifest(*remote_capabilities,*remote))return fail(Error::ProtocolViolation);
        remote_capabilities=*remote; admitted=*compatible;
        if(ack)hello_acknowledged=true; else { hello_received=true; ack_hello=true; } return {};
    }
    Status send_hello(bool ack) noexcept {
        std::array<std::byte,512> bytes{}; bytes[0]=std::byte(ack?2:1); Writer w(std::span(bytes).subspan(1));
        if(!w.varuint(Session::hello_wire_version) || !w.u64(config.session_id) || !w.u64(config.epoch) || !w.u64(config.local_peer) || !w.raw(config.schema_fingerprint) || !w.varuint(config.logical_channels))return fail(Error::ProtocolViolation);
        for(unsigned i=0;i<config.logical_channels;++i) { std::byte mode=std::byte(static_cast<unsigned>(config.channel_modes[i])); if(!w.raw({&mode,1}))return fail(Error::ProtocolViolation); }
        for(unsigned i=0;i<config.logical_channels;++i){std::byte purpose=std::byte(static_cast<unsigned>(config.channel_purposes[i]));if(!w.raw({&purpose,1}))return fail(Error::ProtocolViolation);}
        for(unsigned i=0;i<config.logical_channels;++i){std::byte route=std::byte(static_cast<unsigned>(config.channel_carriers[i]));if(!w.raw({&route,1}))return fail(Error::ProtocolViolation);}
        for(unsigned i=0;i<config.logical_channels;++i)if(!w.varuint(config.channel_message_bytes[i]))return fail(Error::ProtocolViolation);
        if(!w.varuint(config.limits.max_message_bytes) || !w.varuint(config.limits.fragment_payload_bytes) || !w.varuint(config.limits.max_messages))return fail(Error::ProtocolViolation);
        if(auto encoded=write_manifest(w,config.capabilities);!encoded)return encoded;
        return transport->send_frame({bytes.data(),w.size()+1},CarrierLane::Control);
    }
};
Session::~Session() { if(impl_) { impl_->~Impl(); allocator_->deallocate(impl_); } }
Session::Session(Session&& other) noexcept:impl_(std::exchange(other.impl_,nullptr)),allocator_(other.allocator_) {}
Session& Session::operator=(Session&& other) noexcept {
    if(this!=&other) { if(impl_) { impl_->~Impl(); allocator_->deallocate(impl_); } impl_=std::exchange(other.impl_,nullptr); allocator_=other.allocator_; } return *this;
}
Result<Session> Session::create(Allocator& allocator,TransportProvider& transport,SessionConfig config) noexcept {
    auto caps=transport.capabilities(); if(!caps.authenticated || !caps.encrypted || !caps.datagram || caps.maximum_frame<49U+config.limits.fragment_payload_bytes)return fail(Error::Unsupported);
    if(!config.session_id || !config.epoch || !config.logical_channels || config.logical_channels>32 || !config.limits.max_messages || config.limits.max_messages>8 || config.limits.fragment_payload_bytes<512 || config.limits.fragment_payload_bytes>896)return fail(Error::InvalidArgument);
    bool have_control=false;
    for(unsigned i=0;i<config.logical_channels;++i){if(config.channel_carriers[i]!=CarrierLane::Control&&config.channel_carriers[i]!=CarrierLane::State)return fail(Error::InvalidArgument);if(config.channel_purposes[i]==ChannelPurpose::Control)config.channel_carriers[i]=CarrierLane::Control;}
    for(unsigned i=0;i<config.logical_channels;++i){if(static_cast<unsigned>(config.channel_modes[i])>static_cast<unsigned>(DeliveryMode::Unreliable)||static_cast<unsigned>(config.channel_purposes[i])>static_cast<unsigned>(ChannelPurpose::Control)||!config.channel_message_bytes[i]||config.channel_message_bytes[i]>65536)return fail(Error::InvalidArgument);
        config.channel_message_bytes[i]=std::min(config.channel_message_bytes[i],config.limits.max_message_bytes);
        if(config.channel_modes[i]==DeliveryMode::Unreliable)config.channel_message_bytes[i]=std::min(config.channel_message_bytes[i],std::uint32_t{config.limits.fragment_payload_bytes});
        if(config.channel_purposes[i]==ChannelPurpose::Control){if(config.channel_modes[i]!=DeliveryMode::ReliableOrdered)return fail(Error::InvalidArgument);have_control=true;config.channel_message_bytes[i]=std::min(config.channel_message_bytes[i],std::uint32_t{4096});}}
    auto admitted=admit_capabilities(config.capabilities,config.capabilities,config.admission); if(!admitted)return fail(admitted.error());
    if(config.limits.max_message_bytes>admitted->maximum_encoded_bytes || config.limits.max_message_bytes>admitted->maximum_decoded_bytes)return fail(Error::Unsupported);
    if(known_fingerprint(config.schema_fingerprint) && config.schema_fingerprint!=config.capabilities.schemas)return fail(Error::IncompatibleSchema);
    config.schema_fingerprint=config.capabilities.schemas;
    auto instance=new_session_instance(); if(!instance)return fail(instance.error());
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Session); if(!memory)return fail(Error::OutOfMemory);
    Session result; result.allocator_=&allocator; result.impl_=new(memory)Impl(allocator,transport,config); auto& s=*result.impl_;
    s.instance=*instance;s.split_carriers=caps.split_carriers;
    if(auto resized=s.tx.resize(256*1024);!resized)return fail(resized.error());
    if(auto resized=s.rx.resize(256*1024);!resized)return fail(resized.error());
    if(have_control){if(auto resized=s.control_tx.resize(32*1024);!resized)return fail(resized.error());if(auto resized=s.control_rx.resize(32*1024);!resized)return fail(resized.error());}
    for(std::uint16_t channel=0;channel<config.logical_channels;++channel) {
        auto limits=config.limits;limits.max_message_bytes=config.channel_message_bytes[channel];const bool control=config.channel_purposes[channel]==ChannelPurpose::Control;if(control)limits.max_messages=8;
        auto sender=DeliverySender::create_shared(config.channel_modes[channel],config.epoch,limits,control?s.control_tx_slots:s.tx_slots,control?s.control_tx.bytes():s.tx.bytes(),channel);
        auto receiver=DeliveryReceiver::create_shared(config.channel_modes[channel],config.epoch,limits,control?s.control_rx_slots:s.rx_slots,control?s.control_rx.bytes():s.rx.bytes(),channel);
        if(!sender)return fail(sender.error()); if(!receiver)return fail(receiver.error());
        s.senders[channel].emplace(std::move(*sender)); s.receivers[channel].emplace(std::move(*receiver));
    } return result;
}
bool Session::ready() const noexcept {
    return impl_ && impl_->owned() && !impl_->failed && impl_->transport->ready() && impl_->hello_received && impl_->hello_acknowledged;
}
Result<std::uint64_t> Session::instance_identity() const noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->owned())return fail(Error::PermissionDenied);return impl_->instance;
}
Result<AdmittedCapabilities> Session::capabilities() const noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady); return impl_->admitted;
}
Status Session::pump(Tick now) noexcept {
    // Every frame written during this pump leaves together: a coalescing
    // transport bundles receipts, probes and data with a piggybacked ACK.
    auto result=pump_frames(now);
    if(impl_ && impl_->owned() && !impl_->failed) {
        auto flushed=impl_->transport->flush();
        if(!flushed && flushed.error()!=Error::Busy) { impl_->failed=true; return flushed; }
    }
    return result;
}
Status Session::pump_frames(Tick now) noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied); auto& s=*impl_;
    if(s.failed)return fail(Error::ChannelFailed);
    if(s.have_tick && now<s.last_tick)return fail(Error::InvalidArgument);
    if(!s.have_tick) { s.started_at=now; s.have_tick=true; } s.last_tick=now;
    // HELLO admission expires independently of carrier progress, writability
    // and crypto readiness. A pending/Busy carrier cannot suppress this timer.
    // An already admitted session does not reuse its startup deadline during
    // a later transport association handoff.
    if(!(s.hello_received && s.hello_acknowledged) && now-s.started_at>=s.config.limits.timeout_ticks) {
        s.failed=true; return fail(Error::Timeout);
    }
    auto progressed=s.transport->advance_frames(); if(!progressed && progressed.error()!=Error::Busy) { s.failed=true; return fail(progressed.error()); }
    if(!s.transport->ready())return fail(Error::Busy);
    std::array<std::byte,960> frame{};
    for(unsigned work=0;work<64;++work) {
        auto n=s.transport->receive_frame(frame); if(!n) { if(n.error()==Error::Busy)break; s.failed=true; return fail(n.error()); }
        if(!n->bytes || n->bytes>frame.size() || (s.split_carriers?(n->lane!=CarrierLane::Control&&n->lane!=CarrierLane::State):n->lane!=CarrierLane::Single)) { s.failed=true; return fail(Error::ProtocolViolation); }
        auto bytes=std::span<const std::byte>(frame.data(),n->bytes); auto kind=std::to_integer<unsigned>(bytes[0]); auto payload=bytes.subspan(1); Status received;
        if(s.split_carriers && kind!=0 && n->lane!=CarrierLane::Control)received=fail(Error::ProtocolViolation);
        else if(kind==1 || kind==2)received=s.hello(payload,kind==2);
        else if(kind==0 && ready()) {
            auto fragment=decode_fragment(payload,s.config.limits.fragment_payload_bytes);
            if(!fragment)received=fail(fragment.error());
            else if(fragment->channel>=s.config.logical_channels)received=fail(Error::ProtocolViolation);
            else if(s.split_carriers&&n->lane!=s.config.channel_carriers[fragment->channel])received=fail(Error::ProtocolViolation);
            else if(fragment->total_bytes>s.config.channel_message_bytes[fragment->channel])received=fail(Error::ProtocolViolation);
            else {
                auto& receiver=*s.receivers[fragment->channel]; auto admitted=receiver.receive(*fragment,now);
                // Shared resource pressure drops this frame; reliable logical
                // retransmission retries after capacity becomes available.
                if(!admitted) { if(admitted.error()!=Error::CapacityExceeded)received=fail(admitted.error()); }
                else received=s.acknowledge(static_cast<std::uint8_t>(fragment->channel),fragment->message);
            }
        } else if(kind==3 && ready()) {
            Reader r(payload); auto epoch=r.u64(),message=r.u64(),channel=r.varuint(),stage=r.varuint();
            if(!epoch || !message || !channel || !stage || !r.empty() || *epoch!=s.config.epoch || *channel>=s.config.logical_channels ||
                (*stage!=static_cast<unsigned>(DeliveryStage::Received) && *stage!=static_cast<unsigned>(DeliveryStage::Applied)))received=fail(Error::ProtocolViolation);
            else received=s.senders[*channel]->receipt(*message,static_cast<DeliveryStage>(*stage),now);
        } else if(kind==4 && ready()) {
            auto receipt=decode_fragment_receipt(payload);
            if(!receipt)received=fail(receipt.error());
            else if(receipt->channel>=s.config.logical_channels)received=fail(Error::ProtocolViolation);
            else received=s.senders[receipt->channel]->fragment_receipt(*receipt,now);
        } else if(kind==5 && ready()) {
            Reader r(payload); auto epoch=r.u64(),message=r.u64(),channel=r.varuint();
            if(!epoch || !message || !channel || !r.empty() || *epoch!=s.config.epoch || !*message || *channel>=s.config.logical_channels)received=fail(Error::ProtocolViolation);
            else received=s.acknowledge(static_cast<std::uint8_t>(*channel),*message);
        // A reordered authenticated data/receipt frame can outrun the final
        // hello acknowledgement. Drop it; logical retries resume after readiness.
        } else if(kind==0 || kind==3 || kind==4 || kind==5)received={};
        else received=fail(Error::ProtocolViolation);
        if(!received) { s.failed=true; return received; }
    }
    for(unsigned channel=0;channel<s.config.logical_channels;++channel) {
        auto a=s.senders[channel]->expire(now),b=s.receivers[channel]->expire(now);
        if(!a || !b) { s.failed=true; return !a?a:b; }
    }
    if(!progressed)return fail(Error::Busy);
    bool control_writable=progressed->control_writable,state_writable=progressed->state_writable,blocked=false;
    if(s.ack_hello && control_writable) {
        auto sent=s.send_hello(true); if(!sent && sent.error()!=Error::Busy) { s.failed=true; return sent; }
        s.ack_hello=false; if(!sent)return sent;
    }
    if(control_writable && !s.hello_acknowledged && (!s.hello_sent || now-s.hello_at>=6)) {
        auto sent=s.send_hello(false); if(!sent && sent.error()!=Error::Busy) { s.failed=true; return sent; }
        s.hello_sent=true; s.hello_at=now; if(!sent)return sent;
    }
    if(!ready())return fail(Error::Busy);
    for(auto& receipt:s.receipts) if(receipt.active && control_writable) {
        if(receipt.have_stage) {
            frame[0]=std::byte{3}; Writer w(std::span(frame).subspan(1));
            if(!w.u64(s.config.epoch) || !w.u64(receipt.message) || !w.varuint(receipt.channel) || !w.varuint(static_cast<unsigned>(receipt.stage)))return fail(Error::ProtocolViolation);
            auto sent=s.transport->send_frame({frame.data(),w.size()+1},CarrierLane::Control);
            if(!sent && sent.error()!=Error::Busy) { s.failed=true; return sent; }
            receipt.have_stage=false; if(!receipt.have_fragments)receipt.active=false; if(!sent){if(!s.split_carriers)return sent;control_writable=false;blocked=true;continue;}
        }
        if(receipt.have_fragments) {
            frame[0]=std::byte{4}; auto encoded=encode_fragment_receipt(receipt.fragments,std::span(frame).subspan(1));
            if(!encoded)return fail(encoded.error()); auto sent=s.transport->send_frame({frame.data(),*encoded+1},CarrierLane::Control);
            if(!sent && sent.error()!=Error::Busy) { s.failed=true; return sent; }
            receipt.have_fragments=false; receipt.active=false; if(!sent){if(!s.split_carriers)return sent;control_writable=false;blocked=true;}
        }
    }
    // Rotate across channels for each frame, including across pump boundaries.
    // A continuously active bulk channel cannot consume another channel's turn.
    unsigned frames=0,scanned=0;
    // Coalescing carriers accept many frames per pump; others stop on Busy.
    while(frames<32 && scanned<s.config.logical_channels) {
        auto channel=s.next_channel; s.next_channel=static_cast<std::uint8_t>((channel+1)%s.config.logical_channels); ++scanned;
        auto& sender=*s.senders[channel]; auto attempt=sender.next(now);
        if(!attempt) { if(attempt.error()==Error::NotReady || attempt.error()==Error::Busy)continue; s.failed=true; return fail(attempt.error()); }
        auto lane=attempt->receipt_probe?CarrierLane::Control:s.config.channel_carriers[channel];
        auto& writable=lane==CarrierLane::Control?control_writable:state_writable;
        if(!writable){auto deferred=sender.carrier_result(attempt->attempt,false,now);if(!deferred){s.failed=true;return deferred;}blocked=true;continue;}
        frame[0]=std::byte(attempt->receipt_probe?5:0);
        Result<std::size_t> encoded;
        if(attempt->receipt_probe) {
            Writer w(std::span(frame).subspan(1));
            if(!w.u64(attempt->fragment.epoch) || !w.u64(attempt->fragment.message) || !w.varuint(attempt->fragment.channel))encoded=fail(Error::ProtocolViolation);
            else encoded=w.size();
        } else encoded=encode_fragment(attempt->fragment,std::span(frame).subspan(1),s.config.limits.fragment_payload_bytes);
        if(!encoded) { s.failed=true; return fail(encoded.error()); }
        auto sent=s.transport->send_frame({frame.data(),*encoded+1},attempt->receipt_probe?CarrierLane::Control:s.config.channel_carriers[channel]); const bool accepted=sent.has_value() || sent.error()==Error::Busy;
        auto recorded=sender.carrier_result(attempt->attempt,accepted,now); if(!recorded) { s.failed=true; return recorded; }
        ++frames; scanned=0;
        if(!sent) {if(sent.error()!=Error::Busy){s.failed=true;return sent;}if(!s.split_carriers)return sent;writable=false;blocked=true;}
    } return blocked?Status(fail(Error::Busy)):Status{};
}
Status Session::set_retry_ticks(Tick ticks) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(impl_->failed)return fail(Error::ChannelFailed);
    for(auto& sender:impl_->senders)if(sender)if(auto set=sender->set_retry_ticks(ticks);!set)return set;
    return {};
}
Result<DeliveryTicket> Session::send(std::span<const std::byte> bytes,Tick now,std::uint8_t channel) noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady); if(channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument);
    if(impl_->have_tick && now<impl_->last_tick)return fail(Error::InvalidArgument);
    auto id=impl_->senders[channel]->admit(bytes,now); if(!id)return fail(id.error()); return DeliveryTicket{impl_->config.epoch,*id,channel,impl_->config.channel_modes[channel]};
}
Result<DeliveryView> Session::receive(std::uint8_t channel) const noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady); if(channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument); return impl_->receivers[channel]->ready();
}
Result<std::uint8_t> Session::channel_count() const noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady); return impl_->config.logical_channels;
}
Result<DeliveryMode> Session::channel_mode(std::uint8_t channel) const noexcept {
    auto count=channel_count(); if(!count)return fail(count.error());
    if(channel>=*count)return fail(Error::InvalidArgument); return impl_->config.channel_modes[channel];
}
Result<SessionIdentity> Session::identity() const noexcept {
    auto count=channel_count(); if(!count)return fail(count.error());
    return SessionIdentity{impl_->config.session_id,impl_->config.epoch,impl_->config.local_peer,impl_->config.remote_peer,impl_->config.capabilities.schemas,impl_->config.capabilities.simulation,impl_->instance};
}
Result<DeliveryLimits> Session::delivery_limits() const noexcept { auto count=channel_count();if(!count)return fail(count.error());return impl_->config.limits; }
Result<CarrierLane> Session::channel_carrier(std::uint8_t channel) const noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady);if(channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument);return impl_->config.channel_carriers[channel];
}
Result<ChannelPurpose> Session::channel_purpose(std::uint8_t channel) const noexcept {auto count=channel_count();if(!count)return fail(count.error());if(channel>=*count)return fail(Error::InvalidArgument);return impl_->config.channel_purposes[channel];}
Result<std::uint32_t> Session::channel_message_bytes(std::uint8_t channel) const noexcept {auto count=channel_count();if(!count)return fail(count.error());if(channel>=*count)return fail(Error::InvalidArgument);return impl_->config.channel_message_bytes[channel];}
Result<bool> Session::storage_overlaps(std::span<const std::byte> bytes) const noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->owned())return fail(Error::PermissionDenied);
    auto overlaps=[&](const void* address,std::size_t size) noexcept {if(bytes.empty()||!size)return false;const auto a=reinterpret_cast<std::uintptr_t>(address),b=reinterpret_cast<std::uintptr_t>(bytes.data());return a<=b?b-a<size:a-b<bytes.size();};
    return overlaps(this,sizeof(*this))||overlaps(impl_,sizeof(Impl))||overlaps(impl_->tx.bytes().data(),impl_->tx.bytes().size())||overlaps(impl_->rx.bytes().data(),impl_->rx.bytes().size())||overlaps(impl_->control_tx.bytes().data(),impl_->control_tx.bytes().size())||overlaps(impl_->control_rx.bytes().data(),impl_->control_rx.bytes().size());
}
Status Session::applied(std::uint64_t message,std::uint8_t channel) noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(!ready())return fail(Error::NotReady); if(channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument);
    if(!impl_->receipt_capacity(channel,message)) {
        // Receipts back up while a slow carrier is paced. A lossy channel applies
        // without one (its sender retires unreliable tickets by timeout); a reliable
        // channel reports Busy so the application retries after receipts drain.
        const auto mode=impl_->config.channel_modes[channel];
        if(mode!=DeliveryMode::Unreliable && mode!=DeliveryMode::UnreliableLatest)return fail(Error::Busy);
        return impl_->receivers[channel]->applied(message);
    }
    if(auto applied=impl_->receivers[channel]->applied(message);!applied)return applied;
    return impl_->receipt(channel,message,DeliveryStage::Applied);
}
Result<Outcome> Session::outcome(DeliveryTicket ticket) const noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(ticket.epoch!=impl_->config.epoch)return fail(Error::StaleEpoch); if(ticket.channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument);
    if(ticket.mode!=impl_->config.channel_modes[ticket.channel])return fail(Error::InvalidArgument);
    if(impl_->failed)return fail(Error::ChannelFailed); auto stage=impl_->senders[ticket.channel]->stage(ticket.message); if(!stage)return fail(stage.error());
    return *stage==DeliveryStage::Applied?Outcome::Applied:*stage==DeliveryStage::Received?Outcome::Received:Outcome::Accepted;
}
Status Session::retire(DeliveryTicket ticket) noexcept {
    if(!impl_)return fail(Error::NotReady); if(!impl_->owned())return fail(Error::PermissionDenied);
    if(ticket.epoch!=impl_->config.epoch)return fail(Error::StaleEpoch); if(ticket.channel>=impl_->config.logical_channels)return fail(Error::InvalidArgument);
    if(ticket.mode!=impl_->config.channel_modes[ticket.channel])return fail(Error::InvalidArgument);
    if(impl_->failed)return fail(Error::ChannelFailed); return impl_->senders[ticket.channel]->retire(ticket.message);
}
} // namespace superpos
