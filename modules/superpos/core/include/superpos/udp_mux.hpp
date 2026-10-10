#pragma once
#include "udp.hpp"

namespace superpos {
// Single-port native UDP server: one bound socket per address family carries
// every peer association. Each client-to-server datagram starts with a 9-byte
// cleartext routing prefix (tag 0xF0, u64 connection ID) ahead of its DTLS
// record; server-to-client datagrams carry no prefix. Connection IDs come from
// trusted admission, are nonzero, and must be unpredictable: the ID routes,
// it never authenticates. A changed source address therefore still reaches its
// association (NAT rebinding), but outbound traffic moves only after the DTLS
// layer validates the new address with an encrypted challenge/response.
//
// Admission is stateless for unknown traffic: datagrams with no prefix, an
// unknown connection ID, or (before a path exists) anything but a DTLS
// handshake record are dropped without a reply or any allocation. Until an
// address is authenticated, bytes sent to it are limited to
// amplification_factor times the bytes received from it. Every association has
// its own bounded queue, so a flooding peer only overflows its own queue.
// One owner thread drives the mux and all of its ports.
struct UdpMuxConfig {
    std::uint32_t maximum_associations{256};   // 1..4096
    std::uint16_t queue_datagrams{32};         // per association, 1..256
    std::uint16_t poll_quantum{256};           // datagrams drained per poll, 1..4096
    std::uint8_t amplification_factor{3};      // 1..10
    // Poll the shared sockets when a port's queue is empty, so owners that
    // pump associations need no separate poll() call.
    bool poll_on_empty_receive{true};
    // Shared-socket kernel receive buffer request (0: system default). One
    // socket carries every association, so busy hosts should raise it.
    std::uint32_t receive_buffer_bytes{};
};
struct UdpMuxStatistics {
    std::uint64_t datagrams_received{}, datagrams_routed{}, datagrams_sent{};
    std::uint64_t malformed{}, unknown_connection{}, not_handshake{}, foreign_during_handshake{};
    std::uint64_t queue_overflow{}, amplification_limited{}, candidate_paths{}, path_promotions{};
    std::uint32_t attached{};
};
class UdpMux;
// The DatagramIO for one server-side association. The mux outlives its ports;
// destroying a port detaches its connection ID and frees its queue.
class UdpMuxPort final : public DatagramIO {
    friend class UdpMux;
    UdpMux* mux_{};
    std::uint32_t slot_{};
    void detach() noexcept;
public:
    UdpMuxPort() noexcept=default;
    ~UdpMuxPort();
    UdpMuxPort(const UdpMuxPort&)=delete;
    UdpMuxPort& operator=(const UdpMuxPort&)=delete;
    UdpMuxPort(UdpMuxPort&&) noexcept;
    UdpMuxPort& operator=(UdpMuxPort&&) noexcept;
    Result<std::size_t> send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    bool path_aware() const noexcept override { return mux_!=nullptr; }
    DatagramPath received_path() const noexcept override;
    std::span<const std::byte> path_identity() const noexcept override;
    Result<std::size_t> send_candidate(std::uint64_t generation,std::span<const std::byte>) noexcept override;
    Status promote_candidate(std::uint64_t generation) noexcept override;
    void path_authenticated() noexcept override;
    Status poll() noexcept override;
    // Forget the current and candidate paths and queued datagrams, keeping the
    // connection ID attached (for a replacement association after a failure).
    Status reset() noexcept;
    Result<IpEndpoint> current_endpoint() const noexcept;
};
class UdpMux {
    friend class UdpMuxPort;
    struct Impl;
    Impl* impl_{};
    Allocator* allocator_{};
    void release() noexcept;
public:
    static constexpr std::byte routing_tag{0xF0};
    static constexpr std::size_t routing_bytes=9;
    UdpMux() noexcept=default;
    ~UdpMux();
    UdpMux(const UdpMux&)=delete;
    UdpMux& operator=(const UdpMux&)=delete;
    // Moving a mux with attached ports is a contract violation (ports refer to it).
    UdpMux(UdpMux&&) noexcept;
    UdpMux& operator=(UdpMux&&) noexcept;
    // One or two numeric endpoints of distinct families; port 0 picks a port.
    static Result<UdpMux> bind(Allocator&,std::span<const IpEndpoint> local,UdpMuxConfig={}) noexcept;
    Result<IpEndpoint> local_endpoint(std::size_t index=0) const noexcept;
    // Effective kernel receive buffer of one shared socket.
    Result<std::uint32_t> receive_buffer_bytes(std::size_t index=0) const noexcept;
    Result<UdpMuxPort> attach(std::uint64_t connection_id) noexcept;
    // Drain up to poll_quantum datagrams per socket and route them.
    Status poll() noexcept;
    Result<UdpMuxStatistics> statistics() const noexcept;
};
// Client side of the single-port server: a connected UDP socket that prefixes
// every outbound datagram with the routing tag and connection ID. Size the DTLS
// association's udp_payload_ceiling and the packet transport's routing
// overhead by routing_bytes so the complete datagram stays within 1,200 bytes.
class RoutedUdpSocket final : public DatagramIO {
    UdpSocket socket_{};
    IpEndpoint remote_{};
    std::uint64_t connection_id_{};
public:
    RoutedUdpSocket() noexcept=default;
    static Result<RoutedUdpSocket> open(const IpEndpoint& local,const IpEndpoint& remote,std::uint64_t connection_id) noexcept;
    Result<std::size_t> send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    // Move to a new local endpoint (the client-side view of a NAT rebinding or
    // interface change). Datagrams in the old socket are discarded.
    Status rebind(const IpEndpoint& local) noexcept;
    Result<IpEndpoint> local_endpoint() const noexcept;
};
}
