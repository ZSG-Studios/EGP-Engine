// SPDX-License-Identifier: MIT
#pragma once
#include <box2d/types.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct SpChainView {int id,body,next;uint16_t generation;int *shapes;b2SurfaceMaterial *materials;uint32_t count,material_count,shape_capacity,material_capacity;} SpChainView;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportChain(const void *,SpChainView *);
bool spImportChainCandidate(const SpChainView *,void *);
bool spChainFixtureRoundTrip(const SpChainView *,SpChainView *);
#ifdef __cplusplus
}
#endif
