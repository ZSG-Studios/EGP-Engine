#pragma once

#include "superpos/result.hpp"
#include "superpos/types.hpp"

#include <cstdint>
#include <span>

namespace superpos {

class OperationLedger;
struct StableOperationId;

enum class PublicationMode : std::uint8_t { Responsive, Durable };
enum class RecoveryPhase : std::uint8_t { Unavailable, Fenced, Active, Paused };

struct RecoveryLimits {
    Tick lease_ticks{60};
    Tick max_publication_gap_ticks{15};
    PublicationMode publication{PublicationMode::Responsive};
};

struct RecoveryPosition {
    Epoch epoch{};
    std::uint64_t sequence{};
    Tick tick{};
    std::uint64_t digest{};
    friend bool operator==(const RecoveryPosition &, const RecoveryPosition &) = default;
};

struct RecoveryLease {
    Epoch epoch{};
    PeerId owner{};
    Tick expires_at{};
};

struct RecoveryFence {
    RecoveryLease lease{};
    // Frozen complete committed prefix; old-epoch writes cannot extend it after fencing.
    RecoveryPosition final_prefix{};
};

// Deterministic store/authority reference model, not a disk or distributed lease provider.
class RecoveryModel {
public:
    static Result<RecoveryModel> create(RecoveryLimits limits,
        RecoveryPosition durable_prefix = {}) noexcept;

    Result<RecoveryFence> acquire(PeerId owner, Tick now) noexcept;
    Status restored(const RecoveryFence &fence, Tick now) noexcept;
    Status renew(PeerId owner, Epoch epoch, Tick now) noexcept;
    Result<RecoveryPosition> advance(PeerId owner, Epoch epoch, Tick tick,
        std::uint64_t digest, Tick now) noexcept;
    // Models a transaction committing the complete current prefix and its receipts/effects.
    Status committed(PeerId owner, Epoch epoch, const RecoveryPosition &prefix, Tick now) noexcept;
    Result<RecoveryPosition> publish(PeerId owner, Epoch epoch, Tick now) noexcept;

    [[nodiscard]] RecoveryPosition durable_prefix() const noexcept { return durable_; }
    [[nodiscard]] RecoveryPosition tentative_prefix() const noexcept { return tentative_; }
    [[nodiscard]] RecoveryPhase phase() const noexcept { return phase_; }

private:
    friend Status commit_recovery_operation(RecoveryModel &, OperationLedger &, PeerId,
        Epoch, const RecoveryPosition &, Tick, StableOperationId,
        std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
    explicit RecoveryModel(RecoveryLimits limits, RecoveryPosition durable) noexcept;
    Status authorize(PeerId owner, Epoch epoch, Tick now) const noexcept;
    Status check_commit(PeerId owner, Epoch epoch, const RecoveryPosition &prefix, Tick now) const noexcept;

    RecoveryLimits limits_{};
    RecoveryPosition durable_{};
    RecoveryPosition tentative_{};
    RecoveryFence fence_{};
    Epoch last_epoch_{};
    Tick last_store_time_{};
    bool has_lease_{};
    RecoveryPhase phase_{RecoveryPhase::Unavailable};
};

struct StableOperationId {
    PeerId peer{};
    std::uint64_t incarnation{};
    std::uint64_t sequence{};
    friend bool operator==(const StableOperationId &, const StableOperationId &) = default;
};

enum class OperationState : std::uint8_t { Pending, Committed };

struct OperationReceipt {
    bool occupied{};
    StableOperationId id{};
    std::uint64_t request_digest{};
    std::uint64_t outcome_digest{};
    std::uint64_t effects{};
    OperationState state{OperationState::Pending};
};

struct OperationWatermark {
    bool occupied{};
    PeerId peer{};
    std::uint64_t incarnation{};
    std::uint64_t retired_through{};
    std::uint64_t effects{};
};

struct OperationAdmission {
    bool execute{};
    std::uint64_t outcome_digest{};
};

// Caller storage is portable recovery state. Committing receipt and effects is one
// model transition; real providers must put both in the same durable transaction.
class OperationLedger {
public:
    static Result<OperationLedger> open(std::span<OperationReceipt> receipts,
        std::span<OperationWatermark> watermarks) noexcept;

    Status register_peer(PeerId peer, std::uint64_t incarnation) noexcept;
    Result<OperationAdmission> begin(StableOperationId id, std::uint64_t request_digest) noexcept;
    Result<OperationReceipt> commit(StableOperationId id, std::uint64_t request_digest,
        std::uint64_t outcome_digest, std::uint64_t effects) noexcept;
    // Only a complete committed prefix can be retired; old IDs never execute again.
    Status retire(PeerId peer, std::uint64_t incarnation, std::uint64_t through) noexcept;
    [[nodiscard]] std::uint64_t effects(PeerId peer, std::uint64_t incarnation) const noexcept;
    [[nodiscard]] std::size_t occupied_receipts() const noexcept;

private:
    friend Status restore_recovery_operations(RecoveryModel &, OperationLedger &,
        const RecoveryFence &, Tick) noexcept;
    OperationLedger(std::span<OperationReceipt> receipts,
        std::span<OperationWatermark> watermarks) noexcept;
    OperationReceipt *find(StableOperationId id) noexcept;
    OperationWatermark *watermark(PeerId peer, std::uint64_t incarnation) noexcept;

    std::span<OperationReceipt> receipts_{};
    std::span<OperationWatermark> watermarks_{};
};

// Atomic reference transition: validate fence and prefix, commit dedupe receipt/effect,
// then publish the durable prefix. No fallible operation follows the ledger transition.
Status commit_recovery_operation(RecoveryModel &model, OperationLedger &ledger,
    PeerId owner, Epoch epoch, const RecoveryPosition &prefix, Tick now,
    StableOperationId operation, std::uint64_t request_digest,
    std::uint64_t outcome_digest, std::uint64_t effects) noexcept;

// After the exact durable fence is restored, uncommitted reservations carry no
// effects and can be retried. Committed receipts and retired watermarks survive.
Status restore_recovery_operations(RecoveryModel &model, OperationLedger &ledger,
    const RecoveryFence &fence, Tick now) noexcept;

} // namespace superpos
