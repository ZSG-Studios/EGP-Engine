// SPDX-License-Identifier: MIT
#pragma once
#include "owned_solver_sets.hpp"
#include "owned_cold_bridge.h"
#include "world_ownership_bridge.h"
namespace superpos::box2d_portable {
// Owns the native cold b2Body/b2Contact/b2Joint arrays beside the assembled
// solver sets, owned islands and owned graph. The complete owned solver view
// must pass the native ownership validator (reciprocal cold/solver/island
// membership, body contact/joint list heads, counts and edge lists) before the
// prior owner moves. Body-move events are referenced through a caller map and
// are not owned here. No world root, geometry, broadphase, event arrays,
// application participants or engine adoption exist; no capability is enabled.
class OwnedSolverWorld {
 Allocator*allocator_{};void*block_{};size_t bytes_{};b2Body*bodies_{};void*contacts_{},*joints_{};uint32_t contact_stride_{},joint_stride_{};
 SpWorldOwnershipView view_{};std::optional<OwnedSolverSets> prior_;
 OwnedSolverWorld()noexcept=default;
 friend Result<OwnedSolverWorld>restore_owned_solver_world(OwnedSolverSets&&,std::span<const canonical::Record>,const canonical::IdentityMap&,Allocator&,const canonical::PointerIdentityMap*)noexcept;
public:
 OwnedSolverWorld(const OwnedSolverWorld&)=delete;OwnedSolverWorld&operator=(const OwnedSolverWorld&)=delete;
 OwnedSolverWorld(OwnedSolverWorld&&)noexcept;OwnedSolverWorld&operator=(OwnedSolverWorld&&)=delete;~OwnedSolverWorld();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedSolverSets&sets()const noexcept{return *prior_;}
 // Borrowed view over owned storage; valid while this owner lives.
 const SpWorldOwnershipView&ownership_view()const noexcept{return view_;}
 const b2Body*native_body(uint32_t i)const noexcept{return i<view_.body_count?bodies_+i:nullptr;}
 const void*native_contact(uint32_t i)const noexcept{return i<view_.contact_count?static_cast<const std::byte*>(contacts_)+size_t(i)*contact_stride_:nullptr;}
 const void*native_joint(uint32_t i)const noexcept{return i<view_.joint_count?static_cast<const std::byte*>(joints_)+size_t(i)*joint_stride_:nullptr;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
// Cold body records must cover every live body; their set/local and island
// membership and native generation must match the owned placement. Failure
// preserves the prior owner and returns the new charge.
Result<OwnedSolverWorld>restore_owned_solver_world(OwnedSolverSets&&,std::span<const canonical::Record> cold_bodies,const canonical::IdentityMap&body_move_events,Allocator&)noexcept;
// The same with an admitted binding map for cold-body user data (null admits
// only null user data).
Result<OwnedSolverWorld>restore_owned_solver_world(OwnedSolverSets&&,std::span<const canonical::Record> cold_bodies,const canonical::IdentityMap&body_move_events,Allocator&,const canonical::PointerIdentityMap*bindings)noexcept;
}
