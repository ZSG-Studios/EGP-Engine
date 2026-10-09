// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
// Only native membership IDs are staged here. Each referenced simulation
// record must be separately captured/restored by its complete typed codec.
// Bodies order also supplies the identities of aligned awake body states.
typedef struct SpSolverMembership {
 int set_index;int *body_ids,*joint_ids,*contact_ids,*island_ids;
 uint32_t body_count,body_capacity,state_count,state_capacity,joint_count,joint_capacity,contact_count,contact_capacity,island_count,island_capacity;
} SpSolverMembership;
#ifdef __cplusplus
extern "C" {
#endif
bool spIslandSimNativeRoundTrip(int,int *);
bool spExportSolverMembership(const void *,SpSolverMembership *);
// Fixture builds a genuine native set with these IDs. It does not establish
// solver payload coverage; the independent typed component fixtures do that.
bool spSolverMembershipFixtureRoundTrip(const SpSolverMembership *,SpSolverMembership *);
#ifdef __cplusplus
}
#endif
