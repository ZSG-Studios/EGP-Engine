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
#else
using Socket=int;bool init() noexcept{return true;}
void close_socket(Socket s) noexcept {close(s);}
bool again() noexcept {
 const auto error=errno;
 // Connected UDP can report ICMP refusal for an earlier datagram.
 return error==EAGAIN||error==EWOULDBLOCK||error==EINTR||error==ECONNREFUSED;
}
#endif
constexpr auto invalid=~std::uintptr_t{0};
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
Result<UdpSocket> UdpSocket::open(const IpEndpoint& l,const IpEndpoint& r) noexcept {
 if(!init()||l.length==0||r.length!=l.length||l.length>128)return fail(Error::InvalidArgument);
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
 if(::bind(s,reinterpret_cast<sockaddr*>(&ls),static_cast<int>(l.length))!=0||::connect(s,reinterpret_cast<sockaddr*>(&rs),static_cast<int>(r.length))!=0)return fail(Error::Io);
 return result;
}
Result<std::size_t> UdpSocket::send(std::span<const std::byte>b) noexcept {if(handle_==invalid)return fail(Error::NotReady);if(b.empty())return fail(Error::InvalidArgument);if(b.size()>1200)return fail(Error::CapacityExceeded);auto n=::send(static_cast<Socket>(handle_),reinterpret_cast<const char*>(b.data()),static_cast<int>(b.size()),0);if(n<0)return fail(again()?Error::Busy:Error::Io);if(static_cast<std::size_t>(n)!=b.size())return fail(Error::Io);return static_cast<std::size_t>(n);}
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

UdpListener::~UdpListener(){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));}
UdpListener::UdpListener(UdpListener&& other) noexcept:handle_(std::exchange(other.handle_,invalid)){}
UdpListener& UdpListener::operator=(UdpListener&& other) noexcept {
 if(this!=&other){if(handle_!=invalid)close_socket(static_cast<Socket>(handle_));handle_=std::exchange(other.handle_,invalid);}return *this;
}
Result<UdpListener> UdpListener::bind(const IpEndpoint& endpoint) noexcept {
 sockaddr_storage address{};
 if(!init()||!endpoint_storage(endpoint,address))return fail(Error::InvalidArgument);
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
 if(sent<0)return fail(again()?Error::Busy:Error::Io);
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
