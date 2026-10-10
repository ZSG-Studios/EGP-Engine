// SPDX-License-Identifier: MIT
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif
#include "superpos/service/tcp.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <utility>

namespace superpos::service::net {
namespace {
#if defined(_WIN32)
using NativeSocket=SOCKET;
using NativeLength=int;
constexpr NativeSocket invalid_socket=INVALID_SOCKET;
int socket_error() noexcept {return WSAGetLastError();}
bool pending(int e) noexcept {return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEALREADY;}
bool interrupted(int e) noexcept {return e==WSAEINTR;}
bool broken(int e) noexcept {return e==WSAECONNRESET||e==WSAECONNABORTED||e==WSAESHUTDOWN||e==WSAENOTCONN;}
bool close_native(NativeSocket s) noexcept {return ::closesocket(s)==0;}
#else
using NativeSocket=int;
using NativeLength=socklen_t;
constexpr NativeSocket invalid_socket=-1;
int socket_error() noexcept {return errno;}
bool pending(int e) noexcept {return e==EAGAIN||e==EWOULDBLOCK||e==EINPROGRESS||e==EALREADY;}
bool interrupted(int e) noexcept {return e==EINTR;}
bool broken(int e) noexcept {return e==ECONNRESET||e==ECONNABORTED||e==EPIPE||e==ENOTCONN;}
// Linux consumes ownership on close even on an error. Report the error but
// never let the caller retry the released descriptor after address reuse.
bool close_native(NativeSocket s) noexcept {return ::close(s)==0;}
#endif
NativeSocket native(std::uintptr_t value) noexcept {return static_cast<NativeSocket>(value);}
bool valid_config(SocketConfig c) noexcept {
    return c.send_quantum&&c.send_quantum<=INT_MAX&&c.receive_quantum&&c.receive_quantum<=INT_MAX&&c.buffer_hint>0&&c.buffer_hint<=8192&&
        (c.family==LoopbackFamily::IPv4||c.family==LoopbackFamily::IPv6);
}
Error io_error(int e) noexcept {return broken(e)?Error::ChannelFailed:Error::Io;}
bool overlap(const void* a,std::size_t n,const void* b,std::size_t m) noexcept {
    if(!n||!m)return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    if(n>UINTPTR_MAX-x||m>UINTPTR_MAX-y)return true;
    return x<y+m&&y<x+n;
}
struct OwnedSocket {
    NativeSocket value{invalid_socket};
    ~OwnedSocket(){if(value!=invalid_socket&&!close_native(value))std::abort();}
    NativeSocket release() noexcept {return std::exchange(value,invalid_socket);}
};
Status configure(NativeSocket s,SocketConfig config,SocketBuffers& measured) noexcept {
#if defined(_WIN32)
    if(!SetHandleInformation(reinterpret_cast<HANDLE>(s),HANDLE_FLAG_INHERIT,0))return fail(Error::Io);
    u_long nonblocking=1;
    if(::ioctlsocket(s,FIONBIO,&nonblocking)!=0)return fail(Error::Io);
#else
    const int flags=::fcntl(s,F_GETFL,0),descriptor=::fcntl(s,F_GETFD,0);
    if(flags<0||descriptor<0||::fcntl(s,F_SETFL,flags|O_NONBLOCK)<0||::fcntl(s,F_SETFD,descriptor|FD_CLOEXEC)<0)return fail(Error::Io);
#endif
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    const int suppress_signal=1;
    if(::setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&suppress_signal,sizeof(suppress_signal))!=0)return fail(Error::Io);
#endif
    const int hint=config.buffer_hint;
    const int no_delay=1;
    if(::setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&no_delay),sizeof(no_delay))!=0)return fail(Error::Io);
    if(::setsockopt(s,SOL_SOCKET,SO_SNDBUF,reinterpret_cast<const char*>(&hint),sizeof(hint))!=0||
       ::setsockopt(s,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&hint),sizeof(hint))!=0)return fail(Error::Io);
    NativeLength length=sizeof(measured.send);
    if(::getsockopt(s,SOL_SOCKET,SO_SNDBUF,reinterpret_cast<char*>(&measured.send),&length)!=0||length!=sizeof(measured.send)||measured.send<=0)return fail(Error::Io);
    length=sizeof(measured.receive);
    if(::getsockopt(s,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&measured.receive),&length)!=0||length!=sizeof(measured.receive)||measured.receive<=0)return fail(Error::Io);
    return {};
}
Result<NativeSocket> open_socket(LoopbackFamily family) noexcept {
    const int domain=family==LoopbackFamily::IPv6?AF_INET6:AF_INET;
#if defined(_WIN32)
    const auto s=::WSASocketW(domain,SOCK_STREAM,IPPROTO_TCP,nullptr,0,WSA_FLAG_NO_HANDLE_INHERIT);
#elif defined(__linux__)
    const auto s=::socket(domain,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,IPPROTO_TCP);
#else
    const auto s=::socket(domain,SOCK_STREAM,IPPROTO_TCP);
#endif
    if(s==invalid_socket)return fail(Error::Io);
    return s;
}
bool valid_endpoint(const TcpEndpoint& e) noexcept {
    if(e.family==AddressFamily::IPv4)return e.scope==0&&
        std::all_of(e.address.begin()+4,e.address.end(),[](std::byte b){return b==std::byte{};});
    return e.family==AddressFamily::IPv6;
}
struct Endpoint {
    sockaddr_in v4{};
    sockaddr_in6 v6{};
    AddressFamily family;
    NativeLength length{};
    explicit Endpoint(const TcpEndpoint& e) noexcept:family(e.family) {
        if(family==AddressFamily::IPv6){
            v6.sin6_family=AF_INET6;v6.sin6_port=htons(e.port);v6.sin6_scope_id=e.scope;
            std::memcpy(&v6.sin6_addr,e.address.data(),16);length=sizeof(v6);
        }else{
            v4.sin_family=AF_INET;v4.sin_port=htons(e.port);
            std::memcpy(&v4.sin_addr,e.address.data(),4);length=sizeof(v4);
        }
    }
    const sockaddr* data() const noexcept {return family==AddressFamily::IPv6?reinterpret_cast<const sockaddr*>(&v6):reinterpret_cast<const sockaddr*>(&v4);}
    sockaddr* data() noexcept {return family==AddressFamily::IPv6?reinterpret_cast<sockaddr*>(&v6):reinterpret_cast<sockaddr*>(&v4);}
    Result<TcpEndpoint> decoded(NativeLength received) const noexcept {
        TcpEndpoint out;out.family=family;
        if(family==AddressFamily::IPv6){
            if(received!=sizeof(v6)||v6.sin6_family!=AF_INET6)return fail(Error::Io);
            std::memcpy(out.address.data(),&v6.sin6_addr,16);out.port=ntohs(v6.sin6_port);out.scope=v6.sin6_scope_id;
        }else{
            if(received!=sizeof(v4)||v4.sin_family!=AF_INET)return fail(Error::Io);
            std::memcpy(out.address.data(),&v4.sin_addr,4);out.port=ntohs(v4.sin_port);
        }
        return out;
    }
};
Result<TcpEndpoint> socket_endpoint(NativeSocket socket,AddressFamily family,bool remote) noexcept {
    Endpoint address(TcpEndpoint{family});NativeLength length=address.length;
    const int result=remote?::getpeername(socket,address.data(),&length) : ::getsockname(socket,address.data(),&length);
    if(result!=0)return fail(Error::Io);
    auto decoded=address.decoded(length);if(!decoded||!decoded->port)return fail(Error::Io);return decoded;
}
}

