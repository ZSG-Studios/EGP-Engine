// SPDX-License-Identifier: MIT
#pragma once
#include "world_pool_slots.hpp"
#include "geometry_ownership.hpp"
#include <superpos/allocator.hpp>
#include <optional>
namespace superpos::box2d_portable {
struct PoolOccupantDefinition {
 uint32_t slot_kind{},object_kind{},native_generation_max{};
 std::array<canonical::FieldSpec,2> fields{};
};
Result<PoolOccupantDefinition> pool_occupant_definition(SpWorldPool)noexcept;
struct PoolOccupantCell {
 std::array<canonical::Atom,2> atoms{};
 std::array<canonical::Field,2> fields{};
 PoolOccupantCell()noexcept;
 PoolOccupantCell(const PoolOccupantCell&)=delete;
 PoolOccupantCell&operator=(const PoolOccupantCell&)=delete;
};
struct PoolOccupantStorage {std::span<PoolOccupantCell> cells;std::span<canonical::Record> records;};
Result<std::span<const canonical::Record>> capture_pool_occupants(const WorldPoolSlots&,const WorldOwnerMap&,PoolOccupantStorage)noexcept;
Result<std::span<const canonical::Record>> capture_pool_occupants(const WorldPoolSlots&,const GeometryOwnerMap&,PoolOccupantStorage)noexcept;

// Owns one fallibly allocated Recovery block containing all slot/live mappings,
// native generations and native free order. No engine world points into it yet.
// The allocator must outlive this object and any eventual adopted owner.
class OwnedPoolCandidate {
 Allocator*allocator_{};void*block_{};size_t bytes_{};
 std::optional<canonical::IdentityMap> slots_,objects_;
 std::span<uint32_t> generations_;
 SpPoolView pool_{};
 OwnedPoolCandidate()noexcept=default;
 friend Result<OwnedPoolCandidate> restore_pool_occupants(SpWorldPool,std::span<const canonical::Record>,const canonical::Record&,Allocator&)noexcept;
 friend Result<OwnedPoolCandidate> restore_pool_occupants_placed(SpWorldPool,std::span<const canonical::Record>,const canonical::Record&,std::span<const uint32_t>,Allocator&)noexcept;
public:
 OwnedPoolCandidate(const OwnedPoolCandidate&)=delete;
 OwnedPoolCandidate&operator=(const OwnedPoolCandidate&)=delete;
 OwnedPoolCandidate(OwnedPoolCandidate&&)noexcept;
 OwnedPoolCandidate&operator=(OwnedPoolCandidate&&)=delete;
 ~OwnedPoolCandidate();
 const canonical::IdentityMap&slot_map()const noexcept{return *slots_;}
 const canonical::IdentityMap&object_map()const noexcept{return *objects_;}
 std::span<const uint32_t>native_generations()const noexcept{return generations_;}
 const SpPoolView&pool()const noexcept{return pool_;}
 size_t backing_bytes()const noexcept{return bytes_;}
};
// Destination native slots are chosen deterministically from canonical slot
// order. Every occupied identity, native generation and free-list membership is
// checked before the private candidate is returned. Failure releases its block.
Result<OwnedPoolCandidate> restore_pool_occupants(SpWorldPool,std::span<const canonical::Record>,const canonical::Record&,Allocator&)noexcept;
// Placement is a validated permutation, not caller-authored identity bindings.
// Semantic reserved roles must still be established by the composing owner.
Result<OwnedPoolCandidate> restore_pool_occupants_placed(SpWorldPool,std::span<const canonical::Record>,const canonical::Record&,std::span<const uint32_t>,Allocator&)noexcept;
}
