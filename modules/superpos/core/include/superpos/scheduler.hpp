#pragma once
#include "replication.hpp"
#include "relevance.hpp"
#include <thread>
#include <array>

namespace superpos {
// A unit is one ready replica or a complete application-validated publication
// group. Groups are indivisible: object_count and wire_bytes charge the entire
// group, whose membership/canonical payload stays with the application. This
// scheduler never assembles a group or validates simulation state.
struct SchedulingUnit {
    ObjectHandle handle{};
    ReplicaKey key{};
    std::uint64_t revision{};
    std::uint32_t wire_bytes{};
    std::uint16_t object_count{1},weight{1};
    Visibility visibility{Visibility::Visible};
    // Independent complete full-state wire price; zero is unqualified for repair.
    std::uint32_t repair_wire_bytes{};
};
enum class SchedulingAction : std::uint8_t { State,Repair,Retire };
struct ScheduleSelection { SchedulingUnit unit{}; SchedulingAction action{}; std::uint32_t reserved_wire_bytes{}; };
struct ScheduleBudget { std::uint32_t wire_bytes{8192},objects{1000}; std::uint16_t units{64}; };
struct SchedulePlan { std::uint64_t token{}; std::uint32_t wire_bytes{},objects{},oversized_units{}; std::uint16_t units{}; };
struct ScheduleCompletion { bool carrier_accepted{}; std::uint64_t retirement_sequence{}; };
struct SchedulerRecord {
    bool occupied{},retiring{},repair{};
    SchedulingUnit unit{};
    std::uint64_t accepted_revision{},last_served_round{},retirement_sequence{};
    std::uint64_t selected_round{};
    std::uint16_t selected_ordinal{};
    SchedulingAction selected_action{};
    // At most sixteen identities per indivisible publication. The sender store
    // must outlive this record and shares the scheduler's owner thread.
    const PeerReplicas* replicas{};
    std::array<ReplicaBinding,16> members{};
};
enum class SchedulerIndexState : std::uint8_t { Empty,Occupied,Retired };
struct SchedulerIndex { std::uint32_t slot{},record{}; SchedulerIndexState state{}; };
struct SchedulerConfig {
    Epoch authority_epoch{1},connection_epoch{1},replica_epoch{1};
    std::uint32_t maximum_objects{1000},maximum_unit_bytes{131072};
    std::uint16_t maximum_unit_objects{16},maximum_weight{256};
    std::uint64_t maximum_wait_rounds{32},initial_round{};
};
// Owner thread, exclusive caller storage. Open-address lookups inspect only this
// peer's compact records; planning scans at most 4096 tracked units (never World).
// At most 64 updates/selections per call. Priority wins until the age deadline;
// overdue work is oldest first. Fairness assumes units fit the supplied budget
// and accepted transmissions continue. Rejection never marks state published.
// Wire cost is the caller's complete forward-wire estimate, including framing;
// return-path/transport congestion and global peer budgets are separate owners.
// Selections reserve reserved_wire_bytes; Repair never reuses the delta price.
// No allocation. RegionIndex/policy/lifecycle collection is an external stage.
class PeerScheduler {
public:
    static constexpr std::size_t maximum_units=4096,maximum_batch=64;
    static Result<PeerScheduler> create(SchedulerConfig,std::span<SchedulerRecord>,std::span<SchedulerIndex>) noexcept;
    PeerScheduler(const PeerScheduler&)=delete;
    PeerScheduler& operator=(const PeerScheduler&)=delete;
    PeerScheduler(PeerScheduler&&) noexcept;
    PeerScheduler& operator=(PeerScheduler&&) noexcept;
    // Readiness is checked against the actual sender store. Empty members is
    // shorthand for a single object; groups provide every Ready binding, with
    // the anchor first. No compact slot may belong to two tracked units.
    Status track(SchedulingUnit,const PeerReplicas&,std::span<const ReplicaBinding> members={},std::uint64_t accepted_revision=0) noexcept;
    // Preserve every member's full handle, slot and incarnation. Actual newer
    // encoding keys must come from an acknowledged sender reset. Membership is
    // unchanged and a separately priced full repair is mandatory afterwards.
    Status rebind(SchedulingUnit,const PeerReplicas&,std::span<const ReplicaBinding> members={}) noexcept;
    Status update(std::span<const SchedulingUnit>) noexcept;
    // Missing-baseline/unreliable-loss recovery explicitly requests a full repair
    // with its complete encoded wire cost; it does not forge an Applied receipt.
    Status request_repair(ObjectHandle,const ReplicaKey&,std::uint32_t wire_bytes) noexcept;
    Result<SchedulePlan> plan(ScheduleBudget,std::span<ScheduleSelection>) noexcept;
    // Completions match selection order. Accepted Retire must provide the last
    // issued lifecycle sequence covering every member; all other sequences=0.
    // This is carrier acceptance, never an Applied/Committed receipt.
    Status finish(std::uint64_t token,std::span<const ScheduleCompletion>) noexcept;
    // Only a separately authenticated/validated cumulative lifecycle Applied
    // watermark may release a retiring unit. State receipts cannot release it.
    Status retirement_applied(ObjectHandle,const ReplicaKey&,std::uint64_t watermark) noexcept;
    Result<SchedulerRecord> inspect(ObjectHandle,const ReplicaKey&) const noexcept;
    // Zero is a valid initialized count. Wrong-owner and moved-from queries
    // return explicit errors before reading mutable counters.
    Result<std::size_t> tracked_units() const noexcept {
        if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
        if(records_.empty())return fail(Error::NotReady);
        return active_;
    }
    Result<std::uint32_t> tracked_objects() const noexcept {
        if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
        if(records_.empty())return fail(Error::NotReady);
        return objects_;
    }
private:
    PeerScheduler(SchedulerConfig,std::span<SchedulerRecord>,std::span<SchedulerIndex>) noexcept;
    Status validate(const SchedulingUnit&) const noexcept;
    Status readiness(const SchedulerRecord&) const noexcept;
    Result<std::size_t> lookup(std::uint32_t slot) const noexcept;
    Result<std::size_t> index(ObjectHandle,const ReplicaKey&) const noexcept;
    Result<std::size_t> vacant_index(std::uint32_t slot) const noexcept;
    SchedulerConfig config_{};
    std::span<SchedulerRecord> records_{};
    std::span<SchedulerIndex> indices_{};
    std::size_t active_{};
    std::uint32_t objects_{};
    std::uint64_t round_{},pending_token_{};
    std::uint16_t pending_units_{};
    std::thread::id owner_{std::this_thread::get_id()};
    const PeerReplicas* replicas_{};
};
}
