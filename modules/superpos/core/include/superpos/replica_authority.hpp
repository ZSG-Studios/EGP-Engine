#pragma once
#include "superpos/replica_session.hpp"

namespace superpos {
enum class AuthorityCapturePhase : std::uint8_t { Empty,Queued,AwaitingCompletion };
struct AuthorityCapture {
    AuthorityCapturePhase phase{}; ReplicaWireKind kind{}; ReplicaLane lane{};
    DeliveryTicket ticket{}; std::uint64_t order{},lifecycle{},token{},revision{};
    std::uint32_t bytes{}; std::uint16_t count{}; ReplicaKey key{};
    std::array<ReplicaKey,16> members{};
};
struct AuthorityExposure {
    bool occupied{},ready_received{}; ReplicaKey key{};
    std::uint64_t bind_sent{},offered_tokens[2]{},offered_revisions[2]{},state_sent{};
};
struct ReplicaAuthorityProgress { std::uint16_t sent{},transport_applied{},carrier_retired{},received{},discarded{},backpressured{}; };
// Owner-thread authority wire plumbing, without coordinator/physics callbacks.
// Caller gameplay prepares actual sender transitions at its barrier, then queues
// their exact records here. Captures own encoded bytes; borrowed schemas, Session,
// sender and all exclusive caller storage outlive the bridge. Do not move or
// replace components during any call. Replacement between calls is detected.
// Trusted provider callbacks must not mutate borrowed components directly while
// a call is active; dispatcher Busy guards do not guard unrelated direct APIs.
// A new bridge requires an empty sender store. No manual Ready/ACK escape exists.
// The runtime must separately enforce current authority/continuity on retry
// egress AND receiver application; this class is not a lease or CommitStore.
// All three routes are exclusively reserved for replication. Client replies use
// ordered Control; unsolicited client State/Bulk is a terminal protocol error.
// Reliable captures remain through actual transport Applied. Plain unreliable
// State retires after carrier acceptance, without claiming receipt/application;
// its later semantic StateApplied reply, if received, is tracked independently.
class ReplicaAuthoritySession {
public:
    static constexpr std::size_t maximum_captures=4,capture_bytes=65536,maximum_exposures=4096,maximum_repairs=64;
    static Result<ReplicaAuthoritySession> create(Session&,PeerReplicas&,const FrozenRegistry&,ReplicaWireContext,ReplicaSessionRoutes,
        std::span<AuthorityCapture>,std::span<std::byte> immutable_arena,std::span<AuthorityExposure>,std::span<RepairRequest>,std::span<std::byte> validation_scratch) noexcept;
    ReplicaAuthoritySession(const ReplicaAuthoritySession&)=delete;
    ReplicaAuthoritySession& operator=(const ReplicaAuthoritySession&)=delete;
    ReplicaAuthoritySession(ReplicaAuthoritySession&&) noexcept;
    ReplicaAuthoritySession& operator=(ReplicaAuthoritySession&&) noexcept;
    // Returns complete logical encoded size. Acceptance captures bytes; it is
    // not transport Received/Applied or semantic pin/Ready/StateApplied.
    Result<std::uint32_t> queue(const ReplicaWireMessage&) noexcept;
    Result<ReplicaAuthorityProgress> pump(Tick) noexcept;
    Result<std::size_t> take_repairs(std::span<RepairRequest>) noexcept;
private:
    ReplicaAuthoritySession() noexcept=default;
    Status binding_valid() const noexcept;
    Status validate(const ReplicaWireMessage&,bool sending) noexcept;
    Status validate_group(const ReplicaWireMessage&,bool sending) noexcept;
    Result<ReplicaRecord> record(const ReplicaKey&) const noexcept;
    AuthorityExposure* exposure(const ReplicaKey&) noexcept;
    AuthorityCapture* vacant() noexcept;
    std::span<std::byte> storage(AuthorityCapture&) noexcept;
    Status flush(Tick,ReplicaAuthorityProgress&) noexcept;
    Status receipts(ReplicaAuthorityProgress&) noexcept;
    Status stage_retirement(const BaselineRetirement&,AuthorityCapture&) noexcept;
    Session* session_{}; PeerReplicas* sender_{}; const FrozenRegistry* registry_{};
    SessionIdentity identity_{}; ReplicaConfig config_{}; DeliveryLimits limits_{}; std::uint64_t sender_instance_{};
    ReplicaWireContext context_{}; ReplicaSessionRoutes routes_{};
    std::span<AuthorityCapture> captures_{}; std::span<std::byte> arena_{},scratch_{};
    std::span<AuthorityExposure> exposures_{}; std::span<RepairRequest> repairs_{};
    std::size_t repair_count_{}; std::uint64_t order_{},control_queued_{},control_sent_{},consumed_pending_{};
    std::thread::id owner_{std::this_thread::get_id()}; bool failed_{},pumping_{};
};
}