Result<TcpEndpoint> TcpEndpoint::parse(std::string_view text,std::uint16_t port,std::uint32_t scope) noexcept {
    if(text.empty()||text.size()>=INET6_ADDRSTRLEN||text.find('\0')!=text.npos)return fail(Error::InvalidArgument);
    std::array<char,INET6_ADDRSTRLEN> terminated{};std::copy(text.begin(),text.end(),terminated.begin());
    TcpEndpoint out;out.port=port;
    if(::inet_pton(AF_INET,terminated.data(),out.address.data())==1){if(scope)return fail(Error::InvalidArgument);return out;}
    out.address.fill(std::byte{});out.family=AddressFamily::IPv6;out.scope=scope;
    if(::inet_pton(AF_INET6,terminated.data(),out.address.data())!=1)return fail(Error::InvalidArgument);
    return out;
}

Status Network::check() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);
    if(!ready_)return fail(Error::NotReady);
    return {};
}
Status Network::acquire() noexcept {if(auto s=check();!s)return s;if(borrowers_==SIZE_MAX)return fail(Error::CounterExhausted);++borrowers_;return {};}
void Network::release() noexcept {if(owner_!=std::this_thread::get_id()||!borrowers_)std::abort();--borrowers_;}
Status Network::initialize() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(ready_||busy_)return fail(Error::Busy);
    struct Active {bool& value;explicit Active(bool& b):value(b){value=true;}~Active(){value=false;}} active(busy_);
