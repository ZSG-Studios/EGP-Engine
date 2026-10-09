#include "superpos/recovery.hpp"

#include <limits>

namespace superpos {
namespace {
constexpr std::uint64_t maximum_counter = std::numeric_limits<std::uint64_t>::max();
}

RecoveryModel::RecoveryModel(RecoveryLimits limits, RecoveryPosition durable) noexcept
    : limits_(limits), durable_(durable), tentative_(durable), last_epoch_(durable.epoch) {}

Result<RecoveryModel> RecoveryModel::create(RecoveryLimits limits, RecoveryPosition durable) noexcept {
    if (limits.lease_ticks == 0 || (limits.publication != PublicationMode::Responsive &&
        limits.publication != PublicationMode::Durable)) {
        return fail(Error::InvalidArgument);
    }
    return RecoveryModel(limits, durable);
}

Status RecoveryModel::authorize(PeerId owner, Epoch epoch, Tick now) const noexcept {
    if (now < last_store_time_) {
        return fail(Error::InvalidArgument);
    }
    if (!has_lease_) {
        return fail(Error::RecoveryUnavailable);
    }
    if (epoch != fence_.lease.epoch) {
        return fail(Error::StaleEpoch);
    }
    if (owner != fence_.lease.owner) {
        return fail(Error::PermissionDenied);
    }
    if (now >= fence_.lease.expires_at) {
        return fail(Error::Timeout);
    }
    return {};
}

Result<RecoveryFence> RecoveryModel::acquire(PeerId owner, Tick now) noexcept {
    if (now < last_store_time_) {
        return fail(Error::InvalidArgument);
    }
    if (has_lease_ && now < fence_.lease.expires_at) {
        return fail(Error::Busy);
    }
    if (last_epoch_ == maximum_counter || limits_.lease_ticks > maximum_counter - now) {
        return fail(Error::CounterExhausted);
    }
    ++last_epoch_;
    fence_ = {{last_epoch_, owner, now + limits_.lease_ticks}, durable_};
    has_lease_ = true;
    phase_ = RecoveryPhase::Fenced;
    last_store_time_ = now;
    // Responsive output after the durable prefix is deliberately discarded on takeover.
    tentative_ = durable_;
    return fence_;
}

Status RecoveryModel::restored(const RecoveryFence &fence, Tick now) noexcept {
    if (auto status = authorize(fence.lease.owner, fence.lease.epoch, now); !status) {
        return status;
    }
    if (phase_ != RecoveryPhase::Fenced) {
        return fail(Error::NotReady);
    }
    if (fence.lease.expires_at != fence_.lease.expires_at || fence.final_prefix != fence_.final_prefix) {
        return fail(Error::ProtocolViolation);
    }
    tentative_ = fence.final_prefix;
    phase_ = RecoveryPhase::Active;
    last_store_time_ = now;
    return {};
}

Status RecoveryModel::renew(PeerId owner, Epoch epoch, Tick now) noexcept {
    if (auto status = authorize(owner, epoch, now); !status) {
        return status;
    }
    if (limits_.lease_ticks > maximum_counter - now) {
        return fail(Error::CounterExhausted);
    }
    const auto expires = now + limits_.lease_ticks;
    if (expires < fence_.lease.expires_at) {
        return fail(Error::InvalidArgument);
    }
    fence_.lease.expires_at = expires;
    last_store_time_ = now;
    return {};
}

Result<RecoveryPosition> RecoveryModel::advance(PeerId owner, Epoch epoch, Tick tick,
    std::uint64_t digest, Tick now) noexcept {
    if (auto status = authorize(owner, epoch, now); !status) {
        return fail(status.error());
    }
    if (phase_ == RecoveryPhase::Fenced || phase_ == RecoveryPhase::Unavailable) {
        return fail(Error::NotReady);
    }
    if (phase_ == RecoveryPhase::Paused) {
        return fail(Error::RecoveryUnavailable);
    }
    if (tick < tentative_.tick) {
        return fail(Error::InvalidArgument);
    }
    if (tentative_.sequence == maximum_counter) {
        return fail(Error::CounterExhausted);
    }
    tentative_ = {epoch, tentative_.sequence + 1U, tick, digest};
    last_store_time_ = now;
    return tentative_;
}

Status RecoveryModel::check_commit(PeerId owner, Epoch epoch,
    const RecoveryPosition &prefix, Tick now) const noexcept {
    if (auto status = authorize(owner, epoch, now); !status) {
        return status;
    }
    if (phase_ == RecoveryPhase::Fenced || phase_ == RecoveryPhase::Unavailable) {
        return fail(Error::NotReady);
    }
    if (prefix != tentative_ || prefix.epoch != epoch) {
        return fail(Error::ProtocolViolation);
    }
    if (prefix.sequence < durable_.sequence || prefix.tick < durable_.tick) {
        return fail(Error::ProtocolViolation);
    }
    return {};
}

Status RecoveryModel::committed(PeerId owner, Epoch epoch,
    const RecoveryPosition &prefix, Tick now) noexcept {
    if (auto status = check_commit(owner, epoch, prefix, now); !status) {
        return status;
    }
    durable_ = prefix;
    last_store_time_ = now;
    // Publication is a separate transition: a completed write never publishes implicitly.
    return {};
}

Result<RecoveryPosition> RecoveryModel::publish(PeerId owner, Epoch epoch, Tick now) noexcept {
    if (auto status = authorize(owner, epoch, now); !status) {
        return fail(status.error());
    }
    if (phase_ == RecoveryPhase::Fenced || phase_ == RecoveryPhase::Unavailable) {
        return fail(Error::NotReady);
    }
    const bool available = limits_.publication == PublicationMode::Durable
        ? tentative_.sequence == durable_.sequence
        : tentative_.tick - durable_.tick <= limits_.max_publication_gap_ticks;
    if (!available) {
        phase_ = RecoveryPhase::Paused;
        last_store_time_ = now;
        return fail(Error::RecoveryUnavailable);
    }
    phase_ = RecoveryPhase::Active;
    last_store_time_ = now;
    auto publication = tentative_;
    // Publishing a restored prefix belongs to the newly fenced authority epoch.
    publication.epoch = epoch;
    return publication;
}

OperationLedger::OperationLedger(std::span<OperationReceipt> receipts,
    std::span<OperationWatermark> watermarks) noexcept : receipts_(receipts), watermarks_(watermarks) {}

OperationReceipt *OperationLedger::find(StableOperationId id) noexcept {
    for (auto &receipt : receipts_) {
        if (receipt.occupied && receipt.id == id) {
            return &receipt;
        }
    }
    return nullptr;
}

OperationWatermark *OperationLedger::watermark(PeerId peer, std::uint64_t incarnation) noexcept {
    for (auto &entry : watermarks_) {
        if (entry.occupied && entry.peer == peer && entry.incarnation == incarnation) {
            return &entry;
        }
    }
    return nullptr;
}

Result<OperationLedger> OperationLedger::open(std::span<OperationReceipt> receipts,
    std::span<OperationWatermark> watermarks) noexcept {
    if (receipts.empty() || watermarks.empty()) {
        return fail(Error::InvalidArgument);
    }
    OperationLedger ledger(receipts, watermarks);
    for (std::size_t i = 0; i < watermarks.size(); ++i) {
        if (!watermarks[i].occupied) {
            continue;
        }
        for (std::size_t j = i + 1; j < watermarks.size(); ++j) {
            if (watermarks[j].occupied && watermarks[i].peer == watermarks[j].peer &&
                watermarks[i].incarnation == watermarks[j].incarnation) {
                return fail(Error::ProtocolViolation);
            }
        }
    }
    for (std::size_t i = 0; i < receipts.size(); ++i) {
        if (!receipts[i].occupied) {
            continue;
        }
        const auto &receipt = receipts[i];
        const auto *entry = ledger.watermark(receipt.id.peer, receipt.id.incarnation);
        if (entry == nullptr || receipt.id.sequence == 0 || receipt.id.sequence <= entry->retired_through ||
            receipt.id.sequence - entry->retired_through > 64 ||
            (receipt.state != OperationState::Pending && receipt.state != OperationState::Committed) ||
            (receipt.state == OperationState::Pending && (receipt.effects != 0 || receipt.outcome_digest != 0)) ||
            (receipt.state == OperationState::Committed && receipt.effects > entry->effects)) {
            return fail(Error::ProtocolViolation);
        }
        for (std::size_t j = i + 1; j < receipts.size(); ++j) {
            if (receipts[j].occupied && receipts[j].id == receipt.id) {
                return fail(Error::ProtocolViolation);
            }
        }
    }
    for (const auto &entry : watermarks) {
        if (!entry.occupied) {
            continue;
        }
        std::uint64_t retained_effects = 0;
        for (const auto &receipt : receipts) {
            if (receipt.occupied && receipt.state == OperationState::Committed &&
                receipt.id.peer == entry.peer && receipt.id.incarnation == entry.incarnation) {
                if (receipt.effects > maximum_counter - retained_effects) {
                    return fail(Error::ProtocolViolation);
                }
                retained_effects += receipt.effects;
            }
        }
        if (retained_effects > entry.effects) {
            return fail(Error::ProtocolViolation);
        }
    }
    return ledger;
}

Status OperationLedger::register_peer(PeerId peer, std::uint64_t incarnation) noexcept {
    if (watermark(peer, incarnation) != nullptr) {
        return {};
    }
    for (auto &entry : watermarks_) {
        if (!entry.occupied) {
            entry = {true, peer, incarnation, 0, 0};
            return {};
        }
    }
    return fail(Error::CapacityExceeded);
}

Result<OperationAdmission> OperationLedger::begin(StableOperationId id,
    std::uint64_t request_digest) noexcept {
    auto *entry = watermark(id.peer, id.incarnation);
    if (entry == nullptr) {
        return fail(Error::StaleGeneration);
    }
    if (id.sequence == 0) {
        return fail(Error::InvalidArgument);
    }
    if (id.sequence <= entry->retired_through) {
        return fail(Error::UnknownOutcome);
    }
    if (id.sequence - entry->retired_through > 64) {
        return fail(Error::CapacityExceeded);
    }
    if (auto *receipt = find(id); receipt != nullptr) {
        if (receipt->request_digest != request_digest) {
            return fail(Error::ProtocolViolation);
        }
        if (receipt->state == OperationState::Pending) {
            return fail(Error::Busy);
        }
        return OperationAdmission{false, receipt->outcome_digest};
    }
    for (auto &receipt : receipts_) {
        if (!receipt.occupied) {
            receipt = {true, id, request_digest, 0, 0, OperationState::Pending};
            return OperationAdmission{true, 0};
        }
    }
    return fail(Error::CapacityExceeded);
}

Result<OperationReceipt> OperationLedger::commit(StableOperationId id,
    std::uint64_t request_digest, std::uint64_t outcome_digest, std::uint64_t effects) noexcept {
    auto *entry = watermark(id.peer, id.incarnation);
    if (entry == nullptr) {
        return fail(Error::StaleGeneration);
    }
    if (id.sequence != 0 && id.sequence <= entry->retired_through) {
        return fail(Error::UnknownOutcome);
    }
    auto *receipt = find(id);
    if (receipt == nullptr) {
        return fail(Error::NotReady);
    }
    if (receipt->request_digest != request_digest) {
        return fail(Error::ProtocolViolation);
    }
    if (receipt->state == OperationState::Committed) {
        if (receipt->outcome_digest != outcome_digest || receipt->effects != effects) {
            return fail(Error::ProtocolViolation);
        }
        return *receipt;
    }
    if (effects > maximum_counter - entry->effects) {
        return fail(Error::CounterExhausted);
    }
    receipt->outcome_digest = outcome_digest;
    receipt->effects = effects;
    receipt->state = OperationState::Committed;
    entry->effects += effects;
    return *receipt;
}

Status OperationLedger::retire(PeerId peer, std::uint64_t incarnation,
    std::uint64_t through) noexcept {
    auto *entry = watermark(peer, incarnation);
    if (entry == nullptr) {
        return fail(Error::StaleGeneration);
    }
    if (through < entry->retired_through) {
        return fail(Error::InvalidArgument);
    }
    if (through - entry->retired_through > 64) {
        return fail(Error::CapacityExceeded);
    }
    const auto count = through - entry->retired_through;
    for (std::uint64_t distance = 1; distance <= count; ++distance) {
        const auto *receipt = find({peer, incarnation, entry->retired_through + distance});
        if (receipt == nullptr || receipt->state != OperationState::Committed) {
            return fail(Error::NotReady);
        }
    }
    for (auto &receipt : receipts_) {
        if (receipt.occupied && receipt.id.peer == peer && receipt.id.incarnation == incarnation &&
            receipt.id.sequence <= through) {
            receipt = {};
        }
    }
    entry->retired_through = through;
    return {};
}

std::uint64_t OperationLedger::effects(PeerId peer, std::uint64_t incarnation) const noexcept {
    for (const auto &entry : watermarks_) {
        if (entry.occupied && entry.peer == peer && entry.incarnation == incarnation) {
            return entry.effects;
        }
    }
    return 0;
}

std::size_t OperationLedger::occupied_receipts() const noexcept {
    std::size_t count = 0;
    for (const auto &receipt : receipts_) {
        count += receipt.occupied ? 1U : 0U;
    }
    return count;
}

Status commit_recovery_operation(RecoveryModel &model, OperationLedger &ledger,
    PeerId owner, Epoch epoch, const RecoveryPosition &prefix, Tick now,
    StableOperationId operation, std::uint64_t request_digest,
    std::uint64_t outcome_digest, std::uint64_t effects) noexcept {
    if (auto status = model.check_commit(owner, epoch, prefix, now); !status) {
        return status;
    }
    if (auto receipt = ledger.commit(operation, request_digest, outcome_digest, effects); !receipt) {
        return fail(receipt.error());
    }
    model.durable_ = prefix;
    model.last_store_time_ = now;
    return {};
}

Status restore_recovery_operations(RecoveryModel &model, OperationLedger &ledger,
    const RecoveryFence &fence, Tick now) noexcept {
    if (auto status = model.restored(fence, now); !status) {
        return status;
    }
    for (auto &receipt : ledger.receipts_) {
        if (receipt.occupied && receipt.state == OperationState::Pending) {
            receipt = {};
        }
    }
    return {};
}

} // namespace superpos
