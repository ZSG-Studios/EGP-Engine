// SPDX-License-Identifier: MIT
#pragma once
#include "graph_color_bridge.h"
typedef struct SpOwnedGraphLayout {uint32_t size,alignment,contact_size,contact_alignment,joint_size,joint_alignment;} SpOwnedGraphLayout;
#ifdef __cplusplus
extern "C" {
#endif
SpOwnedGraphLayout spOwnedGraphLayout(void);
bool spPrepareOwnedGraph(void*);
bool spPrepareOwnedGraphColor(void*,uint32_t,void*,uint32_t,void*,uint32_t);
const void*spOwnedGraphColor(const void*,uint32_t);
// Read-only access to one stored owned graph slot (no allocation or callback).
uint32_t spOwnedGraphColorCount(const void*,uint32_t color,bool joints);
bool spOwnedGraphContactAt(const void*,uint32_t color,uint32_t local,int*contact_id);
bool spOwnedGraphJointAt(const void*,uint32_t color,uint32_t local,int*joint_id,int*type,int*body_a,int*body_b);
#ifdef __cplusplus
}
#endif
