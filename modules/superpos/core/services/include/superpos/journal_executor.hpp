#pragma once
#include "superpos/service/control_ordered.hpp"
#include "superpos/sqlite_journal.hpp"

namespace superpos {
struct JournalQueueConfig {
    std::uint32_t slots{8};
    std::uint32_t payload_bytes{2*1024*1024};
};
struct JournalJobTicket {
    std::uint64_t sequence{};
    std::uint32_t slot{};
    // Nonzero process-local executor incarnation; preserved by a safe move.
    // A foreign/recreated executor never consumes a ticket solely by slot/seq.
    std::uint64_t incarnation{};
};
enum class JournalJobKind : std::uint8_t { Append, Decisions, Query, Fence, Checkpoint,
    RegisterActor,BeginOperation,QueryOperation,NextEffect,AcknowledgeEffect,Prefix,ReadCommitted,
    QueryBoot,BeginBoot,BindBoot,QueryAuthority,CommitAuthority,QueryMatch,
    BindControlIssuer,CommitControlPolicy,InstallExecutionLease,ReadControl,AppendControl,CheckpointBegin,CheckpointPut,CheckpointSeal,CheckpointQuery,
    CheckpointAbandon,CheckpointRead,CheckpointServiceBegin,CheckpointServicePage,CheckpointServiceQuery,CheckpointServiceRead,
    RestoreEvidence,Compact,QueryReceipt };
struct JournalJobCompletion {
    JournalJobKind kind{};
    Error error{Error::None};
    JournalReceipt receipt{};
    JournalPrefix prefix{};
    CommittedJournalRecord committed{};
    DurableOperationReceipt operation{};
    DurableEffectReceipt effect{};
    // QueryBoot: absent is a successful observation of an empty store. BeginBoot:
    // present with error None is the store's successful FULL/WAL commit receipt.
    // Query observations never reconfirm an uncertain write's durability.
    std::optional<CoordinatorBootReceipt> boot{};
    // QueryAuthority observes; CommitAuthority carries the storage commit receipt.
    // Neither kind authenticates a lease or reports completed recovery.
    std::optional<DurableAuthorityReceipt> authority{};
    std::optional<std::uint64_t> match{};
    std::optional<service::control::PolicyReceipt> control_policy{};
    std::optional<CheckpointContentReceipt> checkpoint{};
    std::optional<CheckpointServiceProgress> checkpoint_service{};
    // RestoreEvidence: storage-derived plan/anchor/final claim, never a permit.
    std::optional<CanonicalRestoreEvidence> restore{};
    // Compact: one bounded storage step; receipt is never a lease or permit.
    std::optional<JournalCompactionReceipt> compaction{};
    std::uint32_t output_bytes{};
};
// One producer/consumer owner, one externally managed serial storage worker.
// Capacity includes completed jobs until their owner consumes them. Job payloads
// are copied at admission; no caller buffers or callbacks cross the boundary.
// The worker owns its SqliteJournal and calls worker_step(). It must be joined,
// and all calls stopped, before moving/destroying the queue or its allocator.
// The first trusted worker_step call binds a fixed worker/journal even if idle;
// that journal must not be moved/replaced until the worker has stopped.
// Accepted queue tickets imply neither storage success nor durable commitment.
class JournalExecutor {
    struct Impl;
    Impl* impl_{};
    explicit JournalExecutor(Impl*) noexcept;
public:
    static Result<JournalExecutor> create(Allocator&,JournalQueueConfig={}) noexcept;
    JournalExecutor(const JournalExecutor&)=delete;
    JournalExecutor& operator=(const JournalExecutor&)=delete;
    JournalExecutor(JournalExecutor&&) noexcept;
    JournalExecutor& operator=(JournalExecutor&&) noexcept;
    ~JournalExecutor();
    Result<JournalJobTicket> append(Epoch,std::uint64_t append_id,Tick,
        std::span<const std::byte>) noexcept;
    Result<JournalJobTicket> append_decisions(Epoch,std::uint64_t append_id,Tick,
        std::span<const std::byte> canonical,std::span<const DurableDecision>,
        std::span<const DurableEffect>) noexcept;
    Result<JournalJobTicket> query(std::uint64_t append_id) noexcept;
    Result<JournalJobTicket> fence(Epoch expected,Epoch replacement) noexcept;
    Result<JournalJobTicket> checkpoint() noexcept;
    Result<JournalJobTicket> begin_checkpoint(CheckpointManifest) noexcept;
    Result<JournalJobTicket> put_checkpoint_chunk(CheckpointTicket,std::uint32_t,std::span<const std::byte>) noexcept;
    Result<JournalJobTicket> seal_checkpoint(CheckpointTicket) noexcept;
    Result<JournalJobTicket> query_checkpoint(std::uint64_t id) noexcept;
    Result<JournalJobTicket> abandon_checkpoint(CheckpointTicket) noexcept;
    Result<JournalJobTicket> read_checkpoint_chunk(CheckpointTicket,std::uint32_t) noexcept;
    Result<JournalJobTicket> begin_checkpoint_service(CheckpointTicket,bool restart=false) noexcept;
    Result<JournalJobTicket> capture_checkpoint_service_page(CheckpointTicket,std::uint32_t expected_page) noexcept;
    Result<JournalJobTicket> checkpoint_service_progress(CheckpointTicket) noexcept;
    Result<JournalJobTicket> read_checkpoint_service_page(CheckpointTicket,std::uint32_t) noexcept;
    Result<JournalJobTicket> prefix() noexcept;
    Result<JournalJobTicket> query_boot() noexcept;
    // Trusted service startup only. The fresh CSPRNG nonce and explicit optional
    // expected term are copied into fixed job metadata before Accepted admission.
    // Retry UnknownOutcome with the exact same expected value and nonce; a query
    // alone is not a Committed transition. No grant/lease/election is implied.
    Result<JournalJobTicket> begin_boot(std::optional<std::uint64_t> expected,
        std::array<std::byte,32> nonce) noexcept;
    // Copy an immutable trusted storage identity into bounded job metadata.
    // Completion is observation/binding only, not a new durable boot receipt.
    Result<JournalJobTicket> bind_boot(CoordinatorBootIdentity) noexcept;
    Result<JournalJobTicket> query_match() noexcept;
    Result<JournalJobTicket> query_authority() noexcept;
    // Proposal metadata is copied into the fixed slot before Accepted admission.
    Result<JournalJobTicket> commit_authority(DurableAuthorityProposal) noexcept;
    Result<JournalJobTicket> bind_control_issuer(const service::control::IssuerIdentity&) noexcept;
    Result<JournalJobTicket> commit_control_policy(const service::control::PolicyChange&) noexcept;
    Result<JournalJobTicket> install_execution_lease(const service::control::ExecutionLease&) noexcept;
    Result<JournalJobTicket> read_control(const service::control::ExecutionPermit&) noexcept;
    Result<JournalJobTicket> append_control(const service::control::ExecutionPermit&,std::uint64_t,Tick,std::span<const std::byte>) noexcept;
    // Requires the default 2MiB slot: reserve separate complete state/envelope
    // readback capacity before admission. Completion output concatenates canonical
    // bytes then envelope bytes, whose exact lengths are recorded in committed.
    // Fencing/restore/authentication validation remains the owning host's duty.
    Result<JournalJobTicket> read_committed(Epoch expected_fence,
        std::uint64_t committed_sequence) noexcept;
    // Scope is copied into fixed slot metadata. Only a coordinator-minted
    // capability should supply it; the worker compares it with stored state.
    Result<JournalJobTicket> restore_evidence(const CanonicalRestoreScope&) noexcept;
    // One bounded compaction step behind the oldest verified base (1..4096
    // records). QueryReceipt resolves an append ID whose bytes may be compacted.
    Result<JournalJobTicket> compact_journal(Epoch expected_fence,std::uint32_t maximum_records) noexcept;
    Result<JournalJobTicket> query_receipt(std::uint64_t append_id) noexcept;
    Result<JournalJobTicket> register_actor(Epoch authority,OperationActorKind,
        std::uint64_t actor,std::uint64_t actor_epoch,PeerId,std::uint64_t first_sequence=0) noexcept;
    // Trusted service admission only. Execute means a durable reservation was
    // created, not permission to run gameplay: restore/lease/ownership gates
    // must be rechecked when the completion reaches the owning dispatcher.
    Result<JournalJobTicket> begin_operation(Epoch authority,DurableOperationId,
        std::span<const std::byte> request) noexcept;
    Result<JournalJobTicket> query_operation(DurableOperationId,
        std::span<const std::byte> request) noexcept;
    Result<JournalJobTicket> next_effect() noexcept;
    // Acknowledge only after verified recipient durable/idempotent execution.
    Result<JournalJobTicket> acknowledge_effect(std::uint64_t append_id,
        std::uint32_t ordinal,std::uint64_t authenticated_recipient) noexcept;
    // Nonblocking owner-side read. Busy means not completed. Too-small output
    // preserves the completion for another read; a successful read retires it.
    Result<JournalJobCompletion> consume(JournalJobTicket,
        std::span<std::byte> output={}) noexcept;
    // Owner-side release of a ticket whose completion will never be consumed.
    // A queued or running job still executes in order (writes keep their
    // normal outcome semantics); its completion is discarded and the worker
    // frees the slot. A completed job is freed immediately. Abandoned tickets
    // are stale for consume/abandon.
    Status abandon(JournalJobTicket) noexcept;
    // Process at most one admitted job. False means no work. An individual
    // storage error is a completion, not an executor error. No gameplay code runs.
    // Concurrent/reentrant entry returns Busy; the provisioned worker retries.
    Result<bool> worker_step(SqliteJournal&) noexcept;
    // Owner-side irreversible admission seal. All later submissions return
    // NotReady; existing completions stay consumable and the worker still drains.
    // Idempotent; this does not itself request worker stop or prove quiescence.
    Status stop_admission() noexcept;
private:
    Result<JournalJobTicket> submit_checkpoint(JournalJobKind,CheckpointTicket,std::uint32_t,std::span<const std::byte> = {}) noexcept;
    Result<JournalJobTicket> submit(JournalJobKind,Epoch,std::uint64_t,Tick,
        std::span<const std::byte>) noexcept;
    Result<JournalJobTicket> submit_operation(JournalJobKind,Epoch,DurableOperationId,
        PeerId,std::span<const std::byte>) noexcept;
};
}
