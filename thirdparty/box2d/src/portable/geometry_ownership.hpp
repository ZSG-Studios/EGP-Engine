// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_ownership_bridge.h"
#include "world_ownership.hpp"
namespace superpos::box2d_portable {
struct GeometryOwnershipScratch {
 std::span<uint8_t> marks;
 std::span<uint64_t> contact_keys,pair_keys;
};
class GeometryOwnership;
Result<GeometryOwnership> validate_geometry_ownership(const SpGeometryOwnershipView&,
 const WorldOwnerMap&,GeometryOwnershipScratch)noexcept;
// Borrows an admitted frozen native world, its body witness and active lease.
// This proves ownership relations only; typed payload and complete tree topology
// are still validated by their existing canonical codecs before publication.
class GeometryOwnership {
 const SpGeometryOwnershipView*source_;const WorldOwnerMap*bodies_;
 GeometryOwnership(const SpGeometryOwnershipView&v,const WorldOwnerMap&b)noexcept:source_(&v),bodies_(&b){}
 friend Result<GeometryOwnership> validate_geometry_ownership(const SpGeometryOwnershipView&,const WorldOwnerMap&,GeometryOwnershipScratch)noexcept;
public:
 const SpGeometryOwnershipView&source()const noexcept{return *source_;}
 bool lease_active()const noexcept{return bodies_->lease_active();}
 bool overlaps_storage(const void*p,size_t bytes)const noexcept;
};
// Current shape/chain identities derive from actual 16-bit native lifetime
// observations and explicit free-slot lists, never a caller-authored binding.
class GeometryOwnerMap;
Result<GeometryOwnerMap> derive_geometry_map(const GeometryOwnership&,bool chain,
 canonical::LifetimeRegistry&,canonical::RegistryLease,std::span<const int>,WorldMapStorage)noexcept;
class GeometryOwnerMap {
 canonical::IdentityMap map_;const GeometryOwnership*owner_;const canonical::LifetimeRegistry*registry_;canonical::RegistryLease lease_;bool chain_;
 GeometryOwnerMap(canonical::IdentityMap m,const GeometryOwnership&o,const canonical::LifetimeRegistry&r,canonical::RegistryLease l,bool c)noexcept:map_(m),owner_(&o),registry_(&r),lease_(l),chain_(c){}
 friend Result<GeometryOwnerMap> derive_geometry_map(const GeometryOwnership&,bool,canonical::LifetimeRegistry&,canonical::RegistryLease,std::span<const int>,WorldMapStorage)noexcept;
public:
 const canonical::IdentityMap&map()const noexcept{return map_;}
 const SpGeometryOwnershipView&source()const noexcept{return owner_->source();}
 bool chain()const noexcept{return chain_;}
 bool lease_active()const noexcept{return owner_->lease_active()&&registry_->lease_active(lease_);}
 bool overlaps_storage(const void*,size_t)const noexcept;
};
// A current leaf proxy is the shape's logical broadphase component, not an
// independently retained native node lifetime. Internal node maps are separate.
Result<canonical::IdentityMap> derive_shape_proxy_map(const GeometryOwnerMap&,uint32_t tree_type,
 std::span<canonical::NativeBinding>,std::span<uint32_t>)noexcept;
}
