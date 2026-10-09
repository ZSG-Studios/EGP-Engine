// SPDX-License-Identifier: MIT
#pragma once
#include "owned_solver_world.hpp"
#include "world_root_bridge.h"
namespace superpos::box2d_portable {
constexpr std::uint32_t world_root_kind=0x1A01;
// Persistent b2World root scalars: gravity, contact/restitution/hit thresholds,
// speed limits, contact softness and recycling, enable flags, absolute step
// index, next-tick split island, end-event buffer index, the last step's
// inverse substep/step rates and capacity hints. Callbacks, callback contexts
// and world user data translate only through an admitted binding map.
// Non-finite floats are excluded from this profile.
struct WorldRootImage {std::array<canonical::Atom,31> atoms{};std::array<canonical::Field,26> fields{};canonical::Record record{};
 WorldRootImage()noexcept;WorldRootImage(const WorldRootImage&)=delete;WorldRootImage&operator=(const WorldRootImage&)=delete;};
std::span<const canonical::FieldSpec> world_root_fields()noexcept;
Status capture_world_root(const SpWorldRoot&,canonical::Identity,const canonical::IdentityMap&island,const canonical::PointerIdentityMap&bindings,WorldRootImage&)noexcept;
Status restore_world_root(const canonical::Record&,const canonical::IdentityMap&island,const canonical::PointerIdentityMap&bindings,SpWorldRoot&)noexcept;
// Owns a candidate native b2World root bound to the owned solver world: cold
// arrays, islands, solver sets, a copy of the owned graph descriptor and the
// five solver ID pools, plus the restored root scalars. The candidate is never
// registered or live; geometry, broadphase, events, task contexts, arena,
// world identity/generation and lifecycle flags are not restored here.
class OwnedWorldRoot {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*world_{};std::optional<OwnedSolverWorld> prior_;
 OwnedWorldRoot()noexcept=default;
 friend Result<OwnedWorldRoot>restore_owned_world_root(OwnedSolverWorld&&,const canonical::Record&,const canonical::PointerIdentityMap&,Allocator&)noexcept;
public:
 OwnedWorldRoot(const OwnedWorldRoot&)=delete;OwnedWorldRoot&operator=(const OwnedWorldRoot&)=delete;
 OwnedWorldRoot(OwnedWorldRoot&&)noexcept;OwnedWorldRoot&operator=(OwnedWorldRoot&&)=delete;~OwnedWorldRoot();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedSolverWorld&solver_world()const noexcept{return *prior_;}
 // Never pass to native world destruction or stepping.
 const void*native_world()const noexcept{return world_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
// The bound root must pass the native ownership validator and the root pool
// checks before the prior owner moves; failure preserves it.
Result<OwnedWorldRoot>restore_owned_world_root(OwnedSolverWorld&&,const canonical::Record&root,const canonical::PointerIdentityMap&bindings,Allocator&)noexcept;
}
