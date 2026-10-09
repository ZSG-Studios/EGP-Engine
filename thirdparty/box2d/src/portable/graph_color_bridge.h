// SPDX-License-Identifier: MIT
#pragma once
#include "contact_bridge.h"
#include "joint_sim_bridge.h"
#include "bitset.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SpGraphColorView {
 b2BitSet body_set;
 SpContactSim *contacts;SpJointSim *joints;
 uint32_t contact_count,contact_capacity,joint_count,joint_capacity;
} SpGraphColorView;
// Stale transient solver pointers and counts are deliberately never exported.
bool spExportGraphColor(const void *,SpGraphColorView *,uint32_t contact_storage,uint32_t joint_storage);
bool spImportGraphColorCandidate(const SpGraphColorView *,void *);
bool spGraphColorFixtureRoundTrip(const SpGraphColorView *,SpGraphColorView *,uint32_t contact_storage,uint32_t joint_storage);
uint32_t spGraphTouchingFlag(void);
uint32_t spGraphColorCount(void);
#ifdef __cplusplus
}
#endif
