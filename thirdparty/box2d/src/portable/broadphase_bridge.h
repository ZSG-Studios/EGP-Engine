// SPDX-License-Identifier: MIT
#pragma once
#include "box2d/collision.h"
#include "bitset.h"
#include "table.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SpBroadPhaseView {
 b2DynamicTree trees[3];b2BitSet movedProxies[3];
 struct {int *data;int count,capacity;} moveArray;
 b2HashSet pairSet;
 bool moveResults,movePairs;
 int movePairCapacity,movePairIndex;
} SpBroadPhaseView;
bool spExportBroadPhase(const void *,SpBroadPhaseView *);
bool spImportBroadPhaseCandidate(const SpBroadPhaseView *,void *);
bool spBroadPhaseFixtureRoundTrip(const SpBroadPhaseView *,SpBroadPhaseView *);
#ifdef __cplusplus
}
#endif
