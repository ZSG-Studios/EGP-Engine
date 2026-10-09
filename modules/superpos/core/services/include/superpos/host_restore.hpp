// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/allocated_owner.hpp"
#include "superpos/checkpoint_service_view.hpp"

namespace superpos {
struct HostRestoreCapabilities {
    Fingerprint schemas{},simulation{},participants{};
    RecoveryGrade grade{RecoveryGrade::None};
    std::uint64_t maximum_checkpoint_bytes{};
    std::uint32_t maximum_record_bytes{},maximum_journal_records{};
    bool cross_epoch{};
};
// Trusted aggregate codec for ALL required participants. Every mutation is to
// isolated private state; no scene/effect callbacks or live publication. Use the
// supplied allocator for owned staging; do not retain input spans. discard releases all prepare resources,
// including partial prepare failures, without throwing or invoking gameplay.
class HostRestoreParticipant {
public:
    virtual ~HostRestoreParticipant()=default;
    virtual HostRestoreCapabilities capabilities() const noexcept=0;
    virtual Status prepare(const HostRestorePlan&,Allocator&) noexcept=0;
    virtual Status checkpoint_chunk(std::uint32_t,std::span<const std::byte>) noexcept=0;
    virtual Status poststate(Epoch,std::uint64_t,Tick,std::span<const std::byte>,std::span<const std::byte>) noexcept=0;
    // Must semantically validate complete participant coverage before returning
    // the canonical final-state digest; a hash alone is not semantic validation.
    virtual Result<Fingerprint> seal() noexcept=0;
    virtual void discard() noexcept=0;
};
// Trusted bridge to the retained coordinator ordering domain. The application
// must implement real fenced-prefix and exact committed-record verification;
// declarations or caller-supplied hashes do not implement these checks.
class HostRestoreAuthority {
public:
    virtual ~HostRestoreAuthority()=default;
    virtual Status validate_plan(const HostRestorePlan&) noexcept=0;
    virtual Status validate_record(const HostRestorePlan&,Epoch,std::uint64_t,Tick,
        std::span<const std::byte>,std::span<const std::byte>) noexcept=0;
};
struct HostRestoreConfig {
    std::uint64_t maximum_checkpoint_bytes{checkpoint_maximum_bytes};
    std::uint32_t maximum_record_bytes{1024*1024},maximum_journal_records{65536};
};
// Allocates one stable owner and a bounded input-copy buffer up front. Borrowed
// providers/allocator outlive it. All access/destruction belongs to its creating
// thread; never reset/move the AllocatedOwner from provider callbacks. No service
// pages reach the participant and no database or live engine state is modified.
// Invalid/foreign tickets and reentry deny access; a failure on an admitted
// active stream discards it exactly once. Successful seal remains private until
// abort/destruction; it grants no authority and offers no publication API.
Result<AllocatedOwner<HostRestoreStaging>> create_host_restore_staging(Allocator&,
    HostRestoreParticipant&,HostRestoreAuthority&,CryptographicDigest&,HostRestoreConfig={}) noexcept;
}
