#pragma once
#include "transport.hpp"
#include <array>
#include <string_view>
namespace superpos {
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
 static Result<UdpSocket> open(const IpEndpoint& local,const IpEndpoint& remote) noexcept;
 Result<std::size_t> send(std::span<const std::byte>) noexcept override;
 Result<std::size_t> receive(std::span<std::byte>) noexcept override;
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
 static Result<UdpListener> bind(const IpEndpoint& local) noexcept;
 Result<IpEndpoint> local_endpoint() const noexcept;
 Result<std::size_t> send_to(const IpEndpoint&,std::span<const std::byte>) noexcept;
 // A rejected oversize datagram is drained without altering caller output.
 // Empty datagrams are consumed and return Busy, matching UdpSocket.
 Result<ReceivedDatagram> receive_from(std::span<std::byte>) noexcept;
};
}
