// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_ownership_bridge.h"
#include "pool_bridge.h"
enum SpWorldPool {SP_POOL_BODY,SP_POOL_CONTACT,SP_POOL_JOINT,SP_POOL_ISLAND,SP_POOL_SET,SP_POOL_SHAPE,SP_POOL_CHAIN,SP_POOL_COUNT};
enum SpWorldEvent {SP_EVENT_BODY_MOVE,SP_EVENT_SENSOR_BEGIN,SP_EVENT_CONTACT_BEGIN,SP_EVENT_SENSOR_END_0,SP_EVENT_SENSOR_END_1,SP_EVENT_CONTACT_END_0,SP_EVENT_CONTACT_END_1,SP_EVENT_CONTACT_HIT,SP_EVENT_JOINT,SP_EVENT_COUNT};
typedef struct SpWorldArrayView {const void*data;uint32_t count,capacity,element_bytes;} SpWorldArrayView;
// Borrowed structural capture only. All addresses remain native and private.
// This is not a portable record or a complete-world capability assertion.
typedef struct SpWorldCaptureView {
 const void*source;
 SpWorldOwnershipView ownership;
 SpGeometryOwnershipView geometry;
 SpPoolView pools[SP_POOL_COUNT];
 SpWorldArrayView cold[8],events[SP_EVENT_COUNT];
 uint64_t step_index;
 int split_island_id,end_event_array_index;
 uint32_t workers;
 uint16_t native_generation,native_world_id;
} SpWorldCaptureView;
#ifdef __cplusplus
extern "C" {
#endif
// Reads the actual root and requires an unlocked, in-use world, no active task,
// no pending tree/query task, and an empty stack arena. Retains event buffers.
// The caller must still hold the engine owner-thread lease for the entire use.
bool spCaptureWorldStructure(const void*,SpWorldCaptureView*);
// Full allocated-capacity guard including root-only arrays, pool free lists,
// worker bitsets, event buffers and allocated arena/rebuild scratch.
bool spWorldCaptureOverlaps(const void*,const void*,uint64_t);
bool spObserveWorldCaptureSlot(const SpWorldCaptureView*,enum SpWorldPool,uint32_t,SpNativeLifetime*);
// Identity/structural revalidation only: in-place payload writes still require
// the continuously held engine owner-thread freeze, not repeated comparisons.
bool spWorldCaptureMatches(const SpWorldCaptureView*);
#ifdef __cplusplus
}
#endif
