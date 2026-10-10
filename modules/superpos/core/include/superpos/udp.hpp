#pragma once
#include "transport.hpp"
#include <array>
#include <string_view>
namespace superpos {
// Socket policy. Every socket binds exclusively: Windows sets
// SO_EXCLUSIVEADDRUSE and other platforms set no address-reuse option, so
// another local process cannot bind the same address and port. reuse_port
// (Linux SO_REUSEPORT, for deliberately sharded listeners) must be requested
// explicitly and is unsupported elsewhere. receive_buffer_bytes requests a
// kernel receive buffer (0 keeps the system default; at most 64 MiB); the
// effective size the kernel granted is reported by receive_buffer_bytes().
struct UdpOptions {
 std::uint32_t receive_buffer_bytes{};
 bool reuse_port{};
 static constexpr std::uint32_t maximum_receive_buffer_bytes=64U<<20;
};
struct IpEndpoint {
 std::array<std::byte,128> address{};
 std::uint32_t length{};
 static Result<IpEndpoint> parse(std::string_view numeric_address,std::uint16_t port) noexcept;
};
class UdpSocket final : public DatagramIO {
 std::uintptr_t handle_{~std::uintptr_t{0}};
public:
 // Connected numeric endpoints only. Each receive drains one whole datagram;
 // oversize input is rejected without exposing a prefix.
 UdpSocket() noexcept=default;~UdpSocket();
 UdpSocket(const UdpSocket&)=delete;
 UdpSocket(UdpSocket&&) noexcept;
 UdpSocket& operator=(UdpSocket&&) noexcept;
 static Result<UdpSocket> open(const IpEndpoint& local,const IpEndpoint& remote,UdpOptions={}) noexcept;
 // CapacityExceeded: the local path refused the datagram as oversize (DF set).
 Result<std::size_t> send(std::span<const std::byte>) noexcept override;
 Result<std::size_t> receive(std::span<std::byte>) noexcept override;
 // Whether datagrams carry the don't-fragment bit (Linux: probe mode, which
 // ignores the kernel PMTU cache). False only where the platform has no control.
 Result<bool> dont_fragment() const noexcept;
 Result<IpEndpoint> local_endpoint() const noexcept;
 Result<std::uint32_t> receive_buffer_bytes() const noexcept;
};
struct ReceivedDatagram {
 std::size_t bytes{};
 IpEndpoint source{};
};
// Unauthenticated, nonblocking datagram I/O for admission and relay services.
// One owner serializes access. Address receipt alone never proves peer identity
// or authorizes forwarding. Every operation is bounded to one complete datagram.
class UdpListener final {
 std::uintptr_t handle_{~std::uintptr_t{0}};
public:
 UdpListener() noexcept=default;
 ~UdpListener();
 UdpListener(const UdpListener&)=delete;
 UdpListener& operator=(const UdpListener&)=delete;
 UdpListener(UdpListener&&) noexcept;
 UdpListener& operator=(UdpListener&&) noexcept;
 static Result<UdpListener> bind(const IpEndpoint& local,UdpOptions={}) noexcept;
 Result<std::uint32_t> receive_buffer_bytes() const noexcept;
 Result<IpEndpoint> local_endpoint() const noexcept;
 // CapacityExceeded: the local path refused the datagram as oversize (DF set).
 Result<std::size_t> send_to(const IpEndpoint&,std::span<const std::byte>) noexcept;
 Result<bool> dont_fragment() const noexcept;
 // A rejected oversize datagram is drained without altering caller output.
 // Empty datagrams are consumed and return Busy, matching UdpSocket.
 Result<ReceivedDatagram> receive_from(std::span<std::byte>) noexcept;
};
}
