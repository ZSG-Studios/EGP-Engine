#include "superpos/udp_mux.hpp"
#include "superpos/codec.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace superpos {
namespace {
// Largest DTLS datagram behind the routing prefix.
constexpr std::size_t payload_ceiling=1200-UdpMux::routing_bytes;
// DTLS 1.2 record header: type, version (2), epoch (2), sequence (6), length (2).
constexpr std::size_t record_header=13;
bool same(const IpEndpoint& a,const IpEndpoint& b) noexcept {
    return a.length==b.length && std::memcmp(a.address.data(),b.address.data(),a.length)==0;
}
std::uint64_t mix(std::uint64_t value) noexcept {
    value^=value>>33;value*=0xff51afd7ed558ccdULL;value^=value>>33;value*=0xc4ceb9fe1a85ec53ULL;value^=value>>33;return value;
}
}
struct UdpMux::Impl {
    struct Datagram { std::uint16_t size{}; bool candidate{}; std::uint64_t generation{}; std::array<std::byte,payload_ceiling> bytes{}; };
    struct Path { IpEndpoint endpoint{}; bool present{},validated{}; std::uint64_t generation{},received{},sent{}; };
    struct Slot {
        bool attached{};
        std::uint64_t connection{};
        Datagram* queue{};
        std::size_t head{},count{};
        Path current{},candidate{};
        DatagramPath last{};
    };
    Allocator* allocator{};
    UdpMuxConfig config{};
    std::array<UdpListener,2> listeners{};
    std::array<std::uint32_t,2> lengths{};
    std::size_t listener_count{};
    Slot* slots{};
    // Open addressing with linear probing and backward-shift deletion; the
    // table is twice the slot count (power of two), so probes stay short.
    std::uint32_t* table{};
    std::size_t table_size{};
    std::uint64_t next_generation{1};
    UdpMuxStatistics stats{};
    static constexpr std::uint32_t empty=std::numeric_limits<std::uint32_t>::max();
    std::size_t home(std::uint64_t connection) const noexcept { return static_cast<std::size_t>(mix(connection))&(table_size-1); }
    Slot* find(std::uint64_t connection) noexcept {
        for(std::size_t i=home(connection),n=0;n<table_size;i=(i+1)&(table_size-1),++n) {
            if(table[i]==empty)return nullptr;
            if(slots[table[i]].connection==connection)return &slots[table[i]];
        }
        return nullptr;
    }
    void insert(std::uint32_t index) noexcept {
        auto i=home(slots[index].connection);while(table[i]!=empty)i=(i+1)&(table_size-1);table[i]=index;
    }
    void erase(std::uint64_t connection) noexcept {
        auto i=home(connection);
        while(table[i]!=empty && slots[table[i]].connection!=connection)i=(i+1)&(table_size-1);
        if(table[i]==empty)return;
        table[i]=empty;
        // Backward shift: re-seat every following entry of the cluster.
        for(auto j=(i+1)&(table_size-1);table[j]!=empty;j=(j+1)&(table_size-1)) {
            const auto moved=table[j];table[j]=empty;insert(moved);
        }
    }
    void clear_paths(Slot& slot) noexcept { slot.current={};slot.candidate={};slot.last={};slot.head=slot.count=0; }
    bool permitted(const Path& path,std::size_t bytes) const noexcept {
        if(path.validated)return true;
        const auto budget=path.received>std::numeric_limits<std::uint64_t>::max()/config.amplification_factor?
            std::numeric_limits<std::uint64_t>::max():path.received*config.amplification_factor;
        return path.sent<=budget && bytes<=budget-path.sent;
    }
    Result<std::size_t> transmit(Path& path,std::span<const std::byte> bytes) noexcept {
        if(!permitted(path,bytes.size())) { ++stats.amplification_limited;return fail(Error::CapacityExceeded); }
        // Families have distinct socket-address lengths; route by the matching one.
        UdpListener* listener=nullptr;
        for(std::size_t i=0;i<listener_count && !listener;++i)if(lengths[i]==path.endpoint.length)listener=&listeners[i];
        if(!listener)return fail(Error::Unsupported);
        auto sent=listener->send_to(path.endpoint,bytes);
        if(!sent)return sent;
        if(!path.validated)path.sent+=*sent;
        ++stats.datagrams_sent;return sent;
    }
    void route(std::span<const std::byte> datagram,const IpEndpoint& source) noexcept {
        ++stats.datagrams_received;
        if(datagram.size()<UdpMux::routing_bytes+record_header || datagram[0]!=UdpMux::routing_tag) { ++stats.malformed;return; }
        Reader reader(datagram.subspan(1,8));auto connection=reader.u64();
        if(!connection || !*connection) { ++stats.malformed;return; }
        auto* slot=find(*connection);
        if(!slot) { ++stats.unknown_connection;return; }
        const auto record=datagram.subspan(UdpMux::routing_bytes);
        DatagramPath path{};
        if(!slot->current.present) {
            // A path starts only with a DTLS handshake record in epoch zero; the
            // association's cookie exchange then binds it to this address.
            if(record[0]!=std::byte{22} || record[3]!=std::byte{} || record[4]!=std::byte{}) { ++stats.not_handshake;return; }
            slot->current={source,true,false,next_generation++,0,0};
        }
        if(same(slot->current.endpoint,source)) {
            path={slot->current.generation,false};
            if(!slot->current.validated)slot->current.received+=record.size();
        } else if(!slot->current.validated) {
            ++stats.foreign_during_handshake;return;
        } else {
            if(!slot->candidate.present || !same(slot->candidate.endpoint,source)) {
                slot->candidate={source,true,false,next_generation++,0,0};++stats.candidate_paths;
            }
            slot->candidate.received+=record.size();
            path={slot->candidate.generation,true};
        }
        if(slot->count==config.queue_datagrams) { ++stats.queue_overflow;return; }
        auto& entry=slot->queue[(slot->head+slot->count)%config.queue_datagrams];
        entry.size=static_cast<std::uint16_t>(record.size());entry.candidate=path.candidate;entry.generation=path.generation;
        std::memcpy(entry.bytes.data(),record.data(),record.size());++slot->count;++stats.datagrams_routed;
    }
    Status poll() noexcept {
        std::array<std::byte,1200> buffer{};
        for(std::size_t i=0;i<listener_count;++i) for(unsigned n=0;n<config.poll_quantum;++n) {
            auto received=listeners[i].receive_from(buffer);
            if(!received) {
                if(received.error()==Error::Busy)break;
                // Oversize datagrams are drained without exposure.
                if(received.error()==Error::CapacityExceeded) { ++stats.datagrams_received;++stats.malformed;continue; }
                return fail(received.error());
            }
            route(std::span<const std::byte>(buffer).first(received->bytes),received->source);
        }
        return {};
    }
};
UdpMux::~UdpMux() { release(); }
void UdpMux::release() noexcept {
    if(!impl_)return;
    if(impl_->stats.attached)std::abort(); // Ports must not outlive their mux.
    auto& s=*impl_;
    for(std::uint32_t i=0;i<s.config.maximum_associations;++i)if(s.slots[i].queue)allocator_->deallocate(s.slots[i].queue);
    if(s.slots){for(std::uint32_t i=0;i<s.config.maximum_associations;++i)s.slots[i].~Slot();allocator_->deallocate(s.slots);}
    if(s.table)allocator_->deallocate(s.table);
    auto* impl=std::exchange(impl_,nullptr);impl->~Impl();allocator_->deallocate(impl);
}
// Ports refer to their mux by address: moving one with attached ports would
// strand them, so that is a fatal contract violation rather than a dangling use.
UdpMux::UdpMux(UdpMux&& other) noexcept {
    if(other.impl_ && other.impl_->stats.attached)std::abort();
    impl_=std::exchange(other.impl_,nullptr);allocator_=other.allocator_;
}
UdpMux& UdpMux::operator=(UdpMux&& other) noexcept {
    if((impl_ && impl_->stats.attached) || (other.impl_ && other.impl_->stats.attached))std::abort();
    if(this!=&other) { release();impl_=std::exchange(other.impl_,nullptr);allocator_=other.allocator_; }
    return *this;
}
Result<UdpMux> UdpMux::bind(Allocator& allocator,std::span<const IpEndpoint> local,UdpMuxConfig config) noexcept {
    if(local.empty() || local.size()>2 || !config.maximum_associations || config.maximum_associations>4096 ||
        !config.queue_datagrams || config.queue_datagrams>256 || !config.poll_quantum || config.poll_quantum>4096 ||
        !config.amplification_factor || config.amplification_factor>10 ||
        config.receive_buffer_bytes>UdpOptions::maximum_receive_buffer_bytes)return fail(Error::InvalidArgument);
    if(local.size()==2 && local[0].length==local[1].length)return fail(Error::InvalidArgument); // Distinct families.
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);if(!memory)return fail(Error::OutOfMemory);
    UdpMux result;result.allocator_=&allocator;result.impl_=new(memory)Impl{};auto& s=*result.impl_;
    s.allocator=&allocator;s.config=config;
    s.table_size=1;while(s.table_size<2ULL*config.maximum_associations)s.table_size*=2;
    auto* table=allocator.allocate(sizeof(std::uint32_t)*s.table_size,alignof(std::uint32_t),MemoryDomain::Backend);
    if(!table)return fail(Error::OutOfMemory);
    s.table=static_cast<std::uint32_t*>(table);std::fill(s.table,s.table+s.table_size,Impl::empty);
    auto* slots=allocator.allocate(sizeof(Impl::Slot)*config.maximum_associations,alignof(Impl::Slot),MemoryDomain::Backend);
    if(!slots)return fail(Error::OutOfMemory);
    s.slots=static_cast<Impl::Slot*>(slots);for(std::uint32_t i=0;i<config.maximum_associations;++i)new(&s.slots[i])Impl::Slot{};
    for(const auto& endpoint:local) {
        UdpOptions options;options.receive_buffer_bytes=config.receive_buffer_bytes;
        auto listener=UdpListener::bind(endpoint,options);if(!listener)return fail(listener.error());
        s.lengths[s.listener_count]=endpoint.length;s.listeners[s.listener_count++]=std::move(*listener);
    }
    return result;
}
Result<IpEndpoint> UdpMux::local_endpoint(std::size_t index) const noexcept {
    if(!impl_)return fail(Error::NotReady);if(index>=impl_->listener_count)return fail(Error::InvalidArgument);
    return impl_->listeners[index].local_endpoint();
}
Result<std::uint32_t> UdpMux::receive_buffer_bytes(std::size_t index) const noexcept {
    if(!impl_)return fail(Error::NotReady);if(index>=impl_->listener_count)return fail(Error::InvalidArgument);
    return impl_->listeners[index].receive_buffer_bytes();
}
Result<UdpMuxPort> UdpMux::attach(std::uint64_t connection) noexcept {
    if(!impl_)return fail(Error::NotReady);auto& s=*impl_;
    if(!connection)return fail(Error::InvalidArgument);
    if(s.find(connection))return fail(Error::InvalidArgument);
    std::uint32_t index=0;while(index<s.config.maximum_associations && s.slots[index].attached)++index;
    if(index==s.config.maximum_associations)return fail(Error::CapacityExceeded);
    auto* queue=s.allocator->allocate(sizeof(Impl::Datagram)*s.config.queue_datagrams,alignof(Impl::Datagram),MemoryDomain::Backend);
    if(!queue)return fail(Error::OutOfMemory);
    auto& slot=s.slots[index];slot=Impl::Slot{};slot.attached=true;slot.connection=connection;
    slot.queue=static_cast<Impl::Datagram*>(queue);
    for(std::size_t i=0;i<s.config.queue_datagrams;++i)new(&slot.queue[i])Impl::Datagram{};
    s.insert(index);++s.stats.attached;
    UdpMuxPort port;port.mux_=this;port.slot_=index;return port;
}
Status UdpMux::poll() noexcept { if(!impl_)return fail(Error::NotReady);return impl_->poll(); }
Result<UdpMuxStatistics> UdpMux::statistics() const noexcept { if(!impl_)return fail(Error::NotReady);return impl_->stats; }

