// SPDX-License-Identifier: MIT
#pragma once
#include "world_ownership_bridge.h"
typedef struct SpGeometryOwnershipView {
 const SpWorldOwnershipView*world;
 const void*shapes,*chains,*sensors,*broadphase;
 uint32_t shape_count,chain_count,sensor_count;
} SpGeometryOwnershipView;
#ifdef __cplusplus
extern "C" {
#endif
bool spGeometryOwnershipOverlaps(const SpGeometryOwnershipView*,const void*,uint64_t);
// Writes caller-reserved marks and unordered pair keys. The C++ composition
// boundary must sort/compare both key columns before issuing a witness.
bool spValidateGeometryOwnership(const SpGeometryOwnershipView*,uint8_t*marks,uint32_t mark_capacity,
 uint64_t*contact_keys,uint64_t*pair_keys,uint32_t key_capacity,uint32_t*key_count);
bool spObserveGeometryOwner(const SpGeometryOwnershipView*,bool chain,uint32_t,SpNativeLifetime*);
bool spObserveShapeProxy(const SpGeometryOwnershipView*,uint32_t,int*node,int*type);
#ifdef __cplusplus
}
#endif
