#pragma once
#include "types.hpp"
#include "capability.hpp"
#include <thread>
#include <cstdint>
#include <span>
#include <utility>

namespace superpos {

enum class SimulationCapability : std::uint32_t { None=0, DeterministicReplay=1, Interpolation=2 };
constexpr SimulationCapability operator|(SimulationCapability a,SimulationCapability b) noexcept {
    return static_cast<SimulationCapability>(static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b));
}
constexpr bool supports(SimulationCapability set,SimulationCapability flag) noexcept {
    return (static_cast<std::uint32_t>(set)&static_cast<std::uint32_t>(flag))!=0;
}
struct SimulationDescriptor {
    std::size_t state_bytes{}, input_bytes{};
    SimulationCapability capabilities{};
};
// Pure, bounded canonical-state transforms. step/interpolate write only output;
// they must not mutate live engine state, emit external effects or retain spans.
// Capability declarations require application qualification for this exact codec.
class SimulationAdapter {
public:
    virtual ~SimulationAdapter()=default;
    virtual SimulationDescriptor descriptor() const noexcept=0;
    virtual Status validate_state(std::span<const std::byte>) const noexcept=0;
    virtual Status validate_input(std::span<const std::byte>) const noexcept=0;
    virtual Status step(Tick,std::span<const std::byte> state,std::span<const std::byte> input,std::span<std::byte> output) noexcept=0;
    virtual Status interpolate(std::span<const std::byte>,std::span<const std::byte>,double,std::span<std::byte>) noexcept { return fail(Error::Unsupported); }
};

struct PredictionConfig { Epoch epoch{1}; std::size_t history_ticks{120}; };
struct PredictionFrame { Tick tick{}; bool occupied{}; };
struct StateView { Tick tick{}; std::span<const std::byte> canonical{}; };
struct Reconciliation { std::size_t replayed_ticks{}; bool changed{}; };
// Exclusive caller storage outlives this move-only owner and its simulation.
// arena: (history_ticks+1)*(state_bytes+input_bytes).
// scratch: (history_ticks+1)*state_bytes. Replay stages every replacement before
// publishing; capacity exhaustion requests authoritative repair, never eviction
// of an unacknowledged input. All returned spans expire on the next mutation.
class PredictionHistory {
public:
    static Result<PredictionHistory> create(PredictionConfig,SimulationAdapter&,
        std::span<PredictionFrame>,std::span<std::byte> arena,std::span<std::byte> scratch,
        Tick initial_tick,std::uint64_t authoritative_revision,std::span<const std::byte> initial_state) noexcept;
    PredictionHistory(const PredictionHistory&)=delete;
    PredictionHistory& operator=(const PredictionHistory&)=delete;
    PredictionHistory(PredictionHistory&&) noexcept;
    PredictionHistory& operator=(PredictionHistory&&) noexcept;
    Status predict(Epoch,Tick,std::span<const std::byte> input) noexcept;
    Result<Reconciliation> reconcile(Epoch,Tick,std::uint64_t revision,std::span<const std::byte> authoritative) noexcept;
    Status reset_epoch(Epoch,Tick,std::uint64_t revision,std::span<const std::byte> canonical) noexcept;
    StateView current() const noexcept;
    Result<StateView> at(Epoch,Tick) const noexcept;
    Tick confirmed_tick() const noexcept { return count_?records_[head_].tick:0; }
    Epoch epoch() const noexcept { return config_.epoch; }
    std::size_t pending_ticks() const noexcept { return count_?count_-1:0; }
private:
    PredictionHistory(PredictionConfig,SimulationAdapter&,SimulationDescriptor,std::span<PredictionFrame>,std::span<std::byte>,std::span<std::byte>) noexcept;
    std::span<std::byte> state(std::size_t) noexcept;
    std::span<const std::byte> state(std::size_t) const noexcept;
    std::span<const std::byte> input(std::size_t) const noexcept;
    std::size_t index(std::size_t relative) const noexcept { return (head_+relative)%records_.size(); }
    PredictionConfig config_{}; SimulationAdapter* simulation_{}; SimulationDescriptor descriptor_{};
    std::span<PredictionFrame> records_{}; std::span<std::byte> arena_{},scratch_{};
    std::size_t head_{},count_{1}; std::uint64_t authority_revision_{};
};

struct HistoryConfig { Epoch epoch{1}; std::size_t capacity{120}; std::uint64_t maximum_bracket_ticks{120}; };
struct HistoryFrame { Tick tick{}; std::uint64_t revision{}; bool occupied{}; };
// Interpolation and exact lag-history queries use canonical snapshots. Older
// arrivals are rejected; equal-tick newer revisions replace that sample. Oldest
// samples may be evicted at this explicit capacity. No extrapolation/physics
// rewind is claimed. Scratch is one state, arena is capacity*state_bytes.
class SnapshotHistory {
public:
    static Result<SnapshotHistory> create(HistoryConfig,SimulationAdapter&,std::span<HistoryFrame>,std::span<std::byte> arena,std::span<std::byte> scratch) noexcept;
    SnapshotHistory(const SnapshotHistory&)=delete;
    SnapshotHistory& operator=(const SnapshotHistory&)=delete;
    SnapshotHistory(SnapshotHistory&&) noexcept;
    SnapshotHistory& operator=(SnapshotHistory&&) noexcept;
    Status push(Epoch,Tick,std::uint64_t revision,std::span<const std::byte>) noexcept;
    Result<StateView> exact(Epoch,Tick) const noexcept;
    // Integer tick plus a fraction in [0,1). Only bounded relative distances are
    // converted to floating point, preserving absolute uint64 tick precision.
    Status sample(Epoch,Tick,double fraction,std::span<std::byte> output) noexcept;
    Status reset_epoch(Epoch) noexcept;
    std::size_t size() const noexcept { return count_; }
private:
    SnapshotHistory(HistoryConfig,SimulationAdapter&,SimulationDescriptor,std::span<HistoryFrame>,std::span<std::byte>,std::span<std::byte>) noexcept;
    std::size_t index(std::size_t relative) const noexcept { return (head_+relative)%records_.size(); }
    std::span<std::byte> state(std::size_t) noexcept;
    std::span<const std::byte> state(std::size_t) const noexcept;
    HistoryConfig config_{}; SimulationAdapter* simulation_{}; SimulationDescriptor descriptor_{};
    std::span<HistoryFrame> records_{}; std::span<std::byte> arena_{},scratch_{};
    std::size_t head_{},count_{};
};

