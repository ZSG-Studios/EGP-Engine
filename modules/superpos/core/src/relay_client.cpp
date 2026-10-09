// SPDX-License-Identifier: MIT
#include "superpos/relay.hpp"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace superpos::relay {
namespace {
struct Busy {bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};
template<std::size_t N>struct Scratch {std::array<std::byte,N> bytes{};~Scratch(){wipe(bytes.data(),N);}};
bool alive(const ClockObservation& now,std::uint64_t expiry) noexcept {return now.now_us<expiry&&now.uncertainty_us<expiry-now.now_us;}
}
struct RelayClient::Storage {
    RouteId route{};SidePermit permit{};std::uint64_t generation{},expires{},next{1},started{},retry_at{},revision{};
    ReplayWindow received;std::uint32_t slot{};std::uint8_t side{},phase{};bool proof_sent{};
    std::array<std::byte,32> nonce{};std::array<std::byte,handshake_payload> proof{};
    std::array<std::byte,maximum_inner> pending{};std::size_t pending_size{};
    std::array<std::byte,maximum_datagram> wire{};std::size_t wire_size{};Kind wire_kind{};
    std::array<std::array<std::byte,maximum_inner>,2> inbox{};std::array<std::size_t,2> inbox_size{};std::size_t inbox_count{};
};
RelayClient::RelayClient(Allocator& a,DatagramIO& s,MacProvider& c,GuardedTime& t) noexcept:allocator_(a),socket_(&s),crypto_(c),clock_(t){}
Status RelayClient::entry() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);return {};}
bool RelayClient::aliases(const void* p,std::size_t n) const noexcept {
    return overlap(p,n,this,sizeof(*this))||(storage_&&overlap(p,n,storage_,sizeof(Storage)))||overlap(p,n,socket_,sizeof(*socket_))||
        overlap(p,n,&crypto_,sizeof(crypto_))||overlap(p,n,&allocator_,sizeof(allocator_))||overlap(p,n,&clock_,sizeof(clock_));
}
RelayClient::~RelayClient(){if(!entry())std::abort();Busy guard(busy_);if(storage_){auto* p=std::exchange(storage_,nullptr);wipe(p,sizeof(Storage));allocator_.deallocate(p);}}
Status RelayClient::reserve() noexcept {
    if(auto e=entry();!e)return e;Busy guard(busy_);if(storage_)return fail(Error::Busy);
    auto* memory=allocator_.allocate(sizeof(Storage),alignof(Storage),MemoryDomain::Backend);if(!memory)return fail(Error::OutOfMemory);
    if(!valid_range(memory,sizeof(Storage))||reinterpret_cast<std::uintptr_t>(memory)%alignof(Storage)||aliases(memory,sizeof(Storage))){allocator_.deallocate(memory);return fail(Error::InvalidArgument);}
    storage_=::new(memory)Storage;state_=ClientState::Idle;return {};
}
void RelayClient::fail_closed() noexcept {if(storage_)wipe(storage_,sizeof(Storage));state_=ClientState::Failed;}
Status RelayClient::configure(const RouteId& id,std::uint64_t generation,std::uint32_t slot,std::uint8_t side,const SidePermit& borrowed,std::uint64_t expires) noexcept {
    if(auto e=entry();!e)return e;
    if(!valid_range(&id,sizeof(id))||!valid_range(&borrowed,sizeof(borrowed))||aliases(&id,sizeof(id))||aliases(&borrowed,sizeof(borrowed)))return fail(Error::InvalidArgument);
    const auto route=id;SidePermit permit=borrowed;struct Cleanup{SidePermit& p;~Cleanup(){wipe(&p,sizeof(p));}}cleanup{permit};Busy guard(busy_);
    if(!storage_||state_!=ClientState::Idle)return fail(Error::NotReady);
    if(!nonzero(route)||!generation||slot>=maximum_routes||side>1||!nonzero(permit.upstream)||!nonzero(permit.downstream)||permit.upstream==permit.downstream||
       !permit.scope.match||!permit.scope.session||!permit.scope.authority_epoch||!permit.scope.actor||!permit.scope.permissions)return fail(Error::InvalidArgument);
    auto now=clock_.now();if(!now)return fail(now.error());if(!alive(*now,expires))return fail(Error::Timeout);
    auto& s=*storage_;s.route=route;s.generation=generation;s.slot=slot;s.side=side;s.permit=permit;s.expires=expires;s.started=now->now_us;
    auto random=crypto_.random(s.nonce);if(!random||!nonzero(s.nonce)){fail_closed();return random?Status(fail(Error::AuthenticationFailed)):random;}
    auto fresh=clock_.now();if(!fresh||!alive(*fresh,s.expires)){fail_closed();return fail(fresh?Error::Timeout:fresh.error());}
    state_=ClientState::Handshaking;return {};
}
Status RelayClient::rebind(DatagramIO& socket) noexcept {
    if(auto e=entry();!e)return e;
    if(overlap(&socket,sizeof(socket),this,sizeof(*this))||(storage_&&overlap(&socket,sizeof(socket),storage_,sizeof(Storage))))return fail(Error::InvalidArgument);
    Busy guard(busy_);if(!storage_||state_!=ClientState::Ready)return fail(Error::NotReady);
    auto now=clock_.now();if(!now)return fail(now.error());if(!alive(*now,storage_->expires)){fail_closed();return fail(Error::Timeout);}
    Scratch<32> nonce;auto generated=crypto_.random(nonce.bytes);if(!generated||!nonzero(nonce.bytes))return generated?Status(fail(Error::AuthenticationFailed)):generated;
    auto fresh=clock_.now();if(!fresh||!alive(*fresh,storage_->expires)){fail_closed();return fail(fresh?Error::Timeout:fresh.error());}
    auto& s=*storage_;s.nonce=nonce.bytes;s.phase=0;s.proof_sent=false;s.started=now->now_us;s.retry_at=0;
    wipe(s.pending.data(),s.pending.size());s.pending_size=0;wipe(s.wire.data(),s.wire.size());s.wire_size=0;
    for(auto& bytes:s.inbox)wipe(bytes.data(),bytes.size());s.inbox_size={};s.inbox_count=0;
    socket_=&socket;state_=ClientState::Handshaking;return {};
}
Status RelayClient::send_control(Kind kind,const ClockObservation& now) noexcept {
    auto& s=*storage_;if(s.wire_size)return fail(Error::Busy);if(!s.next||s.next==UINT64_MAX){fail_closed();return fail(Error::CounterExhausted);}
    Scratch<handshake_payload> payload;
    if(kind==Kind::Hello)std::copy(s.nonce.begin(),s.nonce.end(),payload.bytes.begin());else payload.bytes=s.proof;
    auto encoded=encode({kind,s.side,s.route,s.generation,s.next++,s.slot},payload.bytes,s.wire);if(!encoded)return fail(encoded.error());
    auto tag=crypto_.sign(s.permit.upstream,std::span(s.wire).first(*encoded-tag_bytes),std::span<std::byte,32>(s.wire.data()+*encoded-tag_bytes,32));
    auto fresh=clock_.now();if(!fresh||!alive(*fresh,s.expires)||fresh->now_us-s.started>=5000000){fail_closed();return fail(fresh?Error::Timeout:fresh.error());}
    if(!tag){wipe(s.wire.data(),s.wire.size());return tag;}s.wire_size=*encoded;s.wire_kind=kind;(void)now;return {};
}
Status RelayClient::advance() noexcept {
    if(auto e=entry();!e)return e;Busy guard(busy_);
    if(!storage_||(state_!=ClientState::Handshaking&&state_!=ClientState::Ready))return fail(Error::NotReady);
    auto now=clock_.now();if(!now){fail_closed();return fail(now.error());}
    auto& s=*storage_;if(!alive(*now,s.expires)||(state_==ClientState::Handshaking&&now->now_us-s.started>=5000000)){fail_closed();return fail(Error::Timeout);}
    auto refresh=[&]() noexcept -> Status {
        now=clock_.now();if(!now||!alive(*now,s.expires)||(state_==ClientState::Handshaking&&now->now_us-s.started>=5000000)){fail_closed();return fail(now?Error::Timeout:now.error());}return {};
    };
    Scratch<maximum_datagram> input;
    for(unsigned count=0;count<8;++count){
        auto size=socket_->receive(input.bytes);if(auto current=refresh();!current)return current;if(!size){if(size.error()==Error::Busy)break;if(size.error()==Error::CapacityExceeded)continue;fail_closed();return fail(size.error());}
        if(*size>input.bytes.size()){fail_closed();return fail(Error::ProtocolViolation);}
        auto parsed=decode(std::span(input.bytes).first(*size));if(!parsed)continue;const auto& p=*parsed;
        if(p.header.route!=s.route||p.header.generation!=s.generation||p.header.slot!=s.slot||p.header.side!=s.side||!s.received.accepts(p.header.sequence)||
           (p.header.kind!=Kind::Challenge&&p.header.kind!=Kind::Ack&&p.header.kind!=Kind::Data))continue;
        auto valid=crypto_.verify(s.permit.downstream,p.authenticated,std::span<const std::byte,32>(p.tag.data(),32));if(auto current=refresh();!current)return current;if(!valid){if(valid.error()==Error::OutOfMemory)return valid;continue;}
        if(p.header.sequence==UINT64_MAX){fail_closed();return fail(Error::CounterExhausted);}
        if(p.header.kind==Kind::Data){
            if(state_!=ClientState::Ready)continue;s.received.consume(p.header.sequence);
            if(s.inbox_count==s.inbox.size())continue;std::copy(p.payload.begin(),p.payload.end(),s.inbox[s.inbox_count].begin());s.inbox_size[s.inbox_count++]=p.payload.size();continue;
        }
        if(state_!=ClientState::Handshaking||!std::equal(s.nonce.begin(),s.nonce.end(),p.payload.begin()))continue;
        if(p.header.kind==Kind::Challenge){
            if(!get64(p.payload.data()+32))continue;
            // Cookie expiry uses the server's local clock. The client only echoes it.
            s.received.consume(p.header.sequence);std::copy(p.payload.begin(),p.payload.end(),s.proof.begin());s.phase=1;s.retry_at=0;
            if(s.wire_size&&s.wire_kind==Kind::Hello){wipe(s.wire.data(),s.wire.size());s.wire_size=0;}
        }else{
            const auto revision=get64(p.payload.data()+32);if(!s.proof_sent||!revision||nonzero(p.payload.subspan(40)))continue;
            s.received.consume(p.header.sequence);s.revision=revision;state_=ClientState::Ready;
            wipe(s.proof.data(),s.proof.size());wipe(s.wire.data(),s.wire.size());s.wire_size=0;
        }
    }
    if(!s.wire_size&&state_==ClientState::Handshaking&&now->now_us>=s.retry_at){auto built=send_control(s.phase?Kind::Prove:Kind::Hello,*now);if(!built)return built;}
    if(!s.wire_size&&state_==ClientState::Ready&&s.pending_size){
        if(!s.next||s.next==UINT64_MAX){fail_closed();return fail(Error::CounterExhausted);}
        auto encoded=encode({Kind::Data,s.side,s.route,s.generation,s.next++,s.slot},std::span(s.pending).first(s.pending_size),s.wire);if(!encoded)return fail(encoded.error());
        auto tag=crypto_.sign(s.permit.upstream,std::span(s.wire).first(*encoded-tag_bytes),std::span<std::byte,32>(s.wire.data()+*encoded-tag_bytes,32));if(!tag){wipe(s.wire.data(),s.wire.size());return tag;}
        if(auto current=refresh();!current)return current;
        s.wire_size=*encoded;s.wire_kind=Kind::Data;wipe(s.pending.data(),s.pending.size());s.pending_size=0;
    }
    if(s.wire_size){
        if(auto current=refresh();!current)return current;
        auto sent=socket_->send(std::span(s.wire).first(s.wire_size));if(auto current=refresh();!current)return current;if(!sent){if(sent.error()==Error::Busy)return fail(Error::Busy);fail_closed();return fail(sent.error());}
        if(*sent!=s.wire_size){fail_closed();return fail(Error::Io);}
        if(s.wire_kind==Kind::Prove)s.proof_sent=true;
        if(s.wire_kind!=Kind::Data){if(now->now_us>UINT64_MAX-100000){fail_closed();return fail(Error::CounterExhausted);}s.retry_at=now->now_us+100000;}
        wipe(s.wire.data(),s.wire.size());s.wire_size=0;
    }return {};
}
Result<ClientState> RelayClient::state() const noexcept {if(auto e=entry();!e)return fail(e.error());return state_;}
Result<std::size_t> RelayClient::send(std::span<const std::byte> bytes) noexcept {
    if(auto e=entry();!e)return fail(e.error());if(!valid_range(bytes.data(),bytes.size())||aliases(bytes.data(),bytes.size()))return fail(Error::InvalidArgument);
    Busy guard(busy_);if(!storage_||state_!=ClientState::Ready)return fail(Error::NotReady);if(bytes.empty()||bytes.size()>maximum_inner)return fail(Error::CapacityExceeded);
    auto& s=*storage_;if(s.pending_size||s.wire_size)return fail(Error::Busy);std::copy(bytes.begin(),bytes.end(),s.pending.begin());s.pending_size=bytes.size();return bytes.size();
}
Result<std::size_t> RelayClient::receive(std::span<std::byte> bytes) noexcept {
    if(auto e=entry();!e)return fail(e.error());if(!valid_range(bytes.data(),bytes.size())||aliases(bytes.data(),bytes.size()))return fail(Error::InvalidArgument);
    Busy guard(busy_);if(!storage_||state_!=ClientState::Ready)return fail(Error::NotReady);auto& s=*storage_;if(!s.inbox_count)return fail(Error::Busy);
    auto count=s.inbox_size[0];const bool fits=count<=bytes.size();if(fits)std::copy_n(s.inbox[0].begin(),count,bytes.begin());
    wipe(s.inbox[0].data(),s.inbox[0].size());if(s.inbox_count==2){s.inbox[0]=s.inbox[1];s.inbox_size[0]=s.inbox_size[1];wipe(s.inbox[1].data(),s.inbox[1].size());}
    --s.inbox_count;s.inbox_size[s.inbox_count]=0;if(!fits)return fail(Error::CapacityExceeded);return count;
}
}
