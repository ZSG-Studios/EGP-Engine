// SPDX-License-Identifier: MIT
#pragma once
#include "solver_set_bridge.h"
#include "joint_sim_bridge.h"
#include "contact_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
// Body pointers refer to actual pinned-native b2BodySim/b2BodyState arrays.
// Joint/contact pointers refer to flattened private staging wrappers.
typedef struct SpSolverPayload {
 int set_index; void *body_sims,*body_states;SpJointSim *joints;SpContactSim *contacts;int *islands;
 uint32_t count[5],capacity[5];
} SpSolverPayload;
bool spExportSolverPayload(const void *,SpSolverPayload *,const uint32_t storage[5]);
bool spImportSolverPayloadCandidate(const SpSolverPayload *,void *);
bool spSolverPayloadFixtureRoundTrip(const SpSolverPayload *,SpSolverPayload *,const uint32_t storage[5]);
#ifdef __cplusplus
}
#endif
