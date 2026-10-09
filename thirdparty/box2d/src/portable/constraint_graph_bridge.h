// SPDX-License-Identifier: MIT
#pragma once
#include "graph_color_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
// Caller-owned flattened output wrappers have count entries; native candidate
// storage is separately reserved at exact b2ContactSim/b2JointSim capacities.
bool spConstraintGraphFixtureRoundTrip(const SpGraphColorView *in,SpGraphColorView *out,uint32_t colors);
#ifdef __cplusplus
}
#endif
