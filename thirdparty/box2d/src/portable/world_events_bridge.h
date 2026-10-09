// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct SpEventArray {void*data;uint32_t count,capacity;} SpEventArray;
// The nine world event arrays in native element types (box2d/types.h).
// end_* hold both end-event generations; the root selects the current one.
typedef struct SpWorldEventArrays {SpEventArray move,sensor_begin,contact_begin,sensor_end[2],contact_end[2],hit,joint;uint16_t world_id;} SpWorldEventArrays;
#ifdef __cplusplus
extern "C" {
#endif
// Borrowed pointers into the world's arrays; no copy is made.
bool spExportWorldEvents(const void*world,SpWorldEventArrays*);
// Binds owned arrays into a candidate root; the root never frees them.
bool spBindWorldEvents(void*world,const SpWorldEventArrays*);
#ifdef __cplusplus
}
#endif
