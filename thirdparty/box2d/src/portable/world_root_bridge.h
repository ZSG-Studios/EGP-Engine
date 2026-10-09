// SPDX-License-Identifier: MIT
#pragma once
#include "world_ownership_bridge.h"
#include "pool_bridge.h"
#include <stdint.h>
#include <stdbool.h>
// Persistent b2World root scalars and process-local bindings. Function and
// context pointers are carried only for admitted binding translation; their
// addresses never enter canonical data.
typedef struct SpWorldRoot {
 float gravity[2],hitEventThreshold,restitutionThreshold,maxLinearSpeed,contactSpeed,contactHertz,contactDampingRatio,contactRecycleDistance,inv_h,inv_dt;
 bool enableSleep,enableWarmStarting,enableContactSoftening,enableContinuous,enableSpeculative;
 uint64_t stepIndex;int splitIslandId,endEventArrayIndex;int maxCapacity[5];
 void*frictionCallback,*restitutionCallback,*preSolveFcn,*preSolveContext,*customFilterFcn,*customFilterContext,*userData;
} SpWorldRoot;
typedef struct SpWorldRootLayout {uint32_t size,alignment;} SpWorldRootLayout;
// Owned solver arrays bound into a candidate root. Every pointer is owned by
// the composing owner; the root never frees them.
typedef struct SpWorldRootArrays {
 void*bodies,*contacts,*joints,*islands,*sets;const void*graph;uint32_t body_count,contact_count,joint_count,island_count,set_count;
 SpPoolView body_pool,contact_pool,joint_pool,island_pool,set_pool;
} SpWorldRootArrays;
#ifdef __cplusplus
extern "C" {
#endif
SpWorldRootLayout spWorldRootLayout(void);
bool spExportWorldRoot(const void*world,SpWorldRoot*);
// Writes only the root scalars and bindings above.
bool spImportWorldRoot(const SpWorldRoot*,void*world);
// Zeroes the candidate root, then binds solver arrays, the five solver ID
// pools and a copy of the owned graph descriptor. Geometry, broadphase,
// events, task contexts, arena, registry identity and lifecycle flags stay
// zero/null: the candidate is never a live or registered world.
bool spAssembleWorldRoot(void*world,const SpWorldRootArrays*);
bool spWorldRootOwnershipView(const void*world,SpWorldOwnershipView*);
// Each solver pool extent equals its array count; free entries are unique,
// in range and exactly the free native slots. The split island is absent or
// a live island.
bool spValidateWorldRootPools(const void*world);
#ifdef __cplusplus
}
#endif