// Detach without ending the object's lifetime: move assignment reuses it, and
// an explicit destructor call would also reset its dynamic type.
void UdpMuxPort::detach() noexcept {
    auto* mux=std::exchange(mux_,nullptr);
    if(!mux || !mux->impl_)return;
    auto& s=*mux->impl_;auto& slot=s.slots[slot_];
    s.erase(slot.connection);s.allocator->deallocate(slot.queue);slot=UdpMux::Impl::Slot{};--s.stats.attached;
}
UdpMuxPort::~UdpMuxPort() { detach(); }
UdpMuxPort::UdpMuxPort(UdpMuxPort&& other) noexcept:mux_(std::exchange(other.mux_,nullptr)),slot_(other.slot_) {}
UdpMuxPort& UdpMuxPort::operator=(UdpMuxPort&& other) noexcept {
    if(this!=&other) { detach();mux_=std::exchange(other.mux_,nullptr);slot_=other.slot_; }
    return *this;
}
Result<std::size_t> UdpMuxPort::send(std::span<const std::byte> bytes) noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    if(bytes.empty())return fail(Error::InvalidArgument);
    auto& s=*mux_->impl_;auto& slot=s.slots[slot_];
    // Nothing to answer yet: no datagram has established a path.
    if(!slot.current.present)return fail(Error::Busy);
    auto sent=s.transmit(slot.current,bytes);
    // The current path's amplification limit holds the write until more of
    // the peer's bytes arrive; a refused size is the path's own drop.
    if(!sent && sent.error()==Error::CapacityExceeded && !slot.current.validated)return fail(Error::Busy);
    return sent;
}
Result<std::size_t> UdpMuxPort::receive(std::span<std::byte> output) noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    auto& s=*mux_->impl_;auto& slot=s.slots[slot_];
    if(!slot.count && s.config.poll_on_empty_receive)if(auto polled=s.poll();!polled)return fail(polled.error());
    if(!slot.count)return fail(Error::Busy);
    auto& entry=slot.queue[slot.head];slot.head=(slot.head+1)%s.config.queue_datagrams;--slot.count;
    if(entry.size>output.size())return fail(Error::CapacityExceeded);
    std::memcpy(output.data(),entry.bytes.data(),entry.size);
    slot.last={entry.generation,entry.candidate};return std::size_t{entry.size};
}
DatagramPath UdpMuxPort::received_path() const noexcept {
    if(!mux_ || !mux_->impl_)return {};return mux_->impl_->slots[slot_].last;
}
std::span<const std::byte> UdpMuxPort::path_identity() const noexcept {
    if(!mux_ || !mux_->impl_)return {};
    const auto& current=mux_->impl_->slots[slot_].current;
    if(!current.present)return {};
    return std::span<const std::byte>(current.endpoint.address.data(),current.endpoint.length);
}
Result<std::size_t> UdpMuxPort::send_candidate(std::uint64_t generation,std::span<const std::byte> bytes) noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    auto& s=*mux_->impl_;auto& slot=s.slots[slot_];
    if(!slot.candidate.present || slot.candidate.generation!=generation)return fail(Error::StaleGeneration);
    return s.transmit(slot.candidate,bytes);
}
Status UdpMuxPort::promote_candidate(std::uint64_t generation) noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    auto& s=*mux_->impl_;auto& slot=s.slots[slot_];
    if(!slot.candidate.present || slot.candidate.generation!=generation)return fail(Error::StaleGeneration);
    slot.current=slot.candidate;slot.current.validated=true;slot.candidate={};++s.stats.path_promotions;return {};
}
void UdpMuxPort::path_authenticated() noexcept {
    if(!mux_ || !mux_->impl_)return;auto& current=mux_->impl_->slots[slot_].current;
    if(current.present)current.validated=true;
}
Status UdpMuxPort::poll() noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    auto& s=*mux_->impl_;if(!s.slots[slot_].count)return s.poll();return {};
}
Status UdpMuxPort::reset() noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    mux_->impl_->clear_paths(mux_->impl_->slots[slot_]);return {};
}
Result<IpEndpoint> UdpMuxPort::current_endpoint() const noexcept {
    if(!mux_ || !mux_->impl_)return fail(Error::NotReady);
    const auto& current=mux_->impl_->slots[slot_].current;if(!current.present)return fail(Error::NotReady);return current.endpoint;
}

