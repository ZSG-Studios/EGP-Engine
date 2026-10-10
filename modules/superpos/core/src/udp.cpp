#include "superpos/udp.hpp"
#include <cstring>
#include <utility>
#include <limits>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif
namespace superpos {
namespace {
#ifdef _WIN32
using Socket=SOCKET;
struct Winsock { int result; Winsock() noexcept { WSADATA d{};result=WSAStartup(MAKEWORD(2,2),&d); } ~Winsock(){if(!result)WSACleanup();} };
bool init() noexcept {static Winsock runtime;return runtime.result==0;}
void close_socket(Socket s) noexcept {closesocket(s);}
bool again() noexcept {
 const auto error=WSAGetLastError();
 // UDP ICMP port-unreachable reports a previous datagram, not peer authentication.
 // Let the bounded transport/handshake deadlines decide whether that peer recovers.
 return error==WSAEWOULDBLOCK||error==WSAECONNRESET;
}
bool oversize() noexcept { return WSAGetLastError()==WSAEMSGSIZE; }
#else
using Socket=int;bool init() noexcept{return true;}
void close_socket(Socket s) noexcept {close(s);}
bool again() noexcept {
 const auto error=errno;
 // Connected UDP can report ICMP refusal or a too-big notice for an earlier
 // datagram; neither is this receive's failure.
 return error==EAGAIN||error==EWOULDBLOCK||error==EINTR||error==ECONNREFUSED||error==EMSGSIZE;
}
bool oversize() noexcept { return errno==EMSGSIZE; }
#endif
constexpr auto invalid=~std::uintptr_t{0};
// A send the local path refuses as larger than its MTU is a distinct, nonfatal
// outcome: datagram path MTU probing treats it as that size's loss.
Error send_error() noexcept { return oversize()?Error::CapacityExceeded:again()?Error::Busy:Error::Io; }
// Datagrams carry the don't-fragment bit so path MTU probes measure the real
// path. Linux uses probe mode: DF without a kernel-cached PMTU send ceiling, so
// the authenticated probes above remain the only source of size decisions.
// Platforms without such a control leave datagrams unchanged.
bool set_dont_fragment(Socket socket,int family) noexcept {
#if defined(_WIN32)
 const DWORD enabled=1;
 const int level=family==AF_INET?IPPROTO_IP:IPPROTO_IPV6,name=family==AF_INET?IP_DONTFRAGMENT:IPV6_DONTFRAG;
 return setsockopt(socket,level,name,reinterpret_cast<const char*>(&enabled),sizeof(enabled))==0;
#elif defined(IP_MTU_DISCOVER) && defined(IPV6_MTU_DISCOVER)
 if(family==AF_INET){const int mode=IP_PMTUDISC_PROBE;return setsockopt(socket,IPPROTO_IP,IP_MTU_DISCOVER,&mode,sizeof(mode))==0;}
 const int mode=IPV6_PMTUDISC_PROBE;if(setsockopt(socket,IPPROTO_IPV6,IPV6_MTU_DISCOVER,&mode,sizeof(mode))!=0)return false;
#if defined(IPV6_DONTFRAG)
 const int enabled=1;if(setsockopt(socket,IPPROTO_IPV6,IPV6_DONTFRAG,&enabled,sizeof(enabled))!=0)return false;
#endif
 return true;
#elif defined(IP_DONTFRAG) && defined(IPV6_DONTFRAG)
 const int enabled=1;
 return setsockopt(socket,family==AF_INET?IPPROTO_IP:IPPROTO_IPV6,family==AF_INET?IP_DONTFRAG:IPV6_DONTFRAG,&enabled,sizeof(enabled))==0;
#else
 (void)socket;(void)family;return true;
#endif
}
// Exclusive binding, optional explicit port sharing and the receive buffer,
// applied before bind.
bool apply_options(Socket socket,const UdpOptions& options) noexcept {
 if(options.receive_buffer_bytes>UdpOptions::maximum_receive_buffer_bytes)return false;
#ifdef _WIN32
 if(options.reuse_port)return false;
 const BOOL exclusive=TRUE;
 if(setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0)return false;
#elif defined(SO_REUSEPORT) && defined(__linux__)
 if(options.reuse_port){const int enabled=1;if(setsockopt(socket,SOL_SOCKET,SO_REUSEPORT,&enabled,sizeof(enabled))!=0)return false;}
#else
 if(options.reuse_port)return false;
#endif
 if(options.receive_buffer_bytes){
  const int bytes=static_cast<int>(options.receive_buffer_bytes);
  if(setsockopt(socket,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&bytes),sizeof(bytes))!=0)return false;
 }
 return true;
}
Result<std::uint32_t> receive_buffer(std::uintptr_t handle) noexcept {
 int value=0;
#ifdef _WIN32
 int size=sizeof(value);
#else
 socklen_t size=sizeof(value);
#endif
 if(getsockopt(static_cast<Socket>(handle),SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&value),&size)!=0||value<=0)return fail(Error::Io);
 return static_cast<std::uint32_t>(value);
}
Result<bool> dont_fragment_state(std::uintptr_t handle) noexcept {
 const auto socket=static_cast<Socket>(handle);
 sockaddr_storage address{};
#ifdef _WIN32
 int address_size=sizeof(address);
#else
 socklen_t address_size=sizeof(address);
#endif
 if(getsockname(socket,reinterpret_cast<sockaddr*>(&address),&address_size)!=0)return fail(Error::Io);
 const int family=address.ss_family;
#if defined(_WIN32)
 DWORD value=0;int size=sizeof(value);
 if(getsockopt(socket,family==AF_INET?IPPROTO_IP:IPPROTO_IPV6,family==AF_INET?IP_DONTFRAGMENT:IPV6_DONTFRAG,reinterpret_cast<char*>(&value),&size)!=0)return fail(Error::Io);
 return value!=0;
#elif defined(IP_MTU_DISCOVER) && defined(IPV6_MTU_DISCOVER)
 int value=0;socklen_t size=sizeof(value);
 if(family==AF_INET){if(getsockopt(socket,IPPROTO_IP,IP_MTU_DISCOVER,&value,&size)!=0)return fail(Error::Io);return value==IP_PMTUDISC_PROBE;}
 if(getsockopt(socket,IPPROTO_IPV6,IPV6_MTU_DISCOVER,&value,&size)!=0)return fail(Error::Io);return value==IPV6_PMTUDISC_PROBE;
#elif defined(IP_DONTFRAG) && defined(IPV6_DONTFRAG)
 int value=0;socklen_t size=sizeof(value);
 if(getsockopt(socket,family==AF_INET?IPPROTO_IP:IPPROTO_IPV6,family==AF_INET?IP_DONTFRAG:IPV6_DONTFRAG,&value,&size)!=0)return fail(Error::Io);
 return value!=0;
#else
 (void)family;return false;
#endif
}
bool overlaps_listener(const void* data,std::size_t size,const UdpListener* listener) noexcept {
 if(!size)return false;
 const auto begin=reinterpret_cast<std::uintptr_t>(data),owner=reinterpret_cast<std::uintptr_t>(listener);
 if(!data||size>(std::numeric_limits<std::uintptr_t>::max)()-begin)return true;
 return begin<owner?owner-begin<size:begin-owner<sizeof(UdpListener);
}

