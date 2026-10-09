// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct SpNativeSolverSetLayout {uint32_t size,alignment,island_size,island_alignment;} SpNativeSolverSetLayout;
// Borrowed native payload arrays for one owned solver set. Every pointer is
// owned storage of the composing owner; the native set never frees them.
typedef struct SpOwnedSetArrays {
 void*bodies;uint32_t body_count,body_capacity;void*states;uint32_t state_count,state_capacity;
 void*joints;uint32_t joint_count,joint_capacity;void*contacts;uint32_t contact_count,contact_capacity;
 void*island_sims;uint32_t island_count,island_capacity;const int*island_ids;
} SpOwnedSetArrays;
#ifdef __cplusplus
extern "C" {
#endif
SpNativeSolverSetLayout spNativeSolverSetLayout(void);
// Writes a native b2SolverSet whose arrays reference the owned storage and
// builds its b2IslandSim array from island_ids. set_index -1 is a free slot,
// which must carry no arrays.
bool spAssembleOwnedSolverSet(void*,int set_index,const SpOwnedSetArrays*);
#ifdef __cplusplus
}
#endif
