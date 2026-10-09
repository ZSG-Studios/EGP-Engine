#pragma once
#include "superpos/receiver.hpp"
#include "superpos/delivery.hpp"

namespace superpos {
enum class ReplicaWireKind : std::uint8_t {
    Bind=1,BaselineOffer,SpawnApplied,BaselinePinned,BaselineRetire,LifecycleApplied,
    Leave,OwnershipReset,Publication,FullRepair,StateApplied,RepairRequest
};
enum class ReplicaLane : std::uint8_t { Control,State,Bulk };
struct ReplicaWireContext { Epoch authority{},connection{},replica{}; bool operator==(const ReplicaWireContext&) const noexcept=default; };
// Bounded tagged decode result. Only the selected kind's fields are meaningful.
// Decoded payload spans borrow the entire authenticated delivery message; copy
// or consume them before marking that delivery Applied. No native pointers or
// descriptor padding appear on wire. Epochs are shared by the batch; all counters
// retain the full uint64 domain through canonical varuint encodings.
struct ReplicaWireMessage {
    static constexpr std::size_t maximum_members=16,maximum_receipts=64,maximum_bytes=65536;
    ReplicaWireKind kind{}; ReplicaWireContext context{};
    ViewBind binding{}; BaselineOffer baseline{}; SpawnAppliedReceipt spawned{}; BaselinePinnedReceipt pinned{};
    BaselineRetirement retirement{}; ReplicaKey key{},new_key{};
    // Bulk/State sequence is the required committed lifecycle prefix. It lets
    // unordered data wait for an earlier Bind/reset arriving on Control.
    PeerId owner{}; std::uint64_t ownership_revision{},sequence{},revision{},group_id{}; Tick tick{};
    std::uint16_t count{};
    std::array<StatePatch,maximum_members> patches{};
    std::array<FullRepair,maximum_members> repairs{};
    std::array<StateAppliedReceipt,maximum_receipts> applied{};
    std::array<RepairRequest,maximum_members> requested{};
};
// Encode preflights canonical structure, complete size, capacity and overlap
// before writing a single byte. Decode rejects trailing bytes and nonminimal
// integers. Batch records have strictly increasing compact slots.
Result<std::size_t> encode_replica_message(const ReplicaWireMessage&,std::span<std::byte>) noexcept;
Result<ReplicaWireMessage> decode_replica_message(std::span<const std::byte>) noexcept;
Result<ReplicaLane> replica_lane(ReplicaWireKind) noexcept;
// Exact logical-carrier profile. State permits reliable unordered or a single
// unreliable frame. Whole-channel latest does not identify individual objects.
Status validate_replica_delivery(ReplicaWireKind,ReplicaLane,DeliveryMode) noexcept;
}
