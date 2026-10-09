// SPDX-License-Identifier: MIT
#pragma once
#include "pool_bridge.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Read-only access for digest traversal of any native world or candidate
// root. Pool order follows SpWorldPool.
bool spWorldPoolView(const void*world,uint32_t pool,SpPoolView*);
// Native generation retained by one allocation slot (0 for generationless
// island and solver-set pools).
bool spWorldSlotGeneration(const void*world,uint32_t pool,uint32_t slot,uint32_t*generation);
#ifdef __cplusplus
}
#endif
