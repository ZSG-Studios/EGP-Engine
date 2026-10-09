// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
// Capture and restore under an exclusive native world lease. These pointers are
// caller-owned native staging only; no address is represented in the checkpoint.
typedef struct SpIslandLink { int object,body_a,body_b; } SpIslandLink;
typedef struct SpIslandView {
 int set_index,local_index,island_id,removed_constraints;
 int *bodies;SpIslandLink *contacts,*joints;
 uint32_t body_count,body_capacity,contact_count,contact_capacity,joint_count,joint_capacity;
} SpIslandView;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportIsland(const void *,SpIslandView *);
bool spImportIslandCandidate(const SpIslandView *,void *);
bool spIslandFixtureRoundTrip(const SpIslandView *,SpIslandView *);
#ifdef __cplusplus
}
#endif
