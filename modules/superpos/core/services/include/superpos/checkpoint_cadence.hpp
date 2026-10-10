// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/sqlite_journal.hpp"
#include <optional>

namespace superpos {
// Responsive-recovery cadence (plan: checkpoint every two seconds, retain two
// verified checkpoints and one staging operation, keep published tick P within
// 15 ticks of verified recoverable R at 60 Hz, pause protected publication
// when that bound is lost, report the P-to-R rollback on promotion). Defaults
// are in ticks at 60 Hz. Five seconds of contiguous coverage is enforced by
// storage compaction (JournalConfig::minimum_coverage_ticks).
struct CheckpointCadenceConfig {
    std::uint32_t checkpoint_interval_ticks{120};
    std::uint32_t maximum_publication_lag_ticks{15};
    bool operator==(const CheckpointCadenceConfig&) const noexcept=default;
};
// One retained or staging checkpoint as storage reported it.
struct CadenceCheckpoint {
    CheckpointTicket ticket{};
    Epoch epoch{};
    std::uint64_t sequence{};
    Tick tick{};
    bool operator==(const CadenceCheckpoint&) const noexcept=default;
};
// What the owner should do next, in priority order: finish the staging
// candidate (verify it while fewer than two bases exist, otherwise replace
// the oldest base), compact after a replacement, or capture a new candidate.
enum class CadenceAction : std::uint8_t { None,Capture,Finish,Compact };
struct CadenceStatus {
    Epoch epoch{};
    // R: the durable committed prefix, recoverable only while a verified base
    // lies at or before it. Absent means nothing is recoverable yet.
    std::optional<JournalPrefix> recoverable{};
    std::optional<Tick> published{};
    std::optional<CadenceCheckpoint> oldest{},newest{},candidate{};
    bool paused{},overdue{},compaction_pending{};
    std::uint64_t pauses{},publications{},captures{},bases_installed{},replacements{};
    Tick worst_lag{};
};
struct CadencePromotion {
    Epoch previous{},successor{};
    std::optional<Tick> published{};
    JournalPrefix recoverable{};
    std::uint64_t rolled_back_ticks{};
};
// Deterministic owner-thread policy; no clocks, IO or allocation. The owner
// feeds it storage results (committed prefixes, candidate/base/replacement
// and compaction receipts) and asks it before publishing each protected tick.
// It never authorizes a write, lease or restore by itself.
class CheckpointCadence {
public:
    static Result<CheckpointCadence> create(CheckpointCadenceConfig={},Epoch epoch=1) noexcept;
    // Durable committed prefix C of the current epoch; never moves backwards.
    Status committed(const JournalPrefix&) noexcept;
    // One staging candidate at a time (Busy), captured at the committed prefix.
    Status candidate(const CadenceCheckpoint&) noexcept;
    Status abandoned(CheckpointTicket) noexcept;
    // A recovery-verified base: the candidate promoted while fewer than two
    // bases existed, or (without a candidate) an existing base being seeded.
    Status verified(const CadenceCheckpoint&) noexcept;
    // A committed replacement of the oldest base by the current candidate.
    Status replaced(CheckpointTicket candidate,CheckpointTicket retired) noexcept;
    Status compacted(const JournalCompactionReceipt&) noexcept;
    // Protected publication gate. True admits P (nondecreasing); false means
    // publication is paused because P would exceed R+lag or nothing is
    // recoverable. Pausing clears itself once R catches up.
    Result<bool> publish(Tick) noexcept;
    CadenceAction next() const noexcept;
    CadenceStatus status() const noexcept;
    // Promotion of a successor that restored through C (restored prefix) under
    // a strictly newer epoch: reports the P-to-R rollback, drops the staging
    // candidate, keeps verified bases and restarts publication at C.
    Result<CadencePromotion> promote(Epoch successor,const JournalPrefix& restored) noexcept;
private:
    CheckpointCadenceConfig config_{};
    CadenceStatus state_{};
    std::optional<JournalPrefix> committed_{};
    std::optional<CadenceCheckpoint> bases_[2]{};
    void refresh() noexcept;
};
}
