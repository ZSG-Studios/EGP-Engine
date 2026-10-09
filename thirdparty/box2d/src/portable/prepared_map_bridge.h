// SPDX-License-Identifier: MIT
#pragma once
#include "world_ownership_bridge.h"
typedef struct SpPreparedEndpoints {int body_a,body_b,index_a,index_b;bool has_indices,active;} SpPreparedEndpoints;
#ifdef __cplusplus
extern "C" {
#endif
// O(1) extraction from an already admitted, frozen ownership view. Never
// interprets retained indices as offsets into today's solver arrays.
bool spReadPreparedEndpoints(const SpWorldOwnershipView*,enum SpOwnerKind,uint32_t,SpPreparedEndpoints*);
#ifdef __cplusplus
}
#endif