#if defined(_WIN32)
    WSADATA data{};
    if(::WSAStartup(MAKEWORD(2,2),&data)!=0)return fail(Error::Io);
    if(data.wVersion!=MAKEWORD(2,2)){if(::WSACleanup()!=0)std::abort();return fail(Error::Unsupported);}
#endif
    ready_=true;return {};
}
Network::~Network(){
    if(owner_!=std::this_thread::get_id()||borrowers_||busy_)std::abort();
    busy_=true;
#if defined(_WIN32)
    if(ready_&&::WSACleanup()!=0)std::abort();
#endif
}

SocketStream::SocketStream(Network& n,std::uintptr_t s,SocketConfig c,SocketBuffers b,SocketState state) noexcept:network_(&n),socket_(s),config_(c),buffers_(b),state_(state){}
Status SocketStream::check() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(socket_==invalid_||!network_)return fail(Error::NotReady);
    return network_->check();
}
SocketStream::SocketStream(SocketStream&& other) noexcept {
    if(other.owner_!=std::this_thread::get_id())std::abort();
    network_=std::exchange(other.network_,nullptr);socket_=std::exchange(other.socket_,invalid_);config_=other.config_;buffers_=other.buffers_;stats_=std::exchange(other.stats_,SocketStats{});state_=std::exchange(other.state_,SocketState::Closed);
}
SocketStream& SocketStream::operator=(SocketStream&& other) noexcept {
    if(owner_!=std::this_thread::get_id()||other.owner_!=std::this_thread::get_id())std::abort();
    if(this!=&other){destroy();network_=std::exchange(other.network_,nullptr);socket_=std::exchange(other.socket_,invalid_);config_=other.config_;buffers_=other.buffers_;stats_=std::exchange(other.stats_,SocketStats{});state_=std::exchange(other.state_,SocketState::Closed);}return *this;
}
Status SocketStream::close() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(socket_==invalid_)return {};
    const bool closed=close_native(native(socket_));
#if defined(_WIN32)
    if(!closed)return fail(Error::Io);
