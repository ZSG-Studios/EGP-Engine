// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct SpVisitor {int shape;uint16_t generation;} SpVisitor;
#ifdef __cplusplus
extern "C" {
#endif
bool spVisitorNativeRoundTrip(const SpVisitor *,SpVisitor *);
#ifdef __cplusplus
}
#endif
