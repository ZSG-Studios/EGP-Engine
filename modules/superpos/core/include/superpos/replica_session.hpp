#pragma once
#include "superpos/replica_wire.hpp"
#include "superpos/session.hpp"
#include <thread>

namespace superpos {
struct ReplicaSessionRoutes { std::uint8_t control{},state{1},bulk{2}; };
enum class ReplicaReplyPhase : std::uint8_t { Empty,Queued,AwaitingApplied };
struct ReplicaReplySlot { ReplicaReplyPhase phase{}; ReplicaWireMessage message{}; DeliveryTicket ticket{}; std::uint64_t order{}; };
struct PendingReplicaSpawn { bool occupied{}; ReplicaKey key{}; };
struct ReplicaSessionProgress { std::uint16_t consumed{},spawned{},sent{},discarded{},backpressured{}; };
// An admitted client Session dispatches typed records into one receiver. Session,
// receiver and application outlive this owner-thread bridge; caller queues are
// exclusive and wire scratch is separate from all three components' storage.
// Every pump verifies local instance tokens as well as admitted identities;
// replacing either borrowed store requires a newly created bridge. Epoch scopes
// in wire v1 start at one and reserve zero; local instance tokens are not sent.
// Exactly three distinct negotiated routes: ordered Control, reliable unordered
// Bulk, reliable unordered or single-frame unreliable State. No backend physics
// or authority simulation is implied. The application supplies a real barrier.
class ReplicaReceiverSession {
public:
    static constexpr std::size_t maximum_replies=4,maximum_pending_spawns=64,minimum_wire_scratch=8192;
    static Result<ReplicaReceiverSession> create(Session&,PeerReplicaReceiver&,ReplicaApplication&,ReplicaWireContext,ReplicaSessionRoutes,
        std::span<ReplicaReplySlot>,std::span<PendingReplicaSpawn>,std::span<std::byte> wire_scratch) noexcept;
    ReplicaReceiverSession(const ReplicaReceiverSession&)=delete;
    ReplicaReceiverSession& operator=(const ReplicaReceiverSession&)=delete;
    ReplicaReceiverSession(ReplicaReceiverSession&&) noexcept;
    ReplicaReceiverSession& operator=(ReplicaReceiverSession&&) noexcept;
    Result<ReplicaSessionProgress> pump(Tick) noexcept;
private:
    ReplicaReceiverSession() noexcept=default;
    Status flush(Tick,ReplicaSessionProgress&) noexcept;
    Status consume(std::uint8_t,ReplicaLane,ReplicaSessionProgress&) noexcept;
    Status readiness(ReplicaSessionProgress&) noexcept;
    Status binding_valid() const noexcept;
    ReplicaReplySlot* vacant_reply() noexcept;
    Session* session_{}; PeerReplicaReceiver* receiver_{}; ReplicaApplication* application_{};
    ReplicaWireContext context_{}; ReplicaSessionRoutes routes_{};
    SessionIdentity identity_{}; ReceiverConfig receiver_config_{}; std::uint64_t receiver_instance_{};
    std::span<ReplicaReplySlot> replies_{}; std::span<PendingReplicaSpawn> spawns_{}; std::span<std::byte> wire_{};
    std::array<std::uint64_t,3> consumed_pending_{};
    std::uint64_t reply_issued_{};
    std::thread::id owner_{std::this_thread::get_id()}; bool failed_{},pumping_{};
};
static_assert(sizeof(ReplicaReplySlot)*ReplicaReceiverSession::maximum_replies<=32768);
}
