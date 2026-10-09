// SPDX-License-Identifier: MIT
#pragma once
#include "world_ownership_bridge.h"
#include <superpos/canonical_lifetime_registry.hpp>
namespace superpos::box2d_portable {
struct WorldMapStorage {
 std::span<canonical::NativeLifetime> native;
 std::span<uint8_t> marks;
 std::span<canonical::NativeBinding> bindings;
 std::span<uint32_t> order;
};
class WorldOwnerMap;
Result<WorldOwnerMap> derive_world_owner_map(const SpWorldOwnershipView&,SpOwnerKind,
 canonical::LifetimeRegistry&,canonical::RegistryLease,std::span<const int>,WorldMapStorage)noexcept;
// Borrowed witness: exclusive world/registry leases and all charged buffers must
// remain alive and frozen. Only the checked construction below creates one.
class WorldOwnerMap {
 canonical::IdentityMap map_;const SpWorldOwnershipView*source_;SpOwnerKind kind_;const canonical::LifetimeRegistry*registry_;canonical::RegistryLease lease_;
 WorldOwnerMap(canonical::IdentityMap m,const SpWorldOwnershipView&v,SpOwnerKind k,const canonical::LifetimeRegistry&r,canonical::RegistryLease l)noexcept:map_(m),source_(&v),kind_(k),registry_(&r),lease_(l){}
 friend Result<WorldOwnerMap> derive_world_owner_map(const SpWorldOwnershipView&,SpOwnerKind,canonical::LifetimeRegistry&,canonical::RegistryLease,std::span<const int>,WorldMapStorage)noexcept;
public:
 const canonical::IdentityMap&map()const noexcept{return map_;}
 const SpWorldOwnershipView&source()const noexcept{return *source_;}
 SpOwnerKind kind()const noexcept{return kind_;}
 bool lease_active()const noexcept{return registry_->lease_active(lease_);}
 bool overlaps_storage(const void*p,size_t bytes)const noexcept{return map_.overlaps_storage(p,bytes)||registry_->overlaps_storage(p,bytes);}
};
// Current solver-array indices only. Never substitutes for retained per-record
// prepared indices in sleeping or disabled constraints.
Result<canonical::IdentityMap> derive_current_solver_body_map(const WorldOwnerMap&,uint32_t,
 std::span<int>,std::span<canonical::NativeBinding>,std::span<uint32_t>)noexcept;
}
