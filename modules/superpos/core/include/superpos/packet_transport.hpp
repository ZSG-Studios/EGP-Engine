#pragma once
#include "congestion.hpp"
#include "transport.hpp"

namespace superpos {
// Datagram packetization-layer path MTU discovery (after RFC 8899), sized in
// complete UDP payload bytes including the 37-byte DTLS record overhead and any
// routing prefix. The search never exceeds the 1,200-byte congestion datagram
// ceiling or the carrier's frame ceiling. Probes are padded, authenticated
// carrier frames with a sequence space separate from packet numbers: their loss
// never reduces the congestion window, at most one is outstanding, each size is
// tried at most maximum_probes times with doubling timeouts, and a completed
// search stays idle until the confirmation/raise timers or black-hole evidence.
struct PathMtuConfig {
    bool probing{false};
    // Assumed-usable base. Never probed below; the sender always fits this size.
    std::uint16_t minimum_datagram_bytes{576};
    // Search stops once confirmed and failed sizes are this close.
    std::uint16_t search_granularity_bytes{16};
    std::uint8_t maximum_probes{3};
    // Consecutive tracked datagrams above the base declared lost (with no larger
    // datagram acknowledged in between) before the current size is re-confirmed.
    std::uint8_t black_hole_losses{3};
    // After a shrink, try to raise again after this interval, doubling to the
    // confirmation interval. A confirmed ceiling is re-confirmed periodically.
    std::uint64_t raise_interval_ms{60000};
    std::uint64_t confirm_interval_ms{600000};
};
enum class PathMtuState : std::uint8_t { Disabled, Confirming, Searching, Complete };
struct PacketTransportConfig {
    // Trusted admission supplies a fresh association epoch shared by both ends.
    // It is distinct from logical delivery IDs and never inferred from input.
    Epoch association_epoch{};
    CongestionConfig congestion{};
    std::uint32_t routing_overhead_bytes{};
    std::uint8_t receive_frames{8};
    // Opt-in coalescing: send() appends frames to a bundle datagram (with a
    // piggybacked ACK) and flush() transmits it at the end of the owner pump.
    // Off preserves the one-frame-per-datagram contract exactly.
    bool bundle_frames{false};
    PathMtuConfig path_mtu{};
};
struct PacketTransportStats {
    std::uint64_t data_sent{}, probes_sent{}, acknowledgements_sent{};
    std::uint64_t charged_wire_bytes{}, received_data{}, dropped_data{};
    std::uint64_t bundles_sent{}, bundled_frames{}, piggybacked_acknowledgements{};
    std::uint64_t bytes_in_flight{}, congestion_window{}, smoothed_rtt_us{}, retransmit_timeout_us{};
    std::size_t queued_receive_frames{};
    bool owns_pending_send{};
    // Validated complete UDP payload currently used for sending; the receive
    // ceiling is independent and never shrinks with the peer's send path.
    std::uint64_t path_mtu_bytes{}, path_mtu_changes{};
    std::uint64_t path_mtu_probes_sent{}, path_mtu_probes_acknowledged{}, path_mtu_probes_lost{};
    std::uint64_t path_mtu_acknowledgements_sent{}, path_mtu_stale_acknowledgements{}, path_mtu_black_holes{};
    PathMtuState path_mtu_state{PathMtuState::Disabled};
    // Tracked (ack-eliciting) packets this peer acknowledged or that loss
    // detection declared lost, and an exponentially weighted per-packet loss
    // estimate (1/16 weight per packet) in parts per million, for consumers that
    // adapt redundancy to this recipient. Path MTU probes are excluded.
    std::uint64_t packets_acknowledged{}, packets_lost{};
    // Validated peer-address changes reported by the carrier; each restarts
    // path MTU discovery from the base with an immediate ceiling probe.
    std::uint64_t path_changes{};
    std::uint32_t loss_rate_ppm{};
};

// Native authenticated DTLS packet pacing/receipts. WebRTC/SCTP has its own
// congestion controller and must never be wrapped in this adapter. The supplied
// provider/clock outlive the adapter and are accessed exclusively by its owner.
// One pending application write and up to 64 receive frames are fixed bounded
// storage; Busy from send means the exact payload has been copied and owned.
// A second send before successful advance is a capacity error, not acceptance.
// Transport packet receipt does not imply logical Received/Applied/Committed.
// Exhausted packet or diagnostic counters fail the association explicitly;
// neither accepted sends nor received/drop counters wrap back to zero.
// Nested state access from a provider/clock callback returns PermissionDenied
// (ready returns false). Moving or destroying an association on another thread
// or during an active callback is a fatal contract violation. Send/receive
// reject spans that overlap the wrapper or its owned packet-state allocation.
// With path MTU probing, capabilities().maximum_frame follows the validated send
// size between owner pumps (it changes only inside advance()); frames already
// accepted at a larger size are still sent, and loss recovery belongs to the
// logical layer, which resegments retries to the new size.
class PacketTransport final : public TransportProvider {
    struct Impl;
    Impl* impl_{};
    Allocator* allocator_{};
public:
    static constexpr std::size_t header_bytes = 17;
    // Probe/ACK frames: header (tag, epoch, probe sequence) plus u16 frame size.
    static constexpr std::size_t path_mtu_header_bytes = 19;
    PacketTransport() noexcept = default;
    ~PacketTransport();
    PacketTransport(const PacketTransport&) = delete;
    PacketTransport& operator=(const PacketTransport&) = delete;
    PacketTransport(PacketTransport&&) noexcept;
    PacketTransport& operator=(PacketTransport&&) noexcept;
    static Result<PacketTransport> create(Allocator&,Clock&,TransportProvider&,
        PacketTransportConfig) noexcept;
    TransportCapabilities capabilities() const noexcept override;
    bool ready() const noexcept override;
    Status advance() noexcept override;
    Status send(std::span<const std::byte>) noexcept override;
    Status flush() noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    Result<PacketTransportStats> statistics() const noexcept;
};
}
