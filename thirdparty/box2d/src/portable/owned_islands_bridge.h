// SPDX-License-Identifier: MIT
#pragma once
#include "island_bridge.h"
typedef struct SpNativeIslandLayout {uint32_t size,alignment,link_size,link_alignment;} SpNativeIslandLayout;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SpOwnedIslandDescriptor {int*bodies;uint32_t body_capacity;} SpOwnedIslandDescriptor;
SpNativeIslandLayout spNativeIslandLayout(void);
SpOwnedIslandDescriptor spOwnedIslandDescriptor(const void*);
bool spPrepareOwnedIsland(void*,int*,uint32_t,void*,uint32_t,void*,uint32_t);
#ifdef __cplusplus
}
#endif
