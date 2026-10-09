// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/checkpoint.hpp"

namespace superpos {
enum class CheckpointServiceTable : std::uint8_t {
    Actors, PendingOperations, Results, Outbox, ControlCommits, Policies,
    Transitions, ExecutionLease, AuthorityGrant
};
enum class CheckpointCellKind : std::uint8_t { Null, Bytes, Unsigned };
// Views borrow one immutable encoded page. They never own storage or authorize
// SQL, effects, authority, or gameplay callbacks.
struct CheckpointCellView {
    CheckpointCellKind kind{};
    std::span<const std::byte> bytes{};
    std::uint64_t number{};
};
struct CheckpointServiceRowView {
    std::array<CheckpointCellView,8> cells{};
    std::uint8_t count{};
};
struct CheckpointServicePageView {
    CheckpointServiceTable table{};
    bool table_complete{};
    std::uint8_t count{};
    std::array<CheckpointServiceRowView,64> rows{};
};
Result<CheckpointServicePageView> decode_checkpoint_service_page(
    std::span<const std::byte>) noexcept;

struct CheckpointServiceInspection {
    CheckpointTicket ticket{};
    std::uint64_t bytes{},rows{};
    std::uint32_t pages{},tables{};
};
// Structural coverage of pages read from a trusted RecoveryVerified receipt.
// Does NOT revalidate its digest, cross-table semantics or source authenticity.
// In particular, historical grants/leases are data, never restored authority.
class CheckpointServiceInspector {
    CheckpointRecoveryReceipt expected_{};
    CheckpointServiceInspection progress_{};
    std::array<std::uint64_t,4> previous_key_{};
    bool has_key_{};
    explicit CheckpointServiceInspector(const CheckpointRecoveryReceipt&) noexcept;
public:
    static Result<CheckpointServiceInspector> create(const CheckpointRecoveryReceipt&) noexcept;
    // Exact ordinal required; failed admission leaves all progress unchanged.
    Status accept(std::uint32_t ordinal,std::span<const std::byte>) noexcept;
    Result<CheckpointServiceInspection> finish() const noexcept;
    CheckpointServiceInspection progress() const noexcept { return progress_; }
};

// Integrity verification against an independently trusted recovery receipt.
// The caller authenticates that receipt; a peer-supplied root is not trust.
// Includes structural inspection, the original boot/anchor seed, ordered page
// hashes and final totals/content binding. It does not validate nested policy
// codecs, cross-table meaning, participant bytes or current authority.
// Owner-thread only; do not move/assign the object during an operation. The
// borrowed digest must outlive it. Digest callbacks cannot synchronously
// reenter admission/completion. Input bytes stay immutable during accept;
// no page spans are retained.
class CheckpointServiceVerifier {
    CheckpointRecoveryReceipt expected_{};
    CheckpointServiceInspector inspector_;
    CryptographicDigest* digest_{};
    Fingerprint chain_{};
    bool busy_{};
    CheckpointServiceVerifier(const CheckpointRecoveryReceipt&,
        const CheckpointServiceInspector&,CryptographicDigest&,Fingerprint) noexcept;
public:
    static Result<CheckpointServiceVerifier> create(const CheckpointRecoveryReceipt&,CryptographicDigest&) noexcept;
    Status accept(std::uint32_t ordinal,std::span<const std::byte>) noexcept;
    // Success authenticates these service bytes relative to the trusted root,
    // never permission to install them or to publish a recovered game world.
    Result<CheckpointServiceInspection> finish() noexcept;
    CheckpointServiceInspection progress() const noexcept { return inspector_.progress(); }
};

// A trusted native host codec consumes these into private fallible staging.
// No publication API exists here. This declaration does not implement fencing,
// post-state replay, physics restoration, or transactional engine publication.
struct HostRestorePlan {
    CheckpointRecoveryReceipt checkpoint{};
    Epoch final_epoch{},successor_epoch{};
    std::uint64_t final_sequence{};
    Tick final_tick{};
    Fingerprint final_state{};
};
struct HostRestoreStagingTicket {
    std::uint64_t generation{},incarnation{};
    bool operator==(const HostRestoreStagingTicket&) const noexcept=default;
};
class HostRestoreStaging {
public:
    virtual ~HostRestoreStaging()=default;
    // Plan must come from a trusted coordinator-fenced final-C proof, not merely
    // decoded network fields. Stage all participants before any visible change.
    virtual Result<HostRestoreStagingTicket> prepare(const HostRestorePlan&) noexcept=0;
    virtual Status checkpoint_chunk(HostRestoreStagingTicket,std::uint32_t,
        std::span<const std::byte>) noexcept=0;
    virtual Status service_page(HostRestoreStagingTicket,std::uint32_t,
        std::span<const std::byte>) noexcept=0;
    // Complete lossless poststate plus envelope; input-only replay is not implied.
    virtual Status poststate(HostRestoreStagingTicket,Epoch,std::uint64_t,Tick,
        std::span<const std::byte> state,std::span<const std::byte> envelope) noexcept=0;
    // Verify exact final state/participant coverage through C. Success means
    // prepared only, never authority, delivery, commit, or visible publication.
    virtual Status seal(HostRestoreStagingTicket) noexcept=0;
    // Must detach/release staging without historical gameplay/effect callbacks.
    virtual Status abort(HostRestoreStagingTicket) noexcept=0;
};
}