bool endpoint_storage(const IpEndpoint& endpoint,sockaddr_storage& out) noexcept {
 if(endpoint.length>sizeof(out)||endpoint.length<sizeof(out.ss_family))return false;
 std::memcpy(&out,endpoint.address.data(),endpoint.length);
 if(out.ss_family==AF_INET)return endpoint.length==sizeof(sockaddr_in);
 if(out.ss_family==AF_INET6)return endpoint.length==sizeof(sockaddr_in6);
 return false;
}
#ifdef _WIN32
using AddressLength=int;
#else
using AddressLength=socklen_t;
#endif

}
Result<IpEndpoint> IpEndpoint::parse(std::string_view text,std::uint16_t port) noexcept {
 if(text.empty()||text.size()>64||text.find('\0')!=std::string_view::npos||!init())return fail(Error::InvalidArgument);
 char host[65]{};std::memcpy(host,text.data(),text.size());
 IpEndpoint e; sockaddr_in v4{};v4.sin_family=AF_INET;v4.sin_port=htons(port);
 if(inet_pton(AF_INET,host,&v4.sin_addr)==1){std::memcpy(e.address.data(),&v4,sizeof(v4));e.length=sizeof(v4);return e;}
 sockaddr_in6 v6{};v6.sin6_family=AF_INET6;v6.sin6_port=htons(port);
 if(inet_pton(AF_INET6,host,&v6.sin6_addr)==1){std::memcpy(e.address.data(),&v6,sizeof(v6));e.length=sizeof(v6);return e;}
 return fail(Error::InvalidArgument);
}
UdpSocket::~UdpSocket(){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));}
UdpSocket::UdpSocket(UdpSocket&& s) noexcept:handle_(std::exchange(s.handle_,invalid)){}
UdpSocket& UdpSocket::operator=(UdpSocket&& s) noexcept {if(this!=&s){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));handle_=std::exchange(s.handle_,invalid);}return *this;}
Result<UdpSocket> UdpSocket::open(const IpEndpoint& l,const IpEndpoint& r,UdpOptions options) noexcept {
 if(!init()||l.length==0||r.length!=l.length||l.length>128||options.receive_buffer_bytes>UdpOptions::maximum_receive_buffer_bytes)return fail(Error::InvalidArgument);
#ifndef __linux__
 if(options.reuse_port)return fail(Error::Unsupported);
#endif
 sockaddr_storage ls{},rs{};std::memcpy(&ls,l.address.data(),l.length);std::memcpy(&rs,r.address.data(),r.length);
 if(ls.ss_family!=rs.ss_family||(ls.ss_family!=AF_INET&&ls.ss_family!=AF_INET6))return fail(Error::InvalidArgument);
 auto expected=ls.ss_family==AF_INET?sizeof(sockaddr_in):sizeof(sockaddr_in6);
 if(l.length!=expected||r.length!=expected)return fail(Error::InvalidArgument);
 auto s=::socket(ls.ss_family,SOCK_DGRAM,IPPROTO_UDP);
 if(s==static_cast<Socket>(-1))return fail(Error::Io);
 UdpSocket result;result.handle_=static_cast<std::uintptr_t>(s);
#ifdef _WIN32
 u_long nonblock=1;if(ioctlsocket(s,FIONBIO,&nonblock)!=0)return fail(Error::Io);
#else
 auto flags=fcntl(s,F_GETFL,0);if(flags<0||fcntl(s,F_SETFL,flags|O_NONBLOCK)!=0)return fail(Error::Io);
#endif
 if(!set_dont_fragment(s,ls.ss_family)||!apply_options(s,options))return fail(Error::Io);
 if(::bind(s,reinterpret_cast<sockaddr*>(&ls),static_cast<int>(l.length))!=0||::connect(s,reinterpret_cast<sockaddr*>(&rs),static_cast<int>(r.length))!=0)return fail(Error::Io);
 return result;
}
Result<std::size_t> UdpSocket::send(std::span<const std::byte>b) noexcept {if(handle_==invalid)return fail(Error::NotReady);if(b.empty())return fail(Error::InvalidArgument);if(b.size()>1200)return fail(Error::CapacityExceeded);auto n=::send(static_cast<Socket>(handle_),reinterpret_cast<const char*>(b.data()),static_cast<int>(b.size()),0);if(n<0)return fail(send_error());if(static_cast<std::size_t>(n)!=b.size())return fail(Error::Io);return static_cast<std::size_t>(n);}
Result<std::size_t> UdpSocket::receive(std::span<std::byte>b) noexcept {
 // Always drain into a full datagram buffer. Oversize datagrams never expose a prefix.
 if(handle_==invalid)return fail(Error::NotReady);
 std::array<std::byte,65536> packet;
 auto n=::recv(static_cast<Socket>(handle_),reinterpret_cast<char*>(packet.data()),static_cast<int>(packet.size()),0);
 if(n<0)return fail(again()?Error::Busy:Error::Io);
 if(n==0)return fail(Error::Busy);
 if(static_cast<std::size_t>(n)>1200||static_cast<std::size_t>(n)>b.size())return fail(Error::CapacityExceeded);
 if(n)std::memcpy(b.data(),packet.data(),static_cast<std::size_t>(n));return static_cast<std::size_t>(n);
}

