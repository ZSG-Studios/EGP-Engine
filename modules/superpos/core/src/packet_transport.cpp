#include "superpos/packet_transport.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace superpos {
namespace {
constexpr std::byte data_tag{0x10}, ack_tag{0x11}, probe_tag{0x12};
constexpr std::size_t raw_ceiling = 960, ack_bytes = 33;
struct ActiveCall {
    bool& active;
    explicit ActiveCall(bool& value) noexcept : active(value) { active=true; }
    ~ActiveCall() { active=false; }
};
bool overlaps(const void* bytes,std::size_t size,const void* object,std::size_t extent) noexcept {
    if(!size)return false;
    const auto begin=reinterpret_cast<std::uintptr_t>(bytes);
    const auto other=reinterpret_cast<std::uintptr_t>(object);
    if(!bytes||size>std::numeric_limits<std::uintptr_t>::max()-begin)return true;
    return begin<other?other-begin<size:begin-other<extent;
}
}
struct PacketTransport::Impl {
    Clock* clock;
    TransportProvider* provider;
    PacketTransportConfig config;
    TransportCapabilities carrier;
    std::thread::id owner{std::this_thread::get_id()};
    std::array<PacketRecord,64> records{};
    std::optional<PacketCongestion> flow;
    std::array<std::byte,raw_ceiling> pending{};
    std::size_t pending_size{};
    struct Frame { std::array<std::byte,raw_ceiling-header_bytes> bytes{}; std::size_t size{}; };
    std::array<Frame,8> received{};
    std::size_t head{},count{};
    std::uint64_t largest{},bits{},largest_received_at{},ack_due{},last_time{};
    bool have_time{},ack_dirty{},carrier_blocked{},probe_due{},failed{},active_call{};
    PacketTransportStats stats{};
    Impl(Clock& c,TransportProvider& p,PacketTransportConfig cfg,TransportCapabilities caps) noexcept
        :clock(&c),provider(&p),config(cfg),carrier(caps) {}
    bool owned() const noexcept { return owner==std::this_thread::get_id(); }
    std::size_t maximum() const noexcept {
        const auto overhead=carrier.encrypted_overhead_bytes+config.routing_overhead_bytes;
        return std::min<std::size_t>(carrier.maximum_frame,config.congestion.datagram_bytes-overhead)-header_bytes;
    }
    std::uint32_t charge(std::size_t plain) const noexcept {
        return static_cast<std::uint32_t>(plain)+carrier.encrypted_overhead_bytes+config.routing_overhead_bytes;
    }
    Status can_record_send(std::uint32_t bytes,std::uint64_t counter) const noexcept {
        const auto ceiling=std::numeric_limits<std::uint64_t>::max();
        if(counter==ceiling || bytes>ceiling-stats.charged_wire_bytes)return fail(Error::CounterExhausted);
        return {};
    }
    static Status increment(std::uint64_t& counter) noexcept {
        if(counter==std::numeric_limits<std::uint64_t>::max())return fail(Error::CounterExhausted);
        ++counter;return {};
    }
    Result<std::uint64_t> now() noexcept {
        const auto ms=clock->now_ms();
        if(ms>std::numeric_limits<std::uint64_t>::max()/1000)return fail(Error::CounterExhausted);
        const auto us=ms*1000;
        if(have_time && us<last_time)return fail(Error::InvalidArgument);
        have_time=true;last_time=us;return us;
    }
    Status header(std::span<std::byte> bytes,std::byte kind,std::uint64_t id) noexcept {
        bytes[0]=kind;Writer writer(bytes.subspan(1));
        if(!writer.u64(config.association_epoch)||!writer.u64(id))return fail(Error::ProtocolViolation);
        return {};
    }
    bool seen(std::uint64_t id) const noexcept {
        return id<=largest && (largest-id>=64 || (bits&(std::uint64_t{1}<<(largest-id))));
    }
    void mark(std::uint64_t id,std::uint64_t now_us) noexcept {
        if(id>largest) {
            const auto distance=id-largest;
            bits=distance>=64?1:(bits<<distance)|1;
            largest=id;largest_received_at=now_us;
        } else if(largest-id<64)bits|=std::uint64_t{1}<<(largest-id);
        if(!ack_dirty) {
            const auto delay=config.congestion.maximum_ack_delay_us;
            ack_due=now_us>std::numeric_limits<std::uint64_t>::max()-delay?std::numeric_limits<std::uint64_t>::max():now_us+delay;
        }
        ack_dirty=true;
    }
    Status flush_ack(std::uint64_t now_us) noexcept {
        if(!ack_dirty || now_us<ack_due)return {};
        if(carrier_blocked || now_us<flow->next_send_us())return fail(Error::Busy);
        std::array<std::byte,ack_bytes> bytes{};
        if(auto status=header(bytes,ack_tag,largest);!status)return status;
        Writer writer(std::span(bytes).subspan(header_bytes));
        if(!writer.u64(bits)||!writer.u64(std::min(now_us-largest_received_at,config.congestion.maximum_ack_delay_us)))return fail(Error::ProtocolViolation);
        if(auto capacity=can_record_send(charge(bytes.size()),stats.acknowledgements_sent);!capacity)return capacity;
        auto accepted=provider->send(bytes);
        if(!accepted && accepted.error()!=Error::Busy)return accepted;
        if(auto charged=flow->sent_untracked(charge(bytes.size()),now_us);!charged)return charged;
        stats.charged_wire_bytes+=charge(bytes.size());++stats.acknowledgements_sent;
        ack_dirty=false;carrier_blocked=!accepted;
        return accepted;
    }
    Status flush_data(std::uint64_t now_us) noexcept {
        if(!pending_size)return {};
        if(carrier_blocked)return fail(Error::Busy);
        auto allowed=flow->can_send(charge(pending_size),now_us);
        if(!allowed) {
            if(allowed.error()==Error::CapacityExceeded)return fail(Error::Busy);
            return allowed;
        }
        auto id=flow->next_packet_number();if(!id)return fail(id.error());
        if(auto encoded=header(pending,data_tag,*id);!encoded)return encoded;
        if(auto capacity=can_record_send(charge(pending_size),stats.data_sent);!capacity)return capacity;
        auto accepted=provider->send(std::span<const std::byte>(pending).first(pending_size));
        if(!accepted && accepted.error()!=Error::Busy)return accepted;
        auto recorded=flow->sent(charge(pending_size),now_us);
        if(!recorded || *recorded!=*id)return fail(Error::ProtocolViolation);
        stats.charged_wire_bytes+=charge(pending_size);++stats.data_sent;
        pending_size=0;carrier_blocked=!accepted;
        return accepted;
    }
    Status flush_probe(std::uint64_t now_us) noexcept {
        if(!probe_due)return {};
        if(carrier_blocked || now_us<flow->next_send_us())return fail(Error::Busy);
        if(flow->outstanding_packets()==records.size())return fail(Error::Busy);
        auto id=flow->next_packet_number();if(!id)return fail(id.error());
        std::array<std::byte,header_bytes> bytes{};
        if(auto encoded=header(bytes,probe_tag,*id);!encoded)return encoded;
        if(auto capacity=can_record_send(charge(bytes.size()),stats.probes_sent);!capacity)return capacity;
        auto accepted=provider->send(bytes);
        if(!accepted && accepted.error()!=Error::Busy)return accepted;
        auto recorded=flow->sent_probe(charge(bytes.size()),now_us);
        if(!recorded || *recorded!=*id)return fail(Error::ProtocolViolation);
        stats.charged_wire_bytes+=charge(bytes.size());++stats.probes_sent;
        probe_due=false;carrier_blocked=!accepted;
        return accepted;
    }
    Status input(std::span<const std::byte> frame,std::uint64_t now_us) noexcept {
        if(frame.size()<header_bytes || frame.size()>carrier.maximum_frame)return fail(Error::ProtocolViolation);
        Reader reader(frame.subspan(1));auto epoch=reader.u64(),id=reader.u64();
        if(!epoch||!id||!*id)return fail(Error::ProtocolViolation);
        if(*epoch!=config.association_epoch)return fail(Error::StaleEpoch);
        const auto payload=frame.subspan(header_bytes);
        if(frame[0]==ack_tag) {
            auto ack_bits=reader.u64(),delay=reader.u64();
            if(!ack_bits||!delay||!reader.empty())return fail(Error::NonCanonical);
            auto progress=flow->acknowledge({*id,*ack_bits,*delay},now_us);
            if(!progress)return fail(progress.error());
            if(progress->acknowledged_packets)probe_due=false;
            return {};
        }
        if(frame[0]!=data_tag && frame[0]!=probe_tag)return fail(Error::NonCanonical);
        if((frame[0]==probe_tag && !payload.empty()) || (frame[0]==data_tag && (payload.empty()||payload.size()>maximum())))return fail(Error::NonCanonical);
        if(seen(*id)) {
            if(auto counted=increment(stats.dropped_data);!counted)return counted;
            // Re-send the current receipt for a duplicate, without creating an
            // ACK-of-ACK loop or another application delivery.
            mark(*id,now_us);return {};
        }
        if(frame[0]==probe_tag) { mark(*id,now_us);return {}; }
        if(count==config.receive_frames) {
            if(auto counted=increment(stats.dropped_data);!counted)return counted;
            mark(*id,now_us);return {};
        }
        if(auto counted=increment(stats.received_data);!counted)return counted;
        mark(*id,now_us);
        auto& destination=received[(head+count)%received.size()];
        std::memcpy(destination.bytes.data(),payload.data(),payload.size());
        destination.size=payload.size();++count;
        return {};
    }
};
PacketTransport::~PacketTransport() {
    if(!impl_)return;
    if(!impl_->owned()||impl_->active_call)std::abort();
    auto* retired=std::exchange(impl_,nullptr);
    auto* allocator=std::exchange(allocator_,nullptr);
    retired->~Impl();allocator->deallocate(retired);
}
PacketTransport::PacketTransport(PacketTransport&& other) noexcept {
    if(other.impl_&&(!other.impl_->owned()||other.impl_->active_call))std::abort();
    impl_=std::exchange(other.impl_,nullptr);
    allocator_=std::exchange(other.allocator_,nullptr);
}
PacketTransport& PacketTransport::operator=(PacketTransport&& other) noexcept {
    // Validate both existing owners before retiring or stealing an association.
    if((impl_&&(!impl_->owned()||impl_->active_call))||(other.impl_&&(!other.impl_->owned()||other.impl_->active_call)))std::abort();
    if(this==&other)return *this;
    auto* retired=std::exchange(impl_,nullptr);
    auto* allocator=std::exchange(allocator_,nullptr);
    if(retired){retired->~Impl();allocator->deallocate(retired);}
    impl_=std::exchange(other.impl_,nullptr);
    allocator_=std::exchange(other.allocator_,nullptr);
    return *this;
}
Result<PacketTransport> PacketTransport::create(Allocator& allocator,Clock& clock,TransportProvider& provider,PacketTransportConfig config) noexcept {
    const auto caps=provider.capabilities();
    if(!caps.authenticated||!caps.encrypted||!caps.datagram||caps.browser||caps.split_carriers||caps.encrypted_overhead_bytes!=37||caps.maximum_frame>raw_ceiling)return fail(Error::Unsupported);
    const auto overhead=static_cast<std::uint64_t>(caps.encrypted_overhead_bytes)+config.routing_overhead_bytes;
    if(!config.association_epoch||!config.receive_frames||config.receive_frames>8||
        caps.maximum_frame<ack_bytes||overhead+ack_bytes>config.congestion.datagram_bytes||config.congestion.maximum_ack_delay_us>25000)return fail(Error::InvalidArgument);
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);if(!memory)return fail(Error::OutOfMemory);
    PacketTransport result;result.allocator_=&allocator;result.impl_=new(memory)Impl(clock,provider,config,caps);
    auto flow=PacketCongestion::create(config.congestion,result.impl_->records);
    if(!flow)return fail(flow.error());result.impl_->flow.emplace(std::move(*flow));return result;
}
TransportCapabilities PacketTransport::capabilities() const noexcept {
    return impl_?TransportCapabilities{true,true,true,false,impl_->maximum(),0}:TransportCapabilities{};
}
bool PacketTransport::ready() const noexcept {
    if(!impl_||!impl_->owned()||impl_->failed||impl_->active_call)return false;
    ActiveCall guard(impl_->active_call);
    return impl_->provider->ready();
}
Status PacketTransport::advance() noexcept {
    if(!impl_)return fail(Error::NotReady);auto& self=*impl_;
    if(!self.owned()||self.active_call)return fail(Error::PermissionDenied);if(self.failed)return fail(Error::ChannelFailed);
    ActiveCall guard(self.active_call);
    auto now=self.now();if(!now) { self.failed=true;return fail(now.error()); }
    auto raw=self.provider->advance();
    if(!raw) {
        if(raw.error()==Error::Busy)return raw;
        self.failed=true;return raw;
    }
    self.carrier_blocked=false;
    if(!self.provider->ready())return fail(Error::Busy);
    std::array<std::byte,raw_ceiling> bytes{};
    for(unsigned quantum=0;quantum<16;++quantum) {
        auto received=self.provider->receive(bytes);
        if(!received) {
            if(received.error()==Error::Busy)break;
            if(received.error()==Error::CapacityExceeded) {
                if(auto counted=Impl::increment(self.stats.dropped_data);!counted) { self.failed=true;return counted; }
                continue;
            }
            self.failed=true;return fail(received.error());
        }
        if(*received>bytes.size()) { self.failed=true;return fail(Error::ProtocolViolation); }
        auto consumed=self.input(std::span<const std::byte>(bytes).first(*received),*now);
        if(!consumed) { self.failed=true;return consumed; }
    }
    auto loss=self.flow->detect_loss(*now);if(!loss) { self.failed=true;return fail(loss.error()); }
    if(self.flow->bytes_in_flight() && !self.probe_due) {
        auto due=self.flow->probe_timeout(*now);
        if(due)self.probe_due=true;
        else if(due.error()!=Error::Busy) { self.failed=true;return due; }
    }
    auto ack=self.flush_ack(*now);if(!ack) { if(ack.error()!=Error::Busy)self.failed=true;return ack; }
    auto probe=self.flush_probe(*now);if(!probe) { if(probe.error()!=Error::Busy)self.failed=true;return probe; }
    auto data=self.flush_data(*now);if(!data && data.error()!=Error::Busy)self.failed=true;return data;
}
Status PacketTransport::send(std::span<const std::byte> bytes) noexcept {
    if(!impl_)return fail(Error::NotReady);auto& self=*impl_;
    if(!self.owned()||self.active_call)return fail(Error::PermissionDenied);
    if(overlaps(bytes.data(),bytes.size(),this,sizeof(*this))||overlaps(bytes.data(),bytes.size(),impl_,sizeof(Impl)))return fail(Error::InvalidArgument);
    ActiveCall guard(self.active_call);
    if(self.failed||!self.provider->ready())return fail(Error::NotReady);
    if(bytes.empty()||bytes.size()>self.maximum()||self.pending_size||self.carrier_blocked)return fail(Error::CapacityExceeded);
    auto now=self.now();if(!now) { self.failed=true;return fail(now.error()); }
    std::memcpy(self.pending.data()+header_bytes,bytes.data(),bytes.size());self.pending_size=bytes.size()+header_bytes;
    auto sent=self.flush_data(*now);if(!sent && sent.error()!=Error::Busy)self.failed=true;return sent;
}
Result<std::size_t> PacketTransport::receive(std::span<std::byte> output) noexcept {
    if(!impl_)return fail(Error::NotReady);auto& self=*impl_;
    if(!self.owned()||self.active_call)return fail(Error::PermissionDenied);if(self.failed)return fail(Error::ChannelFailed);
    if(!self.count)return fail(Error::Busy);
    // Reject known manager/pool aliases before consuming a queued datagram.
    if(overlaps(output.data(),output.size(),this,sizeof(*this))||overlaps(output.data(),output.size(),impl_,sizeof(Impl)))return fail(Error::InvalidArgument);
    auto& source=self.received[self.head];const auto size=source.size;
    self.head=(self.head+1)%self.received.size();--self.count;source.size=0;
    if(output.size()<size)return fail(Error::CapacityExceeded);
    std::memcpy(output.data(),source.bytes.data(),size);return size;
}
Result<PacketTransportStats> PacketTransport::statistics() const noexcept {
    if(!impl_)return fail(Error::NotReady);const auto& self=*impl_;
    if(!self.owned()||self.active_call)return fail(Error::PermissionDenied);
    auto stats=self.stats;stats.bytes_in_flight=self.flow->bytes_in_flight();stats.congestion_window=self.flow->congestion_window();
    stats.smoothed_rtt_us=self.flow->smoothed_rtt_us();stats.queued_receive_frames=self.count;
    stats.owns_pending_send=self.pending_size!=0||self.carrier_blocked;return stats;
}
}
