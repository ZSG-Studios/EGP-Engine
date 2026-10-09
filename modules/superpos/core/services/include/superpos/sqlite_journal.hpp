#pragma once
#include "superpos/allocator.hpp"
#include "superpos/types.hpp"
#include "superpos/lease.hpp"
#include "superpos/checkpoint.hpp"
#include "superpos/canonical_state.hpp"
#include "superpos/checkpoint_service_view.hpp"
#include <span>
#include <optional>

namespace superpos {
namespace service::control { class IssuerIdentity; class ExecutionLease; class ExecutionPermit; struct PolicyChange; struct PolicyReceipt; }
enum class StorageIo : std::uint8_t { Write, Sync, Truncate };
class StorageFaultInjector {
public:
    virtual ~StorageFaultInjector()=default;
    // Trusted fixture instrumentation only; no network input selects a hook.
    // It must outlive the journal, remain nonblocking and never throw.
    virtual bool reject(StorageIo, bool wal) noexcept=0;
};
struct JournalConfig {
    std::uint64_t match{};
    std::uint32_t maximum_record_bytes{1024 * 1024};
    std::uint64_t retention_bytes{1024ULL * 1024 * 1024};
    std::uint64_t wal_high_water_bytes{256ULL * 1024 * 1024};
    StorageFaultInjector* fault_injector{};
    struct OperationLimits {
        std::uint32_t maximum_actors{16384},maximum_pending{16384};
        std::uint32_t pending_bytes{64*1024*1024},peer_request_bytes{64*1024};
        std::uint32_t result_cache_bytes{8*1024*1024};
        std::uint32_t maximum_effects{2048},outbox_bytes{10*1024*1024};
        bool operator==(const OperationLimits&) const noexcept=default;
    } operations{};
};
enum class OperationActorKind : std::uint8_t { Player,Authority,Service };
struct DurableOperationId {
    std::uint64_t match{},actor{},actor_epoch{},sequence{};
    OperationActorKind kind{OperationActorKind::Player};
    bool operator==(const DurableOperationId&) const noexcept=default;
};
enum class DurableOperationState : std::uint8_t { Execute,Pending,Committed,Retired };
struct DurableOperationReceipt {
    DurableOperationState state{};
    std::uint32_t result_bytes{};
};
struct DurableDecision {
    DurableOperationId id{};
    std::span<const std::byte> request{},result{};
};
struct DurableEffect {
    std::uint64_t recipient{};
    std::span<const std::byte> payload{};
};
struct DurableEffectReceipt {
    std::uint64_t match{},append_id{},sequence{},recipient{};
    std::uint32_t ordinal{},bytes{};
};
struct PendingOperation {
    DurableOperationId id{};
    PeerId peer{};
    std::uint16_t request_bytes{};
    std::array<std::byte,4096> request{};
};
struct JournalReceipt {
    std::uint64_t append_id{}, epoch{}, sequence{}, tick{};
    std::uint32_t bytes{};
    bool duplicate{};
};
// Storage-only coordinator incarnation. Nonces are fresh trusted CSPRNG values.
// This receipt does not authenticate a grant or authorize any journal writer.
struct CoordinatorBootReceipt {
    std::uint64_t term{};
    std::array<std::byte,32> nonce{};
    bool duplicate{};
};
struct CoordinatorBootIdentity {
    std::uint64_t term{};
    std::array<std::byte,32> nonce{};
    bool operator==(const CoordinatorBootIdentity&) const noexcept=default;
};
struct JournalPrefix {
    std::uint64_t epoch{}, sequence{}, tick{}, retained_bytes{};
};
struct CommittedJournalRecord {
    JournalPrefix snapshot{};
    JournalReceipt receipt{};
    std::uint32_t envelope_bytes{};
};
struct CanonicalJournalProof {
    JournalPrefix snapshot{};
    CanonicalStateHeader final_state{};
    std::uint64_t records{};
};
// Storage proposal from the trusted coordinator, after authenticated eligibility
// and lease/takeover checks. This storage type is never a network admission proof.
struct DurableAuthorityProposal {
    std::optional<std::uint64_t> expected_grant_sequence{};
    Epoch expected_epoch{};
    AuthorityGrant grant{};
};
struct DurableAuthorityReceipt {
    DurableAuthorityProposal proposal{};
    CoordinatorBootIdentity boot{};
    // Exact prefix at the original authority transaction, stable across retries.
    JournalPrefix fenced_prefix{};
    bool duplicate{};
};
// Exact scope of one private restore read, copied from a coordinator-minted
// capability. The store compares it with its own committed state; it is never
// accepted from a peer and grants no execution, lease or publication right.
struct CanonicalRestoreScope {
    DurableAuthorityReceipt authority{};
    CheckpointTicket checkpoint{};
    std::uint32_t maximum_records{};
};
// Storage-derived restore inputs. Every field comes from this store's snapshot:
// the RecoveryVerified checkpoint, the anchor decoded from its stored content
// and the canonical chain through the fenced prefix C.
struct CanonicalRestoreEvidence {
    HostRestorePlan plan{};
    CanonicalStateHeader anchor{},final_state{};
    JournalPrefix snapshot{};
};
// Service-owned synchronous SQLite ordering domain. Run exclusively on a bounded
// storage executor, never a simulation/IO thread. SQLite's allocations are service
// memory and are measured separately from portable core allocation budgets.
// A successful append is Committed; an IO error may have UnknownOutcome and must
// be queried/retried with the same stable append ID. This is not a lease/election.
class SqliteJournal {
    struct Impl;
    Impl* impl_{};
    explicit SqliteJournal(Impl*) noexcept;
    Result<JournalReceipt> append_bundle(Epoch,std::uint64_t,Tick,
        std::span<const std::byte>,std::span<const DurableDecision>,
        std::span<const DurableEffect>,const CanonicalStateClaim*) noexcept;
public:
    SqliteJournal(const SqliteJournal&)=delete;
    SqliteJournal& operator=(const SqliteJournal&)=delete;
    SqliteJournal(SqliteJournal&&) noexcept;
    SqliteJournal& operator=(SqliteJournal&&) noexcept;
    ~SqliteJournal();
    static Result<SqliteJournal> open(Allocator&, const char* file, JournalConfig) noexcept;
    // Observational configuration identity; not admission or authority proof.
    Result<std::uint64_t> match_identity() const noexcept;
    Result<JournalPrefix> prefix() noexcept;
    // Global to this database, not per match. Absent state is explicit; first
    // term is zero. Observe then compare-and-advance in the same FULL/WAL domain.
    // Retry an uncertain commit with the exact expected term and boot nonce.
    // Older coordinators must query/revalidate; this is not lease continuity.
    Result<std::optional<CoordinatorBootReceipt>> coordinator_boot() noexcept;
    Result<CoordinatorBootReceipt> begin_coordinator_boot(
        std::optional<std::uint64_t> expected, std::array<std::byte,32> nonce) noexcept;
    // Trusted storage identity only, not authentication or a lease. Bind once on
    // the store owner; identical rebinding is allowed, changing identity is not.
    // Mutations compare the persisted singleton in their own write transaction.
    // Unbound stores may mutate only before any boot singleton exists.
    Status bind_coordinator_boot(CoordinatorBootIdentity) noexcept;
    // Observe the latest stored proposal only. No authentication, fresh durability
    // confirmation, lease validity or completed restore is established by query.
    Result<std::optional<DurableAuthorityReceipt>> authority_grant() noexcept;
    // Atomically persist a grant and its journal epoch fence under the bound boot.
    // Sequence starts at zero, advances exactly once and never wraps. Exact retry
    // reconfirms FULL/WAL durability; an old/conflicting proposal cannot execute.
    // Renewals retain epoch/owner/kind/membership/scope and increase request ID.
    // Takeover advances epoch by one. Caller proves TTL+guard expiry and eligible
    // membership before invoking; the store has no clocks or signing credentials.
    Result<DurableAuthorityReceipt> commit_authority(DurableAuthorityProposal) noexcept;
    // Epoch transition and appends use BEGIN IMMEDIATE on the same database.
    // Persist before granting authority; no clock/lease authority is implied.
    Result<JournalPrefix> fence(Epoch expected, Epoch replacement) noexcept;
    Result<JournalReceipt> append(Epoch, std::uint64_t append_id, Tick,
        std::span<const std::byte> canonical_post_state) noexcept;
    // Duplicate retries and durable read receipts reconfirm FULL/WAL durability
    // with a bounded per-match counter. A failed confirmation publishes no
    // query/readback output. These storage reads may write WAL and fail under
    // storage pressure; confirmation does not grant or renew authority. This
    // adds bounded WAL IO even for duplicates and authoritative empty results.
    // Caller supplies already authenticated actor/peer authority. Admission does
    // not execute gameplay. New actor epochs require all old pending decisions
    // to be resolved. A 64-sequence window admits reordered requests from the
    // declared first value; retired prefixes never skip sequence holes.
    Status register_actor(Epoch authority,OperationActorKind,std::uint64_t actor,
        std::uint64_t actor_epoch,PeerId peer,std::uint64_t first_sequence=0) noexcept;
    Result<DurableOperationReceipt> begin_operation(Epoch authority,DurableOperationId,
        std::span<const std::byte> request,std::span<std::byte> result) noexcept;
    Result<DurableOperationReceipt> query_operation(DurableOperationId,
        std::span<const std::byte> request,std::span<std::byte> result) noexcept;
    // Bounded recovery/executor readback only. A queued request is not permission
    // to execute: the host must restore the committed prefix, hold current lease
    // authority and revalidate ownership when dispatching it. Max64 per page.
    Result<std::size_t> pending_operations(Epoch authority,
        std::span<PendingOperation>,std::optional<DurableOperationId> after={}) noexcept;
    // State, terminal decisions/retirement watermarks and effect outbox become
    // visible in the same append/fence transaction. Max64 decisions/effects;
    // each request/result/effect <=4KiB. Retry the exact stable append envelope.
    Result<JournalReceipt> append_decisions(Epoch,std::uint64_t append_id,Tick,
        std::span<const std::byte> canonical_post_state,
        std::span<const DurableDecision>,std::span<const DurableEffect>) noexcept;
    // Trusted producer claims only; no semantic state validation or authority
    // is granted. Position and predecessor continuity bind inside one write
    // transaction. Bootstrap requires FullRecord; opaque heads cannot continue.
    // Header, payload and decisions/effects share the maximum-record budget.
    Result<JournalReceipt> append_canonical(Epoch,std::uint64_t append_id,Tick,
        CanonicalStateClaim,std::span<const std::byte> payload,
        std::span<const DurableDecision> decisions={},
        std::span<const DurableEffect> effects={}) noexcept;
    Result<DurableEffectReceipt> next_effect(std::span<std::byte> payload) noexcept;
    // Only an authenticated recipient's durable/idempotent acknowledgement may
    // call this. Delivery remains at least once while acknowledgement is absent.
    Status acknowledge_effect(std::uint64_t append_id,std::uint32_t ordinal,
        std::uint64_t expected_recipient) noexcept;
    Result<JournalReceipt> query(std::uint64_t append_id, std::span<std::byte> output) noexcept;
    // Read exact lossless post-state and decision/outbox envelope by committed
    // sequence in one fence-checked database snapshot. Source epochs may precede
    // the current fence. Neither output changes on failure; outputs cannot overlap.
    // This is storage readback, not authentication, recovery validation or a lease
    // permit. The restored host must validate the full journal and reauthorize
    // before publishing. Missing committed coverage is RecoveryUnavailable.
    Result<CommittedJournalRecord> read_committed(Epoch expected_fence,
        std::uint64_t sequence,std::span<std::byte> canonical,
        std::span<std::byte> decision_envelope) noexcept;
    // Canonical continuity through the fenced committed prefix C in one
    // snapshot. The anchor must come from a separately verified checkpoint
    // envelope (or explicit genesis); this call does not verify its source.
    // Every record in (anchor, C] must be a canonical envelope matching its
    // stored row and continuing the anchor's profile and digest chain. The
    // final claim at C is returned (the anchor itself when C is the anchor).
    // At most maximum_records (<=1,000,000) rows are read, without allocation.
    // Storage continuity only: no semantic validation, authorization or lease.
    Result<CanonicalJournalProof> verify_canonical_journal(Epoch expected_fence,
        const CanonicalStateHeader& anchor,std::uint32_t maximum_records) noexcept;
    // Derive a HostRestorePlan for the exact current committed grant in one
    // snapshot. Requires the bound boot and stored grant to equal the scope, the
    // prefix to remain exactly at the grant's fenced prefix C, and a
    // RecoveryVerified checkpoint whose stored content is one canonical
    // checkpoint envelope matching its manifest and the journal record at its
    // sequence. The chain (anchor, C] must verify and end in an epoch below the
    // grant epoch, which becomes the successor epoch. Bind a checkpoint digest
    // first. Storage evidence only: no semantic validation, lease or publication.
    Result<CanonicalRestoreEvidence> canonical_restore_evidence(
        const CanonicalRestoreScope&) noexcept;
    // Private operation overlay; native management and opaque local permits.
    Status bind_control_issuer(const service::control::IssuerIdentity&) noexcept;
    Result<std::optional<service::control::PolicyReceipt>> control_policy(std::uint64_t session,std::uint64_t principal) noexcept;
    Result<service::control::PolicyReceipt> commit_control_policy(const service::control::PolicyChange&) noexcept;
    Status install_execution_lease(const service::control::ExecutionLease&) noexcept;
    Result<JournalPrefix> read_control(const service::control::ExecutionPermit&) noexcept;
    Result<JournalReceipt> append_control(const service::control::ExecutionPermit&,std::uint64_t append_id,Tick,std::span<const std::byte>) noexcept;
    Status control_heartbeat() noexcept;
    // Additive content staging; never removes journal records or changes append
    // identity admission. One staging/content-verified candidate and two
    // recovery-verified bases, <=48 MiB
    // each, 64 KiB chunks. Only explicit validated replacement retires a base.
    // Bind a trusted nonreentrant digest on the storage owner before these calls.
    // The digest must outlive the journal; identical rebinding is allowed.
    Status bind_checkpoint_digest(CryptographicDigest&) noexcept;
    Result<CheckpointContentReceipt> begin_checkpoint(const CheckpointManifest&) noexcept;
    Status put_checkpoint_chunk(CheckpointTicket,std::uint32_t,std::span<const std::byte>) noexcept;
    Result<CheckpointContentReceipt> seal_checkpoint(CheckpointTicket) noexcept;
    Result<CheckpointContentReceipt> query_checkpoint(std::uint64_t id) noexcept;
    Result<std::size_t> read_checkpoint_chunk(CheckpointTicket,std::uint32_t,std::span<std::byte>) noexcept;
    Status abandon_checkpoint(CheckpointTicket) noexcept;
    // Store-generated canonical service state, never caller-uploaded coverage.
    // Each page uses an indexed key cursor and checks the same per-match revision.
    // Mutations to another match do not invalidate this capture; a global boot
    // transition or an eviction of this match's result does. The match must
    // remain semantically stable across pages; sustained mutations can prevent
    // completion. The required two-second cadence under load, consistent-cut
    // capture during writes, restore and compaction are not qualified here.
    Result<CheckpointServiceProgress> begin_checkpoint_service(CheckpointTicket,bool restart=false) noexcept;
    Result<CheckpointServiceProgress> capture_checkpoint_service_page(CheckpointTicket,std::uint32_t expected_page) noexcept;
    Result<CheckpointServiceProgress> checkpoint_service_progress(CheckpointTicket) noexcept;
    Result<std::size_t> read_checkpoint_service_page(CheckpointTicket,std::uint32_t,std::span<std::byte>) noexcept;
    Result<CheckpointRecoveryReceipt> verify_checkpoint_recovery(CheckpointTicket,CheckpointParticipantVerifier&) noexcept;
    Result<CheckpointParticipantReceipt> verify_checkpoint_participants(
        CheckpointTicket,CheckpointParticipantVerifier&) noexcept;
    // Same-epoch replacement only. One bounded coverage page per call, at most
    // 64 records and 1MiB+56 bytes including metadata and complete envelopes. Replacement
    // identities use a fixed epoch, contiguous sequences, a durable retirement
    // watermark and 64 recent results. Expired IDs return Retired without reuse.
    Result<CheckpointReplacementProgress> begin_checkpoint_replacement(const CheckpointReplacementRequest&,bool restart=false) noexcept;
    Result<CheckpointReplacementProgress> capture_checkpoint_coverage(const CheckpointReplacementRequest&) noexcept;
    Result<CheckpointReplacementProgress> query_checkpoint_replacement(const CheckpointReplacementRequest&) noexcept;
    Result<CheckpointReplacementProgress> cancel_checkpoint_replacement(const CheckpointReplacementRequest&) noexcept;
    Result<CheckpointReplacementProgress> replace_checkpoint(const CheckpointReplacementRequest&,CheckpointParticipantVerifier&) noexcept;
    // SQLite WAL maintenance only; not canonical recovery compaction.
    Status checkpoint() noexcept;
};
}
