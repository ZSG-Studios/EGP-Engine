#pragma once

#include "superpos/result.hpp"
#include "superpos/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace superpos {

enum class DeliveryMode : std::uint8_t {
    ReliableOrdered,
    ReliableUnordered,
    UnreliableLatest,
    // One unfragmented frame, no retransmission; gaps do not block later IDs.
    Unreliable,
};

enum class DeliveryStage : std::uint8_t { Admitted, CarrierAccepted, Received, Applied };

struct DeliveryLimits {
    std::uint32_t max_message_bytes{65'536};
    std::uint16_t fragment_payload_bytes{896};
    std::uint16_t max_messages{8};
    // Minimum retransmission interval; the owner may raise it to the measured
    // retransmission timeout with DeliverySender::set_retry_ticks.
    Tick retry_ticks{6};
    // Lifetime and progress deadlines start when a message's first fragment
    // reaches the carrier, so queueing behind earlier messages on a slow link
    // never expires a message; a never-sent message ages from admission.
    Tick timeout_ticks{600};
    Tick progress_timeout_ticks{300};
};

struct DeliveryFragment {
    Epoch epoch{};
    std::uint64_t message{};
    DeliveryMode mode{};
    std::uint32_t total_bytes{};
    std::uint16_t index{};
    std::uint16_t count{};
    std::span<const std::byte> payload{};
    std::uint16_t channel{};
};

// Fixed canonical 48-byte header; complete encoded frame is at most 944 bytes.
Result<std::size_t> encode_fragment(const DeliveryFragment &fragment,
    std::span<std::byte> output, std::uint16_t fragment_payload_bytes = 896) noexcept;
// The returned payload borrows the input frame until receive() copies its bytes.
Result<DeliveryFragment> decode_fragment(std::span<const std::byte> frame,
    std::uint16_t fragment_payload_bytes = 896) noexcept;

// Progress means these fragment bytes were copied into a bounded receiver
// reservation. It is independent of complete Received and application Applied.
// Fixed 40-byte canonical payload; every bit beyond fragments is zero.
struct FragmentReceipt {
    Epoch epoch{}; std::uint64_t message{}; std::uint16_t channel{},fragments{};
    std::array<std::uint64_t,2> bits{};
};
Result<std::size_t> encode_fragment_receipt(const FragmentReceipt&,std::span<std::byte>) noexcept;
Result<FragmentReceipt> decode_fragment_receipt(std::span<const std::byte>) noexcept;

// These records and the byte arena are owned by the caller and outlive the channel.
struct DeliverySlot {
    bool occupied{};
    bool reserved{};
    bool complete{};
    std::uint64_t message{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint16_t fragments{};
    std::uint16_t next_fragment{};
    std::uint16_t received_fragments{};
    std::array<std::uint64_t, 2> received_bits{};
    std::array<std::uint64_t, 2> sent_bits{};
    Tick admitted_at{};
    Tick last_cycle_at{};
    Tick last_progress_at{};
    DeliveryStage stage{DeliveryStage::Admitted};
    std::uint16_t channel{};
};

struct DeliveryView {
    // The payload borrows the shared arena until its next mutation. Copy it
    // into application-owned bounded storage when retaining across pump calls.
    std::uint64_t message{};
    std::span<const std::byte> payload{};
    DeliveryStage stage{DeliveryStage::Received};
    std::uint16_t channel{};
};

enum class FragmentAdmission : std::uint8_t { Accepted, Duplicate, Complete, Superseded };

struct CarrierAttempt {
    DeliveryFragment fragment{};
    // Every attempt, including an attempt rejected by a carrier, consumes a fresh ID.
    std::uint64_t attempt{};
    // Once every fragment is known received, request Received/Applied with a
    // small control probe. A direct fragment-only carrier may resend index0.
    bool receipt_probe{};
};

class DeliveryReceiver {
public:
    DeliveryReceiver(const DeliveryReceiver &) = delete;
    DeliveryReceiver &operator=(const DeliveryReceiver &) = delete;
    DeliveryReceiver(DeliveryReceiver &&) = default;
    DeliveryReceiver &operator=(DeliveryReceiver &&) = default;
    static Result<DeliveryReceiver> create(DeliveryMode mode, Epoch epoch,
        DeliveryLimits limits, std::span<DeliverySlot> slots,
        std::span<std::byte> arena, std::uint64_t first_message = 1) noexcept;
    // Caller initializes the shared slot pool once and gives every channel a
    // unique ID in [0,31]. Reservation scans include every channel; mutations
    // and timeouts affect only this channel. Arena/slots remain exclusive to
    // this owner-thread family of channels.
    static Result<DeliveryReceiver> create_shared(DeliveryMode,Epoch,DeliveryLimits,
        std::span<DeliverySlot>,std::span<std::byte>,std::uint16_t channel,
        std::uint64_t first_message=1) noexcept;