#endif
    socket_=invalid_;state_=SocketState::Closed;auto* network=std::exchange(network_,nullptr);network->release();
    return closed?Status{}:Status(fail(Error::Io));
}
void SocketStream::destroy() noexcept {if(!close())std::abort();}
SocketStream::~SocketStream(){destroy();}
Result<SocketStream> SocketStream::connect_loopback(Network& network,std::uint16_t port,SocketConfig config) noexcept {
    if(!valid_config(config))return fail(Error::InvalidArgument);
    auto endpoint=TcpEndpoint::parse(config.family==AddressFamily::IPv6?"::1":"127.0.0.1",port);
    if(!endpoint)return fail(endpoint.error());return connect(network,*endpoint,config);
}
Result<SocketStream> SocketStream::connect(Network& network,TcpEndpoint endpoint,SocketConfig config) noexcept {
    if(!valid_endpoint(endpoint)||!endpoint.port||!valid_config(config))return fail(Error::InvalidArgument);
    config.family=endpoint.family;
    auto acquired=network.acquire();if(!acquired)return fail(acquired.error());
    struct Borrow {Network& network;bool transferred{};~Borrow(){if(!transferred)network.release();}} borrow{network};
    auto opened=open_socket(config.family);if(!opened)return fail(opened.error());OwnedSocket owned{*opened};
    SocketBuffers measured{};auto configured=configure(owned.value,config,measured);if(!configured)return fail(configured.error());
    const Endpoint address(endpoint);SocketState state=SocketState::Connected;
    if(::connect(owned.value,address.data(),address.length)!=0){const int e=socket_error();if(!pending(e)&&!interrupted(e))return fail(io_error(e));state=SocketState::Connecting;}
    borrow.transferred=true;return SocketStream(network,static_cast<std::uintptr_t>(owned.release()),config,measured,state);
}
Result<TcpEndpoint> SocketStream::local_endpoint() const noexcept {
    if(auto checked=check();!checked)return fail(checked.error());return socket_endpoint(native(socket_),config_.family,false);
}
Result<TcpEndpoint> SocketStream::remote_endpoint() const noexcept {
    if(auto checked=check();!checked)return fail(checked.error());
    if(state_!=SocketState::Connected)return fail(Error::NotReady);return socket_endpoint(native(socket_),config_.family,true);
}
Status SocketStream::finish_connect() noexcept {
    if(auto s=check();!s)return s;
    if(state_==SocketState::Connected)return {};
    if(state_!=SocketState::Connecting)return fail(Error::ChannelFailed);
    const auto s=native(socket_);
#if !defined(_WIN32)
    if(s<0||s>=FD_SETSIZE){state_=SocketState::Failed;return fail(Error::CapacityExceeded);}
#endif
    fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(s,&writes);FD_SET(s,&errors);timeval timeout{};
    const int selected=::select(
#if defined(_WIN32)
        0,
#else
        s+1,
#endif
        nullptr,&writes,&errors,&timeout);
    if(!selected)return fail(Error::Busy);
    if(selected<0){const int e=socket_error();if(interrupted(e))return fail(Error::Busy);state_=SocketState::Failed;return fail(io_error(e));}
    int error=0;NativeLength length=sizeof(error);
    if(::getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)!=0||length!=sizeof(error)){state_=SocketState::Failed;return fail(Error::Io);}
    if(error){if(pending(error))return fail(Error::Busy);state_=SocketState::Failed;return fail(io_error(error));}
    if(!FD_ISSET(s,&writes)){state_=SocketState::Failed;return fail(Error::Io);}
    state_=SocketState::Connected;return {};
}
Result<std::size_t> SocketStream::send(std::span<const std::byte> bytes) noexcept {
    if(auto s=check();!s)return fail(s.error());
    if(state_!=SocketState::Connected)return fail(state_==SocketState::Connecting?Error::NotReady:Error::ChannelFailed);
    if(bytes.empty())return std::size_t{0};
    if(overlap(bytes.data(),bytes.size(),this,sizeof(*this))||overlap(bytes.data(),bytes.size(),network_,sizeof(Network)))return fail(Error::InvalidArgument);
    const auto count=std::min(bytes.size(),static_cast<std::size_t>(config_.send_quantum));
    // A call may either send positive bytes or report Busy. Reserve enough
    // counter range for either outcome before making an irreversible syscall.
    if(stats_.send_syscalls==UINT64_MAX||stats_.send_busy==UINT64_MAX||count>UINT64_MAX-stats_.sent){state_=SocketState::Failed;return fail(Error::CounterExhausted);}
    ++stats_.send_syscalls;
    const auto result=::send(native(socket_),reinterpret_cast<const char*>(bytes.data()),static_cast<int>(count),
#if defined(MSG_NOSIGNAL)
        MSG_NOSIGNAL
#else
        0
#endif
    );
    if(result<0){const int e=socket_error();if(pending(e)||interrupted(e)){++stats_.send_busy;return fail(Error::Busy);}state_=SocketState::Failed;return fail(io_error(e));}
    if(!result||static_cast<std::size_t>(result)>count){state_=SocketState::Failed;return fail(Error::Io);}
    stats_.sent+=static_cast<std::size_t>(result);
    return static_cast<std::size_t>(result);
}
Result<std::size_t> SocketStream::receive(std::span<std::byte> bytes) noexcept {
    if(auto s=check();!s)return fail(s.error());
    if(state_!=SocketState::Connected)return fail(state_==SocketState::Connecting?Error::NotReady:Error::ChannelFailed);
    if(bytes.empty())return fail(Error::InvalidArgument);
    if(overlap(bytes.data(),bytes.size(),this,sizeof(*this))||overlap(bytes.data(),bytes.size(),network_,sizeof(Network)))return fail(Error::InvalidArgument);
    const auto count=std::min(bytes.size(),static_cast<std::size_t>(config_.receive_quantum));
    if(stats_.receive_syscalls==UINT64_MAX||stats_.receive_busy==UINT64_MAX||count>UINT64_MAX-stats_.received){state_=SocketState::Failed;return fail(Error::CounterExhausted);}
    ++stats_.receive_syscalls;
    const auto result=::recv(native(socket_),reinterpret_cast<char*>(bytes.data()),static_cast<int>(count),0);
    if(result<0){const int e=socket_error();if(pending(e)||interrupted(e)){++stats_.receive_busy;return fail(Error::Busy);}state_=SocketState::Failed;return fail(io_error(e));}
    if(static_cast<std::size_t>(result)>count){state_=SocketState::Failed;return fail(Error::Io);}
    stats_.received+=static_cast<std::size_t>(result);
    return static_cast<std::size_t>(result); // Zero is stream EOF; TLS decides clean/unclean.
}
Result<SocketState> SocketStream::state() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);return state_;}
Result<SocketBuffers> SocketStream::buffers() const noexcept {if(auto s=check();!s)return fail(s.error());return buffers_;}
Result<SocketStats> SocketStream::stats() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);return stats_;}

