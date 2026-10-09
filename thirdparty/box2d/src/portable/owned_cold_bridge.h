// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct SpColdArrayLayout {uint32_t contact_size,contact_alignment,joint_size,joint_alignment;} SpColdArrayLayout;
#ifdef __cplusplus
extern "C" {
#endif
SpColdArrayLayout spColdArrayLayout(void);
// Normalized free native slots: null ID and links, retained native generation.
bool spPrepareFreeColdContact(void*,uint32_t generation);
bool spPrepareFreeColdJoint(void*,uint32_t generation);
#ifdef __cplusplus
}
#endif
