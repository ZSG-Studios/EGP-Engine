// SPDX-License-Identifier: MIT
#pragma once
#include "pool_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SpNativeLifetime {int native_id;uint32_t generation;} SpNativeLifetime;
bool spLifecyclePoolAllocate(SpPoolView *,uint32_t maximum,int *);
bool spLifecyclePoolFree(SpPoolView *,int);
bool spObserveBodyLifetimes(const void *,uint32_t,SpNativeLifetime *);
#ifdef __cplusplus
}
#endif