Result<bool> UdpSocket::dont_fragment() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);return dont_fragment_state(handle_);
}
Result<IpEndpoint> UdpSocket::local_endpoint() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);
 sockaddr_storage address{};AddressLength size=sizeof(address);
 if(getsockname(static_cast<Socket>(handle_),reinterpret_cast<sockaddr*>(&address),&size)!=0)return fail(Error::Io);
 if(size<=0||static_cast<std::size_t>(size)>sizeof(address))return fail(Error::ProtocolViolation);
 IpEndpoint result;result.length=static_cast<std::uint32_t>(size);std::memcpy(result.address.data(),&address,result.length);
 sockaddr_storage checked{};if(!endpoint_storage(result,checked))return fail(Error::ProtocolViolation);return result;
}
Result<std::uint32_t> UdpSocket::receive_buffer_bytes() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);return receive_buffer(handle_);
}
Result<std::uint32_t> UdpListener::receive_buffer_bytes() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);return receive_buffer(handle_);
}
Result<bool> UdpListener::dont_fragment() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);return dont_fragment_state(handle_);
}
UdpListener::~UdpListener(){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));}
UdpListener::UdpListener(UdpListener&& other) noexcept:handle_(std::exchange(other.handle_,invalid)){}
UdpListener& UdpListener::operator=(UdpListener&& other) noexcept {
 if(this!=&other){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));handle_=std::exchange(other.handle_,invalid);}return *this;
}
Result<UdpListener> UdpListener::bind(const IpEndpoint& endpoint,UdpOptions options) noexcept {
 sockaddr_storage address{};
 if(!init()||!endpoint_storage(endpoint,address)||options.receive_buffer_bytes>UdpOptions::maximum_receive_buffer_bytes)return fail(Error::InvalidArgument);
#ifndef __linux__
 if(options.reuse_port)return fail(Error::Unsupported);
#endif
 auto socket=::socket(address.ss_family,SOCK_DGRAM,IPPROTO_UDP);
 if(socket==static_cast<Socket>(-1))return fail(Error::Io);
 UdpListener result;result.handle_=static_cast<std::uintptr_t>(socket);
#ifdef _WIN32
 u_long nonblock=1;if(ioctlsocket(socket,FIONBIO,&nonblock)!=0)return fail(Error::Io);
#else
 auto flags=fcntl(socket,F_GETFL,0);if(flags<0||fcntl(socket,F_SETFL,flags|O_NONBLOCK)!=0)return fail(Error::Io);
#endif
 // Separate family listeners have explicit routing; do not inherit OS-specific
 // dual-stack defaults or accept IPv4-mapped addresses through the IPv6 socket.
 if(address.ss_family==AF_INET6){
  const int enabled=1;
  if(setsockopt(socket,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&enabled),sizeof(enabled))!=0)return fail(Error::Io);
 }
 if(!set_dont_fragment(socket,address.ss_family)||!apply_options(socket,options))return fail(Error::Io);
