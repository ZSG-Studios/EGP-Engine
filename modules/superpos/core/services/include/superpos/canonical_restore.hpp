// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/host_restore.hpp"
#include "superpos/service/coordinator.hpp"

namespace superpos::service {
enum class CanonicalRestorePhase : std::uint8_t {
    Idle,Planning,Checkpoint,Service,Records,Finalizing,Sealed,Failed,Aborted
};
struct CanonicalRestoreProgress {
    CanonicalRestorePhase phase{};
    std::uint32_t chunks{},pages{};
    std::uint64_t records{};
};
// Owner-thread restore orchestration over the storage worker queue. Every
// SQLite read runs on the worker as a queued job; this owner only compares
// staged evidence. The capability scope yields one storage-derived plan
// (verified checkpoint, decoded anchor, canonical chain through fenced C).
// Checkpoint chunks, service pages and each committed record are then read
// one job at a time and staged once. The private HostRestoreAuthority
// accepts only the exact staged plan and record bytes; unexpected callbacks
// fail closed. Records must decode as canonical envelopes continuing the
// anchor chain. Before seal a fresh storage revalidation must reproduce the
// same plan and final claim. Participants receive complete canonical
// envelopes (checkpoint content and records) and must reconstruct state.
//
// The capability expires with its grant lease. Every step, staging callback
// and seal first observes the coordinator's ClockSource/ContinuityGuard
// (same owner thread): a lapsed lease (Timeout) or a continuity change
// (StaleGeneration) fails closed and discards private staging. A renewal of
// the same grant lineage may be adopted with renew().
//
// One job is outstanding at a time; poll() never blocks and returns Busy
// while it runs. abort() and destruction are immediate and safe mid-job: the
// outstanding ticket is abandoned, so the executor discards its completion
// and frees the slot when the worker finishes it. The queue, participant,
// digest, clock source and guard are borrowed and must outlive this object.
// Sealing grants no lease, execution permit or publication.
class CanonicalRestore {
public:
    virtual ~CanonicalRestore()=default;
    virtual Status start() noexcept=0;
    virtual Result<CanonicalRestoreProgress> poll() noexcept=0;
    virtual Result<HostRestorePlan> sealed_plan() const noexcept=0;
    virtual Status renew(const RestoreReadCapability&) noexcept=0;
    virtual Status abort() noexcept=0;
};
Result<AllocatedOwner<CanonicalRestore>> create_canonical_restore(Allocator&,JournalExecutor&,
    const RestoreReadCapability&,HostRestoreParticipant&,CryptographicDigest&,
    ClockSource&,ContinuityGuard&,HostRestoreConfig={}) noexcept;
}
