#pragma once
#include "result.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace superpos {

// Fresh transport packet numbers, not logical delivery IDs. Times are monotonic
// microseconds supplied by the owner; encrypted receipts must be authenticated.
struct PacketRecord {
    bool occupied{};
    std::uint64_t number{}, sent_at{};
    std::uint32_t bytes{};
};
struct CongestionConfig {
    std::uint32_t datagram_bytes{1200};
    std::uint64_t maximum_window{4 * 1024 * 1024};
    std::uint64_t initial_rtt_us{333000};
    std::uint64_t maximum_ack_delay_us{25000};
    // Token-bucket pacing burst, in full datagrams. One preserves strict
    // inter-datagram pacing; larger values let a single owner pump emit up to
    // this many paced datagrams while the long-run rate stays window/RTT.
    std::uint8_t burst_datagrams{1};
    // Application rate floor in bytes per second (0 = none). The effective window
    // never drops below this rate times the smoothed RTT, so random wireless loss
    // cannot starve a real-time stream below its declared budget; growth and
    // loss response above the floor stay NewReno.
    std::uint64_t minimum_rate_bytes_per_second{0};
};
struct PacketAck {
    std::uint64_t largest{};
    // Bit 0 acknowledges largest, bit n acknowledges largest-n. No implicit
    // cumulative receipt; numbers outside this bounded window remain in flight.
    std::uint64_t bits{};
    std::uint64_t delay_us{};
};
struct CongestionReceipt {
    std::size_t acknowledged_packets{}, lost_packets{};
    std::uint64_t acknowledged_bytes{}, lost_bytes{};
};

// Bounded packet-number NewReno window, RTT/loss tracking and byte pacing.
// This does not retransmit messages or establish application success. The UDP
// adapter must charge complete encrypted/routed datagrams, including retries.
class PacketCongestion {
public:
    static Result<PacketCongestion> create(CongestionConfig,
        std::span<PacketRecord>) noexcept;
    Status can_send(std::uint32_t bytes, std::uint64_t now_us) const noexcept;
    Result<std::uint64_t> sent(std::uint32_t bytes, std::uint64_t now_us) noexcept;
    // ACK-only ciphertext still consumes pacing bandwidth but no flight credit.
    Status sent_untracked(std::uint32_t bytes, std::uint64_t now_us) noexcept;
    // A single PTO credit admits a fresh ack-eliciting packet beyond the window;
    // it never treats the probe timer as a declaration of loss.
    Result<std::uint64_t> sent_probe(std::uint32_t bytes, std::uint64_t now_us) noexcept;
    Result<std::uint64_t> next_packet_number() const noexcept;
    Result<CongestionReceipt> acknowledge(PacketAck, std::uint64_t now_us) noexcept;
    Result<CongestionReceipt> detect_loss(std::uint64_t now_us) noexcept;
    // A PTO alone is not proof of packet loss and does not collapse the window.
    Result<std::uint64_t> probe_timeout_us() const noexcept;
    // Smoothed RTT + max(4 x variance, 1 ms) + maximum ACK delay, without probe
    // backoff: the interval after which an unacknowledged retransmission is due.
    [[nodiscard]] std::uint64_t retransmit_timeout_us() const noexcept;
    Status probe_timeout(std::uint64_t now_us) noexcept;
    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept { return flight_; }
    // Effective window: the NewReno window or the rate floor, whichever is larger.
    [[nodiscard]] std::uint64_t congestion_window() const noexcept { return effective_window(); }
    [[nodiscard]] std::uint64_t smoothed_rtt_us() const noexcept { return smoothed_rtt_; }
    [[nodiscard]] std::uint64_t next_send_us() const noexcept { return next_send_; }
    // True when the pacing bucket admits another datagram at now_us.
    [[nodiscard]] bool pacing_allows(std::uint64_t now_us) const noexcept;
    [[nodiscard]] std::size_t outstanding_packets() const noexcept;

private:
    PacketCongestion(CongestionConfig, std::span<PacketRecord>) noexcept;
    Result<CongestionReceipt> loss(std::uint64_t now_us) noexcept;
    Result<std::uint64_t> admit(std::uint32_t, std::uint64_t, bool probe) noexcept;
    void pace(std::uint32_t, std::uint64_t) noexcept;
    std::uint64_t burst_us() const noexcept;
    std::uint64_t effective_window() const noexcept;
    CongestionConfig config_{};
    std::span<PacketRecord> records_{};
    std::uint64_t next_number_{1}, last_sent_{}, last_time_{}, next_send_{};
    std::uint64_t flight_{}, window_{}, threshold_{}, increase_credit_{};
    std::uint64_t latest_rtt_{}, minimum_rtt_{}, smoothed_rtt_{}, variance_{};
    std::uint64_t largest_acked_{}, recovery_number_{};
    unsigned pto_count_{};
    std::uint64_t pto_anchor_{};
    bool probe_credit_{};
    bool rtt_sampled_{}, exhausted_{};
};
}