Result<RoutedUdpSocket> RoutedUdpSocket::open(const IpEndpoint& local,const IpEndpoint& remote,std::uint64_t connection) noexcept {
    if(!connection)return fail(Error::InvalidArgument);
    auto socket=UdpSocket::open(local,remote);if(!socket)return fail(socket.error());
    RoutedUdpSocket result;result.socket_=std::move(*socket);result.remote_=remote;result.connection_id_=connection;return result;
}
Result<std::size_t> RoutedUdpSocket::send(std::span<const std::byte> bytes) noexcept {
    if(!connection_id_)return fail(Error::NotReady);
    if(bytes.empty())return fail(Error::InvalidArgument);
    if(bytes.size()>payload_ceiling)return fail(Error::CapacityExceeded);
    std::array<std::byte,1200> datagram{};datagram[0]=UdpMux::routing_tag;
    Writer writer(std::span<std::byte>(datagram).subspan(1,8));if(!writer.u64(connection_id_))return fail(Error::ProtocolViolation);
    std::memcpy(datagram.data()+UdpMux::routing_bytes,bytes.data(),bytes.size());
    auto sent=socket_.send(std::span<const std::byte>(datagram).first(UdpMux::routing_bytes+bytes.size()));
    if(!sent)return fail(sent.error());
    return bytes.size();
}
Result<std::size_t> RoutedUdpSocket::receive(std::span<std::byte> output) noexcept {
    if(!connection_id_)return fail(Error::NotReady);return socket_.receive(output);
}
Status RoutedUdpSocket::rebind(const IpEndpoint& local) noexcept {
    if(!connection_id_)return fail(Error::NotReady);
    auto socket=UdpSocket::open(local,remote_);if(!socket)return fail(socket.error());
    socket_=std::move(*socket);return {};
}
Result<IpEndpoint> RoutedUdpSocket::local_endpoint() const noexcept {
    if(!connection_id_)return fail(Error::NotReady);return socket_.local_endpoint();
}
}
