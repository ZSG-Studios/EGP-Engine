// SPDX-License-Identifier: MIT
#pragma once
#include "owned_solver_bodies.hpp"
#include "owned_islands_bridge.h"
namespace superpos::box2d_portable {
// Owns actual native island records and nested capacities together with prior
// solver-body storage. Cold constraints, graph, geometry, events and application
// participants remain outside this component. No engine adoption is exposed.
class OwnedIslands {
 Allocator*allocator_{};void*block_{};size_t bytes_{};
 std::optional<OwnedSolverBodies> bodies_;void*islands_{};uint32_t count_{},stride_{};
 OwnedIslands()noexcept=default;
 friend Result<OwnedIslands>restore_owned_islands(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,SolverBodyRecords,std::span<const canonical::Record>,Allocator&)noexcept;
public:
 OwnedIslands(const OwnedIslands&)=delete;OwnedIslands&operator=(const OwnedIslands&)=delete;
 OwnedIslands(OwnedIslands&&)noexcept;OwnedIslands&operator=(OwnedIslands&&)=delete;~OwnedIslands();
 const OwnedSolverBodies&solver_bodies()const noexcept{return *bodies_;}
 const void*native_island(uint32_t slot)const noexcept{return slot<count_?static_cast<const std::byte*>(islands_)+size_t(slot)*stride_:nullptr;}
 uint32_t allocated_count()const noexcept{return count_;}
 bool has_storage()const noexcept{return bodies_.has_value();}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(bodies_?bodies_->reserved_payload_bytes():0);}
};
Result<OwnedIslands>restore_owned_islands(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,SolverBodyRecords,std::span<const canonical::Record>,Allocator&)noexcept;
}
