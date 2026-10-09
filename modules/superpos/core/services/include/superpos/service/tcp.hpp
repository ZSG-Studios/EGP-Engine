// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/stream.hpp"
#include <cstdint>
#include <thread>
#include <array>
#include <string_view>

namespace superpos::service::net {
enum class AddressFamily : std::uint8_t { IPv4, IPv6 };
using LoopbackFamily=AddressFamily;
// Numeric-only endpoints keep DNS and certificate identity separate. IPv4 uses
// the first four address bytes; the remaining bytes and scope must be zero.
struct TcpEndpoint {
    AddressFamily family{AddressFamily::IPv4};
    std::array<std::byte,16> address{};
    std::uint16_t port{};
    std::uint32_t scope{};
    static Result<TcpEndpoint> parse(std::string_view numeric_address,std::uint16_t port,std::uint32_t ipv6_scope=0) noexcept;
    friend bool operator==(const TcpEndpoint&,const TcpEndpoint&)=default;
};
struct SocketConfig {
    std::uint32_t send_quantum{4096};
    std::uint32_t receive_quantum{4096};
    int buffer_hint{8192}; // Request only; measured OS values can be larger.
    LoopbackFamily family{LoopbackFamily::IPv4};
};
struct SocketBuffers { int send{},receive{}; };
struct SocketStats {
    std::uint64_t sent{},received{},send_busy{},receive_busy{},send_syscalls{},receive_syscalls{};
};

// One cooperative owner thread. The initialized Network outlives every open
// Listener/SocketStream; destroying an active borrower owner fails closed.
class Network {
    const std::thread::id owner_{std::this_thread::get_id()};
    std::size_t borrowers_{};
    bool ready_{},busy_{};
    friend class Listener;
    friend class SocketStream;
    Status check() const noexcept;
    Status acquire() noexcept;
    void release() noexcept;
public:
    Network() noexcept=default;
    ~Network();
    Network(const Network&)=delete;
    Network& operator=(const Network&)=delete;
    Status initialize() noexcept;
};

enum class SocketState { Closed,Connecting,Connected,Failed };
class SocketStream final:public StreamIO {
    static constexpr std::uintptr_t invalid_=UINTPTR_MAX;
    Network* network_{};
    std::uintptr_t socket_{invalid_};
    const std::thread::id owner_{std::this_thread::get_id()};
    SocketConfig config_{};
    SocketBuffers buffers_{};
    SocketStats stats_{};
    SocketState state_{SocketState::Closed};
    friend class Listener;
    SocketStream(Network&,std::uintptr_t,SocketConfig,SocketBuffers,SocketState) noexcept;
    Status check() const noexcept;
    void destroy() noexcept;
public:
    SocketStream() noexcept=default;
    ~SocketStream() override;
    SocketStream(const SocketStream&)=delete;
    SocketStream& operator=(const SocketStream&)=delete;
    SocketStream(SocketStream&&) noexcept;
    SocketStream& operator=(SocketStream&&) noexcept;
    // Endpoint selects the address family; config.family is only for the
    // compatibility loopback helper. Remote port must be nonzero.
    static Result<SocketStream> connect(Network&,TcpEndpoint,SocketConfig={}) noexcept;
    Result<TcpEndpoint> local_endpoint() const noexcept;
    Result<TcpEndpoint> remote_endpoint() const noexcept;
    // Only 127.0.0.1 or ::1, selected explicitly, with a nonzero port.
    static Result<SocketStream> connect_loopback(Network&,std::uint16_t port,SocketConfig={}) noexcept;
    Status finish_connect() noexcept; // Zero-time select/SO_ERROR; Busy while pending.
    Result<std::size_t> send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    Result<SocketState> state() const noexcept;
    Result<SocketBuffers> buffers() const noexcept;
    Result<SocketStats> stats() const noexcept;
    Status close() noexcept;
};

class Listener {
    static constexpr std::uintptr_t invalid_=UINTPTR_MAX;
    Network* network_{};
    std::uintptr_t socket_{invalid_};
    const std::thread::id owner_{std::this_thread::get_id()};
    SocketConfig config_{};
    SocketBuffers buffers_{};
    std::uint16_t port_{};
    bool loopback_only_{};
    Listener(Network&,std::uintptr_t,SocketConfig,SocketBuffers,std::uint16_t) noexcept;
    Status check() const noexcept;
    void destroy() noexcept;
public:
    Listener() noexcept=default;
    ~Listener();
    Listener(const Listener&)=delete;
    Listener& operator=(const Listener&)=delete;
    Listener(Listener&&) noexcept;
    Listener& operator=(Listener&&) noexcept;
    // Explicit numeric bind; port zero requests an ephemeral port. Wildcard
    // addresses are permitted only by explicit selection. IPv6 is V6ONLY.
    // Endpoint selects config.family; backlog is bounded to 128.
    static Result<Listener> bind(Network&,TcpEndpoint,SocketConfig={},std::uint32_t backlog=32) noexcept;
    Result<TcpEndpoint> local_endpoint() const noexcept;
    // Bind ephemeral selected loopback only. IPv6 is explicitly V6ONLY.
    // A positive backlog is bounded to 8.
    static Result<Listener> bind_loopback(Network&,SocketConfig={},std::uint32_t backlog=8) noexcept;
    Result<SocketStream> accept() noexcept;
    Result<std::uint16_t> port() const noexcept;
    Result<SocketBuffers> buffers() const noexcept;
    Status close() noexcept;
};
}