Listener::Listener(Network& n,std::uintptr_t s,SocketConfig c,SocketBuffers b,std::uint16_t p) noexcept:network_(&n),socket_(s),config_(c),buffers_(b),port_(p){}
Status Listener::check() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(socket_==invalid_||!network_)return fail(Error::NotReady);return network_->check();}
Listener::Listener(Listener&& other) noexcept {
    if(other.owner_!=std::this_thread::get_id())std::abort();
    network_=std::exchange(other.network_,nullptr);socket_=std::exchange(other.socket_,invalid_);config_=other.config_;buffers_=other.buffers_;port_=std::exchange(other.port_,0);loopback_only_=std::exchange(other.loopback_only_,false);
}
Listener& Listener::operator=(Listener&& other) noexcept {
    if(owner_!=std::this_thread::get_id()||other.owner_!=std::this_thread::get_id())std::abort();
    if(this!=&other){destroy();network_=std::exchange(other.network_,nullptr);socket_=std::exchange(other.socket_,invalid_);config_=other.config_;buffers_=other.buffers_;port_=std::exchange(other.port_,0);loopback_only_=std::exchange(other.loopback_only_,false);}return *this;
}
Status Listener::close() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(socket_==invalid_)return {};
    const bool closed=close_native(native(socket_));
#if defined(_WIN32)
    if(!closed)return fail(Error::Io);