#ifdef _WIN32
 // An unconnected socket would otherwise report an earlier datagram's ICMP
 // port-unreachable as a receive failure, stalling the drain of later datagrams.
 BOOL report=FALSE;DWORD returned=0;
 if(WSAIoctl(socket,_WSAIOW(IOC_VENDOR,12),&report,sizeof(report),nullptr,0,&returned,nullptr,nullptr)!=0)return fail(Error::Io);
#endif
 if(::bind(socket,reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(endpoint.length))!=0)return fail(Error::Io);
 return result;
}
Result<IpEndpoint> UdpListener::local_endpoint() const noexcept {
 if(handle_==invalid)return fail(Error::NotReady);
 sockaddr_storage address{};AddressLength size=sizeof(address);
 if(getsockname(static_cast<Socket>(handle_),reinterpret_cast<sockaddr*>(&address),&size)!=0)return fail(Error::Io);
 if(size<=0||static_cast<std::size_t>(size)>sizeof(address))return fail(Error::ProtocolViolation);
 IpEndpoint result;result.length=static_cast<std::uint32_t>(size);std::memcpy(result.address.data(),&address,result.length);
 sockaddr_storage checked{};if(!endpoint_storage(result,checked))return fail(Error::ProtocolViolation);return result;
}
Result<std::size_t> UdpListener::send_to(const IpEndpoint& endpoint,std::span<const std::byte> bytes) noexcept {
 if(handle_==invalid)return fail(Error::NotReady);
 if(bytes.empty()||overlaps_listener(bytes.data(),bytes.size(),this))return fail(Error::InvalidArgument);
 if(bytes.size()>1200)return fail(Error::CapacityExceeded);
 sockaddr_storage address{};if(!endpoint_storage(endpoint,address))return fail(Error::InvalidArgument);
 auto sent=::sendto(static_cast<Socket>(handle_),reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),0,
                    reinterpret_cast<const sockaddr*>(&address),static_cast<AddressLength>(endpoint.length));
 if(sent<0)return fail(send_error());
 if(static_cast<std::size_t>(sent)!=bytes.size())return fail(Error::Io);return bytes.size();
}
Result<ReceivedDatagram> UdpListener::receive_from(std::span<std::byte> output) noexcept {
 if(handle_==invalid)return fail(Error::NotReady);
 if(overlaps_listener(output.data(),output.size(),this))return fail(Error::InvalidArgument);
 // Same fixed scratch ceiling as connected UDP, sufficient for a complete UDP
 // datagram. Its stack memory is separate from charged heap allocations.
 std::array<std::byte,65536> packet;
 sockaddr_storage source{};AddressLength size=sizeof(source);
 auto received=::recvfrom(static_cast<Socket>(handle_),reinterpret_cast<char*>(packet.data()),static_cast<int>(packet.size()),0,
                          reinterpret_cast<sockaddr*>(&source),&size);
 if(received<0)return fail(again()?Error::Busy:Error::Io);
 if(!received)return fail(Error::Busy);
 if(static_cast<std::size_t>(received)>1200||static_cast<std::size_t>(received)>output.size())return fail(Error::CapacityExceeded);
 if(size<=0||static_cast<std::size_t>(size)>sizeof(source))return fail(Error::ProtocolViolation);
 ReceivedDatagram result;result.bytes=static_cast<std::size_t>(received);result.source.length=static_cast<std::uint32_t>(size);
 std::memcpy(result.source.address.data(),&source,result.source.length);
 sockaddr_storage checked{};if(!endpoint_storage(result.source,checked))return fail(Error::ProtocolViolation);
 std::memcpy(output.data(),packet.data(),result.bytes);return result;
}
}