struct PredictedSpawnId {
    Epoch epoch{}; PeerId peer{}; std::uint64_t sequence{};
    bool operator==(const PredictedSpawnId&) const noexcept=default;
};
enum class PredictedSpawnPhase : std::uint8_t { Empty,Pending,Confirmed,Rejected,Retired };
struct PredictedSpawnRecord {
    PredictedSpawnId id{}; PredictedSpawnPhase phase{};
    ObjectHandle canonical{}; std::uint64_t ownership_revision{};
};
// Presentation-only prediction IDs are never global world handles. Confirmation
// supplies a validated authoritative handle; game spawn/despawn remains outside
// this table. Resolved entries require explicit release; IDs never wrap or alias.
class PredictedSpawns {
public:
    // The optional trusted allocation watermark supports resumptions without
    // reissuing a previously allocated ID in the same epoch.
    static Result<PredictedSpawns> create(Epoch,PeerId,std::span<PredictedSpawnRecord>,std::uint64_t allocation_watermark=0) noexcept;
    PredictedSpawns(const PredictedSpawns&)=delete;
    PredictedSpawns& operator=(const PredictedSpawns&)=delete;
    PredictedSpawns(PredictedSpawns&&) noexcept;
    PredictedSpawns& operator=(PredictedSpawns&&) noexcept;
    Result<PredictedSpawnId> begin() noexcept;
    Status confirm(PredictedSpawnId,ObjectHandle,std::uint64_t ownership_revision) noexcept;
    Status reject(PredictedSpawnId) noexcept;
    Status release(PredictedSpawnId) noexcept;
    Status canonical_destroyed(ObjectHandle) noexcept;
    Result<PredictedSpawnRecord> inspect(PredictedSpawnId) const noexcept;
    Status reset_epoch(Epoch) noexcept;
private:
    PredictedSpawns(Epoch e,PeerId p,std::span<PredictedSpawnRecord> r) noexcept:epoch_(e),peer_(p),records_(r){}
    Result<PredictedSpawnRecord*> find(PredictedSpawnId) noexcept;
    Epoch epoch_{}; PeerId peer_{}; std::uint64_t next_sequence_{};
    std::span<PredictedSpawnRecord> records_{};
};

struct RecoveryParticipantDescriptor {
    std::uint64_t id{},schema_version{}; std::size_t maximum_checkpoint_bytes{};
    RecoveryGrade recovery{};
    // Qualified codec/simulation semantics; local configuration also covers the
    // process/build/backend/ABI configuration required by a local checkpoint.
    Fingerprint simulation{},local_configuration{};
    bool operator==(const RecoveryParticipantDescriptor&) const noexcept=default;
};
struct RecoveryRestoreRequirements {
    RecoveryGrade recovery{RecoveryGrade::PortableRestart};
    Fingerprint local_configuration{};
};
struct RecoveryPart;
// The application must attest that capture contains every simulation-relevant
// value, including hidden state and nondeterminism. stage_restore is isolated;
// descriptor() must not mutate simulation or any participant metadata. Descriptor
// metadata remains stable throughout a restore transaction, including other
// participants' callbacks. stage_restore changes only private staging; commit
// cannot allocate, fail, invoke gameplay, or change registrations. External
// effects belong in the fenced outbox.
class RecoveryParticipant {
    const std::thread::id owner_{std::this_thread::get_id()};
    bool restoring_{};
    friend Status restore_participants(Epoch,Tick,std::span<const RecoveryPart>,RecoveryRestoreRequirements) noexcept;
public:
    RecoveryParticipant() noexcept=default;
    RecoveryParticipant(const RecoveryParticipant&)=delete;
    RecoveryParticipant& operator=(const RecoveryParticipant&)=delete;
    virtual ~RecoveryParticipant()=default;
    virtual RecoveryParticipantDescriptor descriptor() const noexcept=0;
    virtual Result<std::size_t> capture(std::span<std::byte>) const noexcept=0;
    virtual Status stage_restore(Epoch,Tick,std::span<const std::byte>) noexcept=0;
    virtual void commit_restore() noexcept=0;
    virtual void abort_restore() noexcept=0;
};
struct RecoveryPart {
    RecoveryParticipant* participant{}; std::uint64_t schema_version{};
    std::span<const std::byte> canonical{};
    Fingerprint simulation{},local_configuration{};
};
// Caller holds an authenticated recovery fence, complete checkpoint coverage and
// an owner-thread publication barrier. Storage/participants outlive the call and
// checkpoint bytes remain immutable. The declared grade never upgrades restart
// to exact resume and does not establish future deterministic execution.
// Up to 16 participants are locked before descriptor/stage/commit/abort callbacks;
// all stage before any commit. Failure aborts every entered stage, including the
// failing participant. Callbacks cannot reenter restoration of a locked member.
Status restore_participants(Epoch,Tick,std::span<const RecoveryPart>,
    RecoveryRestoreRequirements={}) noexcept;

} // namespace superpos