#endif
    socket_=invalid_;port_=0;auto* network=std::exchange(network_,nullptr);network->release();
    return closed?Status{}:Status(fail(Error::Io));
}
void Listener::destroy() noexcept {if(!close())std::abort();}
Listener::~Listener(){destroy();}
Result<Listener> Listener::bind_loopback(Network& network,SocketConfig config,std::uint32_t backlog) noexcept {
    if(!valid_config(config)||!backlog||backlog>8)return fail(Error::InvalidArgument);
    auto endpoint=TcpEndpoint::parse(config.family==AddressFamily::IPv6?"::1":"127.0.0.1",0);
    if(!endpoint)return fail(endpoint.error());
    auto listener=bind(network,*endpoint,config,backlog);if(!listener)return fail(listener.error());
    listener->loopback_only_=true;return listener;
}
Result<Listener> Listener::bind(Network& network,TcpEndpoint endpoint,SocketConfig config,std::uint32_t backlog) noexcept {
    if(!valid_endpoint(endpoint)||!valid_config(config)||!backlog||backlog>128)return fail(Error::InvalidArgument);
    config.family=endpoint.family;
    auto acquired=network.acquire();if(!acquired)return fail(acquired.error());
    struct Borrow {Network& network;bool transferred{};~Borrow(){if(!transferred)network.release();}} borrow{network};
    auto opened=open_socket(config.family);if(!opened)return fail(opened.error());OwnedSocket owned{*opened};SocketBuffers measured{};
    auto configured=configure(owned.value,config,measured);if(!configured)return fail(configured.error());
#if defined(_WIN32)
    const int exclusive=1;if(::setsockopt(owned.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0)return fail(Error::Io);
#endif
    if(config.family==LoopbackFamily::IPv6){const int only=1;if(::setsockopt(owned.value,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&only),sizeof(only))!=0)return fail(Error::Io);}
    Endpoint address(endpoint);
    if(::bind(owned.value,address.data(),address.length)!=0||::listen(owned.value,static_cast<int>(backlog))!=0)return fail(Error::Io);
    NativeLength length=address.length;
    if(::getsockname(owned.value,address.data(),&length)!=0)return fail(Error::Io);
    auto bound=address.decoded(length);
    if(!bound||!bound->port||bound->address!=endpoint.address||bound->scope!=endpoint.scope||
       (endpoint.port&&bound->port!=endpoint.port))return fail(Error::Io);
    borrow.transferred=true;return Listener(network,static_cast<std::uintptr_t>(owned.release()),config,measured,bound->port);
}
Result<SocketStream> Listener::accept() noexcept {
    if(auto s=check();!s)return fail(s.error());
    auto acquired=network_->acquire();if(!acquired)return fail(acquired.error());
    struct Borrow {Network& network;bool transferred{};~Borrow(){if(!transferred)network.release();}} borrow{*network_};
    Endpoint address(TcpEndpoint{config_.family});NativeLength length=address.length;
#if defined(__linux__)
    const auto accepted=::accept4(native(socket_),address.data(),&length,SOCK_NONBLOCK|SOCK_CLOEXEC);
#else
    const auto accepted=::accept(native(socket_),address.data(),&length);
#endif
    if(accepted==invalid_socket){const int e=socket_error();if(pending(e)||interrupted(e))return fail(Error::Busy);return fail(io_error(e));}
    OwnedSocket owned{accepted};
    auto peer=address.decoded(length);if(!peer||!peer->port)return fail(Error::Io);
    if(loopback_only_){
        auto expected=TcpEndpoint::parse(config_.family==AddressFamily::IPv6?"::1":"127.0.0.1",peer->port);
        if(!expected||*peer!=*expected)return fail(Error::PermissionDenied);
    }
    SocketBuffers measured{};auto configured=configure(owned.value,config_,measured);if(!configured)return fail(configured.error());
    borrow.transferred=true;return SocketStream(*network_,static_cast<std::uintptr_t>(owned.release()),config_,measured,SocketState::Connected);
}
Result<TcpEndpoint> Listener::local_endpoint() const noexcept {
    if(auto checked=check();!checked)return fail(checked.error());return socket_endpoint(native(socket_),config_.family,false);
}
Result<std::uint16_t> Listener::port() const noexcept {if(auto s=check();!s)return fail(s.error());return port_;}
Result<SocketBuffers> Listener::buffers() const noexcept {if(auto s=check();!s)return fail(s.error());return buffers_;}
}
