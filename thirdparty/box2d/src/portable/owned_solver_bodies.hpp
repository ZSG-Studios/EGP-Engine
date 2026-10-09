// SPDX-License-Identifier: MIT
#pragma once
#include "world_pool_candidates.hpp"
#include "solver_set_codec.hpp"
namespace superpos::box2d_portable {
struct SolverBodyRecords {
 std::span<const canonical::Record> sets,bodies,states;
};
struct OwnedSetBodies {
 SpSolverMembership membership{};
 std::span<b2BodySim> bodies;
 std::span<b2BodyState> states;
};
// Owns seven pool tables, validated solver memberships and native body/state
// array capacities. Constraint/geometry/root/event/application payloads are not
// owned here. No engine adoption or complete-world capability is provided.
class OwnedSolverBodies {
 Allocator*allocator_{};void*block_{};size_t bytes_{};
 std::optional<WorldPoolCandidates> pools_;
 std::span<OwnedSetBodies> sets_;
 OwnedSolverBodies()noexcept=default;
 friend Result<OwnedSolverBodies> restore_owned_solver_bodies(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,SolverBodyRecords,Allocator&)noexcept;
public:
 OwnedSolverBodies(const OwnedSolverBodies&)=delete;
 OwnedSolverBodies&operator=(const OwnedSolverBodies&)=delete;
 OwnedSolverBodies(OwnedSolverBodies&&)noexcept;
 OwnedSolverBodies&operator=(OwnedSolverBodies&&)=delete;
 ~OwnedSolverBodies();
 const WorldPoolCandidates& pools()const noexcept{return *pools_;}
 std::span<const OwnedSetBodies>sets()const noexcept{return sets_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(pools_?pools_->reserved_payload_bytes():0);}
};
// Inputs are immutable canonical records sorted by simulation identity. Native
// set roles determine placement: static=0, disabled=1, awake=2, sleeping>=3.
// On failure all partial payloads/tables are destroyed and charges returned.
Result<OwnedSolverBodies> restore_owned_solver_bodies(const std::array<PoolCandidateInput,SP_POOL_COUNT>&,SolverBodyRecords,Allocator&)noexcept;
}
