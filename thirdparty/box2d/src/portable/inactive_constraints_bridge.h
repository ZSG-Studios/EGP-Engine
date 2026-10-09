// SPDX-License-Identifier: MIT
#pragma once
#include "contact_bridge.h"
#include "joint_sim_bridge.h"
#include "cold_contact_bridge.h"
typedef struct SpInactiveConstraintLayout {uint32_t contact_size,contact_alignment,joint_size,joint_alignment;} SpInactiveConstraintLayout;
#ifdef __cplusplus
extern "C" {
#endif
SpInactiveConstraintLayout spInactiveConstraintLayout(void);
// Native solver-set array admissibility for a non-graph contact. Static sets
// hold no contacts; disabled and awake arrays hold only non-touching contacts
// with no manifold points; sleeping sets hold only touching contacts.
bool spInactiveContactAdmissible(const SpContactSim*,int set_index);
// Selected tagged-member prepared index (endpoint 0=A, 1=B). Returns NULL for
// filter joints and unknown tags; inactive union members are never touched.
int*spInactiveJointIndex(SpJointSim*,int endpoint);
// Native touching flag of a cold contact record.
bool spColdContactTouching(const SpColdContact*);
#ifdef __cplusplus
}
#endif
