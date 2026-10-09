// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lifetime_bridge.h"

// Borrowed native arrays under one exclusive completed-step world lease. Counts
// are native allocated prefixes, including free slots. No address is serialized.
typedef struct SpWorldOwnershipView {
 const void *bodies,*contacts,*joints,*islands,*sets,*graph;
 uint32_t body_count,contact_count,joint_count,island_count,set_count;
} SpWorldOwnershipView;
enum SpOwnerKind { SP_OWNER_BODY,SP_OWNER_CONTACT,SP_OWNER_JOINT,SP_OWNER_ISLAND,SP_OWNER_SET };
#ifdef __cplusplus
extern "C" {
#endif
// Solver/cold-object/island bijections only. Geometry, broadphase, application
// participants and actual engine capture-barrier enforcement remain separate.
// Read-only and allocation-free. Failure never publishes an ownership witness.
bool spValidateWorldOwnership(const SpWorldOwnershipView *);
bool spObserveWorldOwner(const SpWorldOwnershipView *,enum SpOwnerKind,uint32_t,SpNativeLifetime *);
bool spCopyWorldSolverBodies(const SpWorldOwnershipView *,uint32_t,int *,uint32_t,uint32_t *);
bool spWorldOwnershipOverlaps(const SpWorldOwnershipView *,const void *,uint64_t);
#ifdef __cplusplus
}
#endif
