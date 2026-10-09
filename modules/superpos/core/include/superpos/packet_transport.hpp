#pragma once
#include "congestion.hpp"
#include "transport.hpp"

namespace superpos {
struct PacketTransportConfig {
    // Trusted admission supplies a fresh association epoch shared by both ends.
    // It is distinct from logical delivery IDs and never inferred from input.
    Epoch association_epoch{};
    CongestionConfig congestion{};
    std::uint32_t routing_overhead_bytes{};
    std::uint8_t receive_frames{8};
};
struct PacketTransportStats {
    std::uint64_t data_sent{}, probes_sent{}, acknowledgements_sent{};
    std::uint64_t charged_wire_bytes{}, received_data{}, dropped_data{};
    std::uint64_t bytes_in_flight{}, congestion_window{}, smoothed_rtt_us{};
    std::size_t queued_receive_frames{};
    bool owns_pending_send{};
};

// Native authenticated DTLS packet pacing/receipts. WebRTC/SCTP has its own
// congestion controller and must never be wrapped in this adapter. The supplied
// provider/clock outlive the adapter and are accessed exclusively by its owner.
// One pending application write and eight receive frames are fixed bounded
// storage; Busy from send means the exact payload has been copied and owned.
// A second send before successful advance is a capacity error, not acceptance.
// Transport packet receipt does not imply logical Received/Applied/Committed.
// Exhausted packet or diagnostic counters fail the association explicitly;
// neither accepted sends nor received/drop counters wrap back to zero.
// Nested state access from a provider/clock callback returns PermissionDenied
// (ready returns false). Moving or destroying an association on another thread
// or during an active callback is a fatal contract violation. Send/receive
// reject spans that overlap the wrapper or its owned packet-state allocation.
class PacketTransport final : public TransportProvider {
    struct Impl;
    Impl* impl_{};
    Allocator* allocator_{};
public:
    static constexpr std::size_t header_bytes = 17;
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
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    Result<PacketTransportStats> statistics() const noexcept;
};
}
