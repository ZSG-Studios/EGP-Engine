// SPDX-License-Identifier: MIT
#pragma once
#include "owned_solver_constraints.hpp"
#include "owned_solver_sets_bridge.h"
namespace superpos::box2d_portable {
// Assembles the actual native b2SolverSet array (one per set slot) over the
// owned body, state, non-graph constraint and island payload storage. The
// native sets borrow that storage from the contained owner and must never be
// passed to native set destruction. No world root, cold arrays, geometry,
// events or engine adoption exist; no capability is enabled.
class OwnedSolverSets {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*sets_{};uint32_t count_{},stride_{};
 std::optional<OwnedSolverConstraints> prior_;
 OwnedSolverSets()noexcept=default;
 friend Result<OwnedSolverSets>assemble_owned_solver_sets(OwnedSolverConstraints&&,Allocator&)noexcept;
public:
 OwnedSolverSets(const OwnedSolverSets&)=delete;OwnedSolverSets&operator=(const OwnedSolverSets&)=delete;
 OwnedSolverSets(OwnedSolverSets&&)noexcept;OwnedSolverSets&operator=(OwnedSolverSets&&)=delete;~OwnedSolverSets();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedSolverConstraints&constraints()const noexcept{return *prior_;}
 uint32_t set_count()const noexcept{return count_;}
 const void*native_set(uint32_t slot)const noexcept{return slot<count_?static_cast<const std::byte*>(sets_)+size_t(slot)*stride_:nullptr;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
// Every assembled set is re-exported and must reproduce the owned membership
// (role, order, counts and exact capacities) before the prior owner moves.
// Failure preserves the prior owner and returns every new charge.
Result<OwnedSolverSets>assemble_owned_solver_sets(OwnedSolverConstraints&&,Allocator&)noexcept;
}