    Result<FragmentAdmission> receive(const DeliveryFragment &fragment, Tick now) noexcept;
    Result<DeliveryView> ready() const noexcept;
    Result<DeliveryStage> acknowledgement(std::uint64_t message) const noexcept;
    Result<FragmentReceipt> fragment_acknowledgement(std::uint64_t message) const noexcept;
    Status applied(std::uint64_t message) noexcept;
    Status expire(Tick now) noexcept;

    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::size_t pending_messages() const noexcept;
    [[nodiscard]] std::uint64_t applied_through() const noexcept { return applied_through_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    DeliveryReceiver(DeliveryMode mode, Epoch epoch, DeliveryLimits limits,
        std::span<DeliverySlot> slots, std::span<std::byte> arena,
        std::uint64_t first_message,std::uint16_t channel=0,bool shared=false) noexcept;
    DeliverySlot *find(std::uint64_t message) noexcept;
    Status conflict() noexcept;

    DeliveryMode mode_{};
    Epoch epoch_{};
    DeliveryLimits limits_{};
    std::span<DeliverySlot> slots_{};
    std::span<std::byte> arena_{};
    std::uint64_t applied_through_{};
    std::uint64_t applied_bits_{};
    std::uint64_t latest_admitted_{};
    std::uint64_t seen_bits_{};
    std::uint16_t channel_{};
    bool failed_{};
};

class DeliverySender {
public:
    DeliverySender(const DeliverySender &) = delete;
    DeliverySender &operator=(const DeliverySender &) = delete;
    DeliverySender(DeliverySender &&) = default;
    DeliverySender &operator=(DeliverySender &&) = default;
    static Result<DeliverySender> create(DeliveryMode mode, Epoch epoch,
        DeliveryLimits limits, std::span<DeliverySlot> slots,
        std::span<std::byte> arena, std::uint64_t first_message = 1) noexcept;
    static Result<DeliverySender> create_shared(DeliveryMode,Epoch,DeliveryLimits,
        std::span<DeliverySlot>,std::span<std::byte>,std::uint16_t channel,
        std::uint64_t first_message=1) noexcept;

    // Admission reserves the entire logical message before assigning its sequence.
    Result<std::uint64_t> admit(std::span<const std::byte> payload, Tick now) noexcept;
    Result<CarrierAttempt> next(Tick now) noexcept;
    // accepted means carrier ownership, never remote receipt or application effects.
    Status carrier_result(std::uint64_t attempt, bool accepted, Tick now) noexcept;
    Status receipt(std::uint64_t message, DeliveryStage stage) noexcept;
    Status receipt(std::uint64_t message, DeliveryStage stage, Tick now) noexcept;
    Status fragment_receipt(const FragmentReceipt&,Tick now) noexcept;
    Result<DeliveryStage> stage(std::uint64_t message) const noexcept;
    Status retire(std::uint64_t message) noexcept;
    Status expire(Tick now) noexcept;
    // Retransmission interval in ticks, bounded below by limits.retry_ticks and
    // above by half the progress timeout. Owners derive it from the carrier's
    // measured round trip so a slow or deep-queued link is not resent spuriously.
    Status set_retry_ticks(Tick ticks) noexcept;
    [[nodiscard]] Tick retry_ticks() const noexcept { return retry_ticks_; }

    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::size_t pending_messages() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    DeliverySender(DeliveryMode mode, Epoch epoch, DeliveryLimits limits,
        std::span<DeliverySlot> slots, std::span<std::byte> arena,
        std::uint64_t first_message,std::uint16_t channel=0,bool shared=false) noexcept;
    DeliverySlot *find(std::uint64_t message) noexcept;

    DeliveryMode mode_{};
    Epoch epoch_{};
    DeliveryLimits limits_{};
    Tick retry_ticks_{};
    std::span<DeliverySlot> slots_{};
    std::span<std::byte> arena_{};
    std::uint64_t next_message_{};
    std::uint64_t next_attempt_{1};
    std::uint64_t applied_through_{};
    std::uint64_t applied_bits_{};
    bool sequence_exhausted_{};
    bool attempt_exhausted_{};
    bool failed_{};
    bool attempt_pending_{};
    std::size_t attempt_slot_{};
    std::uint64_t pending_attempt_{};
    std::uint16_t attempt_fragment_{};
    bool attempt_probe_{};
    std::uint16_t channel_{};
};

} // namespace superpos
