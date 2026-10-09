// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
// Borrowed native buffer, valid only under the owning world's capture/stage lease.
// Pointers and native slot integers in this view never appear in the wire record.
typedef struct SpPoolView {const int *free_entries;uint32_t free_count,free_capacity,allocated_count;} SpPoolView;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportPool(const void *,SpPoolView *);
bool spRestorePoolCandidate(const SpPoolView *,void *);
bool spPoolFixtureRoundTrip(const SpPoolView *,int *,uint32_t,SpPoolView *);
#ifdef __cplusplus
}
#endif
