// SPDX-License-Identifier: MIT
#pragma once
#include "world_capture_bridge.h"
#include "world_ownership.hpp"
#include "pool_codec.hpp"
namespace superpos::box2d_portable {
// Allocation slots outlive occupants. Their canonical namespace deliberately
// differs from the current/retired object lifetime namespaces.
constexpr uint32_t world_pool_slot_kind(SpWorldPool p)noexcept{return 0x4001u+uint32_t(p);}
class WorldPoolSlots;
Result<WorldPoolSlots> derive_world_pool_slots(const SpWorldCaptureView&,SpWorldPool,
 canonical::LifetimeRegistry&,canonical::RegistryLease,WorldMapStorage)noexcept;
class WorldPoolSlots {
 canonical::IdentityMap map_;const SpWorldCaptureView*source_;const canonical::LifetimeRegistry*registry_;canonical::RegistryLease lease_;SpWorldPool pool_;
 WorldPoolSlots(canonical::IdentityMap m,const SpWorldCaptureView&v,const canonical::LifetimeRegistry&r,canonical::RegistryLease l,SpWorldPool p)noexcept:map_(m),source_(&v),registry_(&r),lease_(l),pool_(p){}
 friend Result<WorldPoolSlots> derive_world_pool_slots(const SpWorldCaptureView&,SpWorldPool,canonical::LifetimeRegistry&,canonical::RegistryLease,WorldMapStorage)noexcept;
public:
 const canonical::IdentityMap&map()const noexcept{return map_;}
 const SpWorldCaptureView&source()const noexcept{return *source_;}
 SpWorldPool pool()const noexcept{return pool_;}
 bool lease_active()const noexcept{return registry_->lease_active(lease_);}
 bool overlaps_storage(const void*,size_t)const noexcept;
};
Status capture_world_pool(const WorldPoolSlots&,canonical::Identity,std::span<std::byte>,PoolImage&)noexcept;
}
