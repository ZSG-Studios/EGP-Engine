// SPDX-License-Identifier: MIT
#pragma once
#include "visitor_bridge.h"
typedef struct SpSensorView {int shape;SpVisitor *hits,*overlaps1,*overlaps2;uint32_t hit_count,hit_capacity,first_count,first_capacity,second_count,second_capacity;} SpSensorView;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportSensor(const void *,SpSensorView *);
bool spImportSensorCandidate(const SpSensorView *,void *);
bool spSensorFixtureRoundTrip(const SpSensorView *,SpSensorView *);
#ifdef __cplusplus
}
#endif
