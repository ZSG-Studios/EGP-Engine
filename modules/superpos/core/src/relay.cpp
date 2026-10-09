// SPDX-License-Identifier: MIT
#include "superpos/relay.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace superpos::relay {
namespace {
struct Busy {bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};
template<std::size_t N>struct Scratch {std::array<std::byte,N> bytes{};~Scratch(){wipe(bytes.data(),N);}};
using Address=std::array<std::byte,24>;
bool canonical(const IpEndpoint& endpoint,Address& out) noexcept {
    sockaddr_storage address{};
    if(endpoint.length<sizeof(address.ss_family)||endpoint.length>sizeof(address)||endpoint.length>endpoint.address.size())return false;
    std::memcpy(&address,endpoint.address.data(),endpoint.length);out={};
    if(address.ss_family==AF_INET&&endpoint.length==sizeof(sockaddr_in)){
        const auto* v=reinterpret_cast<const sockaddr_in*>(&address);out[0]=std::byte{4};
        std::memcpy(out.data()+1,&v->sin_addr,4);std::memcpy(out.data()+17,&v->sin_port,2);return true;
    }
    if(address.ss_family==AF_INET6&&endpoint.length==sizeof(sockaddr_in6)){
        const auto* v=reinterpret_cast<const sockaddr_in6*>(&address);out[0]=std::byte{6};
        std::memcpy(out.data()+1,&v->sin6_addr,16);std::memcpy(out.data()+17,&v->sin6_port,2);
        auto n=v->sin6_scope_id;for(unsigned i=0;i<4;++i)out[19+i]=std::byte(n>>(24-8*i));return true;
    }return false;
}
bool alive(const ClockObservation& now,std::uint64_t expiry) noexcept {return now.now_us<expiry&&now.uncertainty_us<expiry-now.now_us;}
bool valid(const PrincipalScope& scope) noexcept {return scope.match&&scope.session&&scope.authority_epoch&&scope.actor&&scope.permissions;}
void increment(std::uint64_t& n) noexcept {if(n!=UINT64_MAX)++n;}
bool reserve_sequence(std::uint64_t& next,std::uint64_t& out) noexcept {if(!next||next==UINT64_MAX)return false;out=next++;return true;}
bool operational(Error error) noexcept {return error==Error::OutOfMemory;}
std::array<std::byte,120> cookie_message(const Header& h,const Address& address,std::span<const std::byte,32> nonce,std::uint64_t expiry) noexcept {
    std::array<std::byte,120> bytes{};
    constexpr char label[]="SuperposRelayCookieV1";std::memcpy(bytes.data(),label,sizeof(label)-1);
    std::copy(h.route.begin(),h.route.end(),bytes.begin()+24);put64(bytes.data()+40,h.generation);bytes[48]=std::byte(h.side);
    std::copy(address.begin(),address.end(),bytes.begin()+49);std::copy(nonce.begin(),nonce.end(),bytes.begin()+73);put64(bytes.data()+105,expiry);put32(bytes.data()+113,h.slot);
    return bytes;
}
}
struct RelayServer::Route {
    struct Side {ReplayWindow receive;std::uint64_t next{1},revision{};IpEndpoint endpoint{};Address address{};bool bound{};};
    RoutePermit permit{};std::array<Side,2> sides{};std::uint64_t highwater{},window_us{};std::uint32_t bytes{},packets{},handshakes{};bool active{};
};
struct RelayServer::Frame {
    std::array<std::byte,maximum_datagram> bytes{};IpEndpoint target{};
    std::size_t size{},route{};std::uint64_t generation{},revision{};std::uint8_t side{};bool used{};
};
RelayServer::RelayServer(Allocator& a,UdpListener& s,MacProvider& c,GuardedTime& t,Limits l) noexcept:allocator_(a),socket_(s),crypto_(c),clock_(t),limits_(l){}
Status RelayServer::entry() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);return {};}
bool RelayServer::aliases(const void* p,std::size_t n) const noexcept {
    return overlap(p,n,this,sizeof(*this))||(routes_&&overlap(p,n,routes_,sizeof(Route)*limits_.routes))||
        (frames_&&overlap(p,n,frames_,sizeof(Frame)*limits_.queued_datagrams))||overlap(p,n,&socket_,sizeof(socket_))||
        overlap(p,n,&crypto_,sizeof(crypto_))||overlap(p,n,&allocator_,sizeof(allocator_))||overlap(p,n,&clock_,sizeof(clock_));
}
RelayServer::~RelayServer(){
    if(!entry())std::abort();Busy guard(busy_);
    wipe(cookie_key_.data(),cookie_key_.size());
    if(frames_){auto* p=std::exchange(frames_,nullptr);wipe(p,sizeof(Frame)*limits_.queued_datagrams);allocator_.deallocate(p);}
    if(routes_){auto* p=std::exchange(routes_,nullptr);wipe(p,sizeof(Route)*limits_.routes);allocator_.deallocate(p);}
}
Status RelayServer::reserve() noexcept {
    if(auto e=entry();!e)return e;Busy guard(busy_);
    if(routes_||frames_||ready_)return fail(Error::Busy);
    if(!limits_.routes||limits_.routes>1024||!limits_.queued_datagrams||limits_.queued_datagrams>1024||!limits_.per_side_queue||limits_.per_side_queue>limits_.queued_datagrams||
       !limits_.packets_per_advance||limits_.packets_per_advance>64||!limits_.global_bytes_per_second||!limits_.global_handshakes_per_second)return fail(Error::InvalidArgument);
    auto* rp=allocator_.allocate(sizeof(Route)*limits_.routes,alignof(Route),MemoryDomain::Backend);if(!rp)return fail(Error::OutOfMemory);
    if(!valid_range(rp,sizeof(Route)*limits_.routes)||reinterpret_cast<std::uintptr_t>(rp)%alignof(Route)||aliases(rp,sizeof(Route)*limits_.routes)){allocator_.deallocate(rp);return fail(Error::InvalidArgument);}
    routes_=static_cast<Route*>(rp);for(std::size_t i=0;i<limits_.routes;++i)std::construct_at(routes_+i);
    auto* fp=allocator_.allocate(sizeof(Frame)*limits_.queued_datagrams,alignof(Frame),MemoryDomain::Backend);
    if(!fp){wipe(routes_,sizeof(Route)*limits_.routes);allocator_.deallocate(routes_);routes_=nullptr;return fail(Error::OutOfMemory);}
    if(!valid_range(fp,sizeof(Frame)*limits_.queued_datagrams)||reinterpret_cast<std::uintptr_t>(fp)%alignof(Frame)||aliases(fp,sizeof(Frame)*limits_.queued_datagrams)){allocator_.deallocate(fp);wipe(routes_,sizeof(Route)*limits_.routes);allocator_.deallocate(routes_);routes_=nullptr;return fail(Error::InvalidArgument);}
    frames_=static_cast<Frame*>(fp);for(std::size_t i=0;i<limits_.queued_datagrams;++i)std::construct_at(frames_+i);
    auto generated=crypto_.random(cookie_key_);if(!generated||!nonzero(cookie_key_))return generated?Status(fail(Error::AuthenticationFailed)):generated;
    statistics_.owned_bytes=sizeof(Route)*limits_.routes+sizeof(Frame)*limits_.queued_datagrams;ready_=true;return {};
}
void RelayServer::retire(std::size_t index,bool expired) noexcept {
    auto& route=routes_[index];auto highwater=route.highwater;
    for(std::size_t i=0;i<limits_.queued_datagrams;++i)if(frames_[i].used&&frames_[i].route==index){statistics_.queued_bytes-=frames_[i].size;wipe(frames_+i,sizeof(Frame));}
    wipe(&route,sizeof(route));std::construct_at(&route);route.highwater=highwater;
    if(expired)increment(statistics_.expired);else increment(statistics_.revoked);
}
Status RelayServer::install(std::size_t index,const RoutePermit& borrowed) noexcept {
    if(auto e=entry();!e)return e;if(!valid_range(&borrowed,sizeof(borrowed))||aliases(&borrowed,sizeof(borrowed)))return fail(Error::InvalidArgument);
    RoutePermit permit=borrowed;Busy guard(busy_);struct Cleanup{RoutePermit& p;~Cleanup(){wipe(&p,sizeof(p));}}cleanup{permit};
    if(!ready_)return fail(Error::NotReady);if(index>=limits_.routes||permit.slot!=index||!nonzero(permit.id)||!permit.generation||!permit.bytes_per_second||!permit.packets_per_second||!permit.handshakes_per_second)return fail(Error::InvalidArgument);
    if(permit.generation<=routes_[index].highwater)return fail(Error::StaleGeneration);
    for(const auto& side:permit.sides)if(!valid(side.scope)||!nonzero(side.upstream)||!nonzero(side.downstream))return fail(Error::InvalidArgument);
    if(permit.sides[0].scope.actor==permit.sides[1].scope.actor||permit.sides[0].scope.match!=permit.sides[1].scope.match||permit.sides[0].scope.session!=permit.sides[1].scope.session||permit.sides[0].scope.authority_epoch!=permit.sides[1].scope.authority_epoch)return fail(Error::InvalidArgument);
    const Key* keys[]{&permit.sides[0].upstream,&permit.sides[0].downstream,&permit.sides[1].upstream,&permit.sides[1].downstream};
    for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b)if(*keys[a]==*keys[b])return fail(Error::InvalidArgument);
    for(std::size_t i=0;i<limits_.routes;++i)if(i!=index&&routes_[i].active&&routes_[i].permit.id==permit.id)return fail(Error::Busy);
    auto now=clock_.now();if(!now)return fail(now.error());if(!alive(*now,permit.expires_us))return fail(Error::Timeout);
    if(routes_[index].active)retire(index,false);auto& route=routes_[index];route.permit=permit;route.highwater=permit.generation;route.active=true;return {};
}
Status RelayServer::revoke(std::size_t index,std::uint64_t generation) noexcept {
    if(auto e=entry();!e)return e;Busy guard(busy_);if(!ready_)return fail(Error::NotReady);
    if(index>=limits_.routes)return fail(Error::InvalidArgument);if(!routes_[index].active||routes_[index].permit.generation!=generation)return fail(Error::StaleGeneration);retire(index,false);return {};
}
Status RelayServer::enqueue(std::size_t index,std::uint8_t side,Kind kind,std::span<const std::byte> payload,const IpEndpoint& target,std::uint64_t revision,std::uint64_t now) noexcept {
    auto& route=routes_[index];std::size_t free=limits_.queued_datagrams,count=0;
    for(std::size_t i=0;i<limits_.queued_datagrams;++i){if(!frames_[i].used&&free==limits_.queued_datagrams)free=i;if(frames_[i].used&&frames_[i].route==index&&frames_[i].side==side)++count;}
    const auto bytes=header_bytes+payload.size()+tag_bytes;
    if(free==limits_.queued_datagrams||count>=limits_.per_side_queue||bytes>route.permit.bytes_per_second-route.bytes||bytes>limits_.global_bytes_per_second-window_bytes_)return fail(Error::CapacityExceeded);
    std::uint64_t sequence;if(!reserve_sequence(route.sides[side].next,sequence)){retire(index,true);return fail(Error::CounterExhausted);}
    auto& frame=frames_[free];auto encoded=encode({kind,side,route.permit.id,route.permit.generation,sequence,route.permit.slot},payload,frame.bytes);if(!encoded)return fail(encoded.error());
    auto signed_tag=crypto_.sign(route.permit.sides[side].downstream,std::span(frame.bytes).first(*encoded-tag_bytes),std::span<std::byte,32>(frame.bytes.data()+*encoded-tag_bytes,tag_bytes));
    if(!signed_tag){wipe(&frame,sizeof(frame));return signed_tag;}
    auto fresh=clock_.now();if(!fresh||!alive(*fresh,route.permit.expires_us)){
        wipe(&frame,sizeof(frame));retire(index,true);return fail(fresh?Error::Timeout:fresh.error());
    }
    if(fresh->now_us-route.window_us>=1000000){route.window_us=fresh->now_us;route.bytes=route.packets=route.handshakes=0;}
    if(fresh->now_us-window_us_>=1000000){window_us_=fresh->now_us;window_bytes_=window_handshakes_=0;}
    if(bytes>route.permit.bytes_per_second-route.bytes||bytes>limits_.global_bytes_per_second-window_bytes_){wipe(&frame,sizeof(frame));return fail(Error::CapacityExceeded);}
    frame.size=*encoded;frame.route=index;frame.generation=route.permit.generation;frame.side=side;frame.revision=revision;frame.target=target;frame.used=true;
    route.bytes+=static_cast<std::uint32_t>(bytes);window_bytes_+=static_cast<std::uint32_t>(bytes);statistics_.queued_bytes+=bytes;(void)now;return {};
}
Status RelayServer::process(const ReceivedDatagram& received,std::span<const std::byte> bytes,const ClockObservation& observed) noexcept {
    auto parsed=decode(bytes);if(!parsed)return fail(parsed.error());const auto& packet=*parsed;
    const std::size_t index=packet.header.slot;if(index>=limits_.routes)return fail(Error::PermissionDenied);auto& route=routes_[index];
    if(!route.active||route.permit.id!=packet.header.route||route.permit.generation!=packet.header.generation)return fail(Error::PermissionDenied);auto& side=route.sides[packet.header.side];
    auto now=observed;
    auto refresh=[&]() noexcept -> Status {auto fresh=clock_.now();if(!fresh||!alive(*fresh,route.permit.expires_us)){retire(index,true);return fail(fresh?Error::Timeout:fresh.error());}now=*fresh;return {};};
    if(packet.header.kind!=Kind::Hello&&packet.header.kind!=Kind::Prove&&packet.header.kind!=Kind::Data)return fail(Error::ProtocolViolation);
    if(!alive(now,route.permit.expires_us)){retire(index,true);return fail(Error::Timeout);}
    if(!side.receive.accepts(packet.header.sequence))return fail(Error::StaleGeneration);
    auto verified=crypto_.verify(route.permit.sides[packet.header.side].upstream,packet.authenticated,std::span<const std::byte,32>(packet.tag.data(),32));if(!verified)return verified;
    if(auto current=refresh();!current)return current;
    Address source{};if(!canonical(received.source,source))return fail(Error::ProtocolViolation);
    if(packet.header.kind==Kind::Hello&&(!nonzero(packet.payload.first(32))||nonzero(packet.payload.subspan(32))))return fail(Error::ProtocolViolation);
    if(packet.header.kind==Kind::Prove){
        auto expiry=get64(packet.payload.data()+32);if(!alive(now,expiry)||expiry>route.permit.expires_us)return fail(Error::Timeout);
        auto message=cookie_message(packet.header,source,std::span<const std::byte,32>(packet.payload.data(),32),expiry);
        auto valid_cookie=crypto_.verify(cookie_key_,message,std::span<const std::byte,32>(packet.payload.data()+40,32));if(!valid_cookie)return valid_cookie;
        if(auto current=refresh();!current)return current;if(!alive(now,expiry))return fail(Error::Timeout);
    }
    if(now.now_us-route.window_us>=1000000){route.window_us=now.now_us;route.bytes=route.packets=route.handshakes=0;}
    if(bytes.size()>route.permit.bytes_per_second-route.bytes||route.packets>=route.permit.packets_per_second||bytes.size()>limits_.global_bytes_per_second-window_bytes_)return fail(Error::CapacityExceeded);
    if(packet.header.kind==Kind::Data&&(!side.bound||side.address!=source))return fail(Error::PermissionDenied);
    if(packet.header.kind!=Kind::Data&&(route.handshakes>=route.permit.handshakes_per_second||window_handshakes_>=limits_.global_handshakes_per_second))return fail(Error::CapacityExceeded);
    // Authenticated sequence is consumed even if a forwarding queue is full.
    side.receive.consume(packet.header.sequence);route.bytes+=static_cast<std::uint32_t>(bytes.size());window_bytes_+=static_cast<std::uint32_t>(bytes.size());++route.packets;
    if(packet.header.sequence==UINT64_MAX){retire(index,true);return fail(Error::CounterExhausted);}
    if(packet.header.kind==Kind::Data){auto target=std::uint8_t(1-packet.header.side);if(!route.sides[target].bound)return fail(Error::NotReady);
        auto result=enqueue(index,target,Kind::Data,packet.payload,route.sides[target].endpoint,route.sides[target].revision,now.now_us);if(result)increment(statistics_.accepted);return result;}
    ++route.handshakes;++window_handshakes_;Scratch<handshake_payload> payload;
    if(packet.header.kind==Kind::Hello){
        if(now.now_us>UINT64_MAX-500000)return fail(Error::CounterExhausted);auto expiry=std::min(route.permit.expires_us,now.now_us+500000);
        std::copy_n(packet.payload.begin(),32,payload.bytes.begin());put64(payload.bytes.data()+32,expiry);
        auto message=cookie_message(packet.header,source,std::span<const std::byte,32>(packet.payload.data(),32),expiry);
        auto signed_cookie=crypto_.sign(cookie_key_,message,std::span<std::byte,32>(payload.bytes.data()+40,32));if(!signed_cookie)return signed_cookie;
        if(auto current=refresh();!current)return current;if(!alive(now,expiry))return fail(Error::Timeout);
        return enqueue(index,packet.header.side,Kind::Challenge,payload.bytes,received.source,0,now.now_us);
    }
    if(!side.bound||side.address!=source){if(side.revision==UINT64_MAX){retire(index,true);return fail(Error::CounterExhausted);}++side.revision;side.address=source;side.endpoint=received.source;side.bound=true;}
    std::copy_n(packet.payload.begin(),32,payload.bytes.begin());put64(payload.bytes.data()+32,side.revision);
    return enqueue(index,packet.header.side,Kind::Ack,payload.bytes,received.source,side.revision,now.now_us);
}
Status RelayServer::advance() noexcept {
    if(auto e=entry();!e)return e;Busy guard(busy_);if(!ready_)return fail(Error::NotReady);
    auto now=clock_.now();if(!now){for(std::size_t i=0;i<limits_.routes;++i)if(routes_[i].active)retire(i,true);return fail(now.error());}
    auto refresh=[&]() noexcept -> Status {
        now=clock_.now();if(!now){for(std::size_t i=0;i<limits_.routes;++i)if(routes_[i].active)retire(i,true);return fail(now.error());}
        if(now->now_us-window_us_>=1000000){window_us_=now->now_us;window_bytes_=window_handshakes_=0;}
        for(std::size_t i=0;i<limits_.routes;++i)if(routes_[i].active&&!alive(*now,routes_[i].permit.expires_us))retire(i,true);return {};
    };
    if(auto current=refresh();!current)return current;
    Scratch<maximum_datagram> input;
    for(std::uint32_t i=0;i<limits_.packets_per_advance;++i){auto received=socket_.receive_from(input.bytes);if(auto current=refresh();!current)return current;if(!received){if(received.error()==Error::Busy)break;if(received.error()==Error::CapacityExceeded){increment(statistics_.refused);continue;}return fail(received.error());}
        auto result=process(*received,std::span(input.bytes).first(received->bytes),*now);if(!result){increment(statistics_.refused);if(operational(result.error()))return result;}}
    for(std::size_t i=0;i<limits_.queued_datagrams;++i){auto& frame=frames_[i];if(!frame.used)continue;if(auto current=refresh();!current)return current;if(!frame.used)continue;auto& route=routes_[frame.route];
        if(!route.active||route.permit.generation!=frame.generation||(frame.revision&&(!route.sides[frame.side].bound||route.sides[frame.side].revision!=frame.revision))){statistics_.queued_bytes-=frame.size;wipe(&frame,sizeof(frame));continue;}
        auto sent=socket_.send_to(frame.target,std::span(frame.bytes).first(frame.size));if(!sent&&sent.error()==Error::Busy)break;
        if(sent&&*sent==frame.size)increment(statistics_.forwarded);else increment(statistics_.refused);statistics_.queued_bytes-=frame.size;wipe(&frame,sizeof(frame));if(!sent)return fail(sent.error());}
    return {};
}
Result<Statistics> RelayServer::statistics() const noexcept {if(auto e=entry();!e)return fail(e.error());if(!ready_)return fail(Error::NotReady);return statistics_;}
}
