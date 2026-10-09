// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_ownership_bridge.h"
#include "pool_bridge.h"
#include <box2d/types.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct SpGeometryLayout {uint32_t chain_size,chain_alignment,sensor_size,sensor_alignment,visitor_size,visitor_alignment;} SpGeometryLayout;
#ifdef __cplusplus
extern "C" {
#endif
SpGeometryLayout spGeometryLayout(void);
// Shallow copy of a candidate root; borrowed arrays stay owned elsewhere.
bool spCopyWorldRoot(void*destination,const void*source);
void*spWorldRootBroadPhase(void*world);
// Native chain/sensor candidates over caller-owned arrays at exact sizes.
bool spPrepareChainCandidate(void*chain,int*shapes,uint32_t count,b2SurfaceMaterial*materials,uint32_t material_count);
bool spPrepareFreeChain(void*chain,uint32_t generation);
bool spPrepareSensorCandidate(void*sensor,void*hits,uint32_t hit_capacity,void*first,uint32_t first_capacity,void*second,uint32_t second_capacity);
// Binds shape/chain/sensor arrays and the shape/chain ID pools into a root.
bool spBindWorldRootGeometry(void*world,void*shapes,uint32_t shape_count,void*chains,uint32_t chain_count,void*sensors,uint32_t sensor_count,const SpPoolView*shape_pool,const SpPoolView*chain_pool);
// Shape/chain pool extents equal array counts; free entries are exactly the free slots.
bool spValidateWorldRootGeometryPools(const void*world);
bool spWorldRootGeometryView(const void*world,const SpWorldOwnershipView*ownership,SpGeometryOwnershipView*out);
#ifdef __cplusplus
}
#endif
