#pragma once
#include "registry.hpp"
#include "replication.hpp"
#include <thread>

namespace superpos {
enum class ReceiverPhase : std::uint8_t { Empty,SpawnPending,Ready };
struct ReceiverImage { std::uint64_t token{},revision{}; Tick tick{}; bool pinned{}; };
struct ReceiverRecord {
    ReceiverPhase phase{}; bool repair_required{}; ObjectHandle handle{}; const SchemaRecord* catalog{};
    PeerId owner{}; std::uint64_t ownership_revision{},incarnation{}; Epoch encoding_epoch{};
    std::uint64_t bind_sequence{},leave_sequence{},highest_token{},current_revision{};
    std::uint64_t initial_applied_revision{},initial_ownership_revision{}; Epoch initial_encoding_epoch{};
    Tick current_tick{}; ReceiverImage images[2]{}; std::int8_t active_image{-1};
    std::uint64_t retired_token{},retired_replacement{},retired_sequence{},ownership_sequence{};
};
struct ReceiverConfig {
    Epoch authority_epoch{1},connection_epoch{1},replica_epoch{1}; PeerId peer{};
    std::size_t maximum_active{1000},maximum_transitions{64},state_stride{64};
    std::uint64_t first_control_sequence{1};
    bool operator==(const ReceiverConfig&) const noexcept=default;
};
struct ViewBind {
    ReplicaKey key{}; ObjectHandle handle{}; SchemaId schema{}; PeerId owner{};
    std::uint64_t ownership_revision{},lifecycle_sequence{};
};
struct StateAppliedReceipt { ReplicaKey key{}; std::uint64_t revision{}; };
struct BaselinePinnedReceipt { ReplicaKey key{}; std::uint64_t token{},revision{}; };
struct SpawnAppliedReceipt { ReplicaKey key{}; std::uint64_t revision{},ownership_revision{}; };
struct LifecycleAppliedReceipt { std::uint64_t sequence{}; };
struct StatePatch { ReplicaKey key{}; std::uint64_t baseline_token{},revision{}; std::span<const std::byte> delta{}; };
struct FullRepair { ReplicaKey key{}; std::uint64_t revision{}; std::span<const std::byte> canonical{}; };
struct RepairRequest { ReplicaKey key{}; std::uint64_t missing_token{},last_applied_revision{}; };
enum class PublicationDisposition : std::uint8_t { Applied,Duplicate,NeedRepair };
struct PublicationResult { PublicationDisposition disposition{}; std::uint16_t receipts{},repairs{}; };
struct CanonicalReplica {
    ReplicaKey key{}; ObjectHandle handle{}; const Schema* schema{}; PeerId owner{};
    std::uint64_t ownership_revision{},revision{}; Tick tick{}; std::span<const std::byte> canonical{};
};
enum class ReplicaChangeKind : std::uint8_t { Spawn,Publication,OwnershipReset,Destroy };
// Stage may fail/Busy; abort must undo all preparation including a failing stage.
// Commit cannot fail and runs at the application's barrier, with no reentrant
// receiver access or retained borrowed spans. No durable side effect is implied.
class ReplicaApplication {
public:
    virtual ~ReplicaApplication()=default;
    virtual Status stage(ReplicaChangeKind,std::span<const CanonicalReplica>) noexcept=0;
    virtual void commit() noexcept=0;
    virtual void abort() noexcept=0;
};
// Receiver implementation: core canonical state and images only. A bound
// global handle is an identity, never an index into a dense global client World.
// Exclusive caller records/images/scratch and a moved registry freeze token live
// for this owner-thread store. No allocation or native simulation backend.
class PeerReplicaReceiver {
public:
    static constexpr std::size_t maximum_group_objects=16,maximum_group_bytes=65536;
    static Result<PeerReplicaReceiver> create(ReceiverConfig,FrozenRegistry,
        std::span<ReceiverRecord>,std::span<std::byte> images,std::span<std::byte> scratch) noexcept;
    PeerReplicaReceiver(const PeerReplicaReceiver&)=delete;
    PeerReplicaReceiver& operator=(const PeerReplicaReceiver&)=delete;
    PeerReplicaReceiver(PeerReplicaReceiver&&) noexcept;
    PeerReplicaReceiver& operator=(PeerReplicaReceiver&&) noexcept;
    Result<LifecycleAppliedReceipt> bind(const ViewBind&) noexcept;
    Result<BaselinePinnedReceipt> offer(const BaselineOffer&,Tick) noexcept;
    Result<SpawnAppliedReceipt> spawn_ready(const ReplicaKey&,ReplicaApplication&) noexcept;
    Result<PublicationResult> publish(std::span<const StatePatch>,std::uint64_t group_id,Tick,
        std::span<StateAppliedReceipt>,std::span<RepairRequest>,ReplicaApplication&) noexcept;
    // A full repair superseded in any member returns StaleGeneration after
    // complete preflight, preserving the entire group and caller receipts. Wire
    // dispatch consumes that obsolete record without claiming StateApplied.
    Result<PublicationResult> repair(std::span<const FullRepair>,std::uint64_t group_id,Tick,
        std::span<StateAppliedReceipt>,ReplicaApplication&) noexcept;
    Result<LifecycleAppliedReceipt> retire_baseline(const BaselineRetirement&) noexcept;
    Result<LifecycleAppliedReceipt> leave(const ReplicaKey&,std::uint64_t sequence,ReplicaApplication&) noexcept;
    Result<LifecycleAppliedReceipt> reset_ownership(const ReplicaKey& old_key,const ReplicaKey& new_key,PeerId owner,
        std::uint64_t ownership_revision,std::uint64_t sequence,ReplicaApplication&) noexcept;
    Result<CanonicalReplica> inspect(const ReplicaKey&) const noexcept;
    Result<ReceiverRecord> inspect_record(const ReplicaKey&) const noexcept;
    Result<Fingerprint> schema_fingerprint() const noexcept;
    Result<ReceiverConfig> configuration() const noexcept;
    // Includes this store, its record/image/scratch pools and frozen schema
    // records. Used to reject accidental sharing before a bridge clears queues.
    Result<bool> storage_overlaps(std::span<const std::byte>) const noexcept;
    // Local instance is preserved by moves and cannot be reused by creation.
    Result<std::uint64_t> instance_identity() const noexcept;
    std::uint64_t lifecycle_applied() const noexcept;
private:
    struct ControlStamp { std::uint64_t sequence{}; std::uint8_t kind{},size{}; std::array<std::uint64_t,16> words{}; };
    PeerReplicaReceiver(ReceiverConfig,FrozenRegistry,std::span<ReceiverRecord>,std::span<std::byte>,std::span<std::byte>) noexcept;
    Status available() const noexcept;
    Result<std::size_t> index(const ReplicaKey&) const noexcept;
    Result<bool> control(const ControlStamp&) const noexcept;
    void acknowledge_control(ControlStamp) noexcept;
    static ControlStamp stamp(std::uint8_t kind,std::uint64_t sequence,const ReplicaKey&,std::span<const std::uint64_t> extra={}) noexcept;
    Status projected(const ReceiverRecord&,std::span<const std::byte>) const noexcept;
    Status group(std::span<const std::size_t>,std::uint64_t group_id,std::size_t decoded_bytes) const noexcept;
    Status application(ReplicaApplication&,ReplicaChangeKind,std::span<const CanonicalReplica>) noexcept;
    std::span<std::byte> image(std::size_t,std::size_t) noexcept;
    std::span<const std::byte> image(std::size_t,std::size_t) const noexcept;
    CanonicalReplica view(std::size_t) const noexcept;
    ReplicaKey key(std::size_t) const noexcept;
    ReceiverConfig config_{}; FrozenRegistry registry_;
    std::span<ReceiverRecord> records_{}; std::span<std::byte> images_{},scratch_{};
    std::size_t active_{},transitions_{}; std::uint64_t control_applied_{},instance_{};
    std::array<ControlStamp,64> controls_{};
    std::thread::id owner_{std::this_thread::get_id()}; bool applying_{};
};
}
