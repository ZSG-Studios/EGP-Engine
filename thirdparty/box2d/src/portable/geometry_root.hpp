// SPDX-License-Identifier: MIT
#pragma once
#include "world_root.hpp"
#include "shape_codec.hpp"
#include "chain_codec.hpp"
#include "broadphase_codec.hpp"
#include "tree_node_codec.hpp"
#include "geometry_root_bridge.h"
#include <superpos/canonical_versioned_map.hpp>
namespace superpos::box2d_portable {
// Canonical geometry participant records. Shape base records and their tagged
// geometry children, chain and sensor records are each sorted by identity.
// Sensor identities must be admitted in native dense sensor order. Tree node
// records are per body-type tree in node-identity order.
struct GeometryRecords {
 std::span<const canonical::Record> shapes,shape_geometry,chains,sensors;
 std::array<const canonical::Record*,3> trees{};std::array<std::span<const canonical::Record>,3> nodes;
 const canonical::Record*moves{},*pairs{},*broadphase{};
};
// Destination-admitted maps that the owned pools do not provide: tree node
// slot identities per body-type tree, retained sensor-visitor lifetimes, shape
// user-data bindings and the broadphase child record identities.
struct GeometryMaps {
 std::array<const canonical::IdentityMap*,3> nodes{};
 const canonical::VersionedIdentityMap*history{};
 const canonical::PointerIdentityMap*bindings{};
 BroadPhaseIds ids{};
};
// Owns native shapes, chains (with segment/material arrays), sensors (with
// visitor arrays) and the complete broadphase (three trees, move buffer and
// bitsets, pair table), plus a new candidate root: a copy of the prior
// solver root with geometry, broadphase and the shape/chain ID pools bound.
// The prior root is never modified. The complete root must pass the native
// world and geometry ownership validators and all seven pool checks before
// the prior owner moves. Events, task contexts, arena, world identity and
// lifecycle flags remain unrestored; the candidate is never registered or live.
class OwnedGeometryRoot {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*world_{};const SpWorldOwnershipView*ownership_{};const SpGeometryOwnershipView*geometry_{};
 std::array<std::optional<canonical::IdentityMap>,3> proxies_;std::optional<canonical::IdentityMap> sensors_;std::optional<OwnedWorldRoot> prior_;
 OwnedGeometryRoot()noexcept=default;
 friend Result<OwnedGeometryRoot>restore_owned_geometry(OwnedWorldRoot&&,const GeometryRecords&,const GeometryMaps&,Allocator&)noexcept;
public:
 OwnedGeometryRoot(const OwnedGeometryRoot&)=delete;OwnedGeometryRoot&operator=(const OwnedGeometryRoot&)=delete;
 OwnedGeometryRoot(OwnedGeometryRoot&&)noexcept;OwnedGeometryRoot&operator=(OwnedGeometryRoot&&)=delete;~OwnedGeometryRoot();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedWorldRoot&solver_root()const noexcept{return *prior_;}
 // Never pass to native world destruction or stepping.
 const void*native_world()const noexcept{return world_;}
 const SpWorldOwnershipView&ownership_view()const noexcept{return *ownership_;}
 const SpGeometryOwnershipView&geometry_view()const noexcept{return *geometry_;}
 // Destination leaf-proxy maps (derived from restored tree leaves) and the
 // dense sensor map; valid while this owner lives.
 const canonical::IdentityMap&proxy_map(size_t tree)const noexcept{return *proxies_[tree];}
 const canonical::IdentityMap&sensor_map()const noexcept{return *sensors_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
Result<OwnedGeometryRoot>restore_owned_geometry(OwnedWorldRoot&&,const GeometryRecords&,const GeometryMaps&,Allocator&)noexcept;
}
