// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
// Runtime scratch of b2World. At the completed-step capture barrier these
// fields carry no state that a later step reads before rewriting it:
// - per-worker b2TaskContext bitsets/hits/split candidates and per-worker
//   b2SensorTaskContext event bits are cleared or reset at the start of the
//   stage that uses them (physics_world.c b2Collide, solver.c b2SolverStep,
//   sensor.c b2OverlapSensors in the audited sources);
// - the stack arena must be drained; debug sets are cleared at the start of
//   b2World_Draw; profile and taskCount are diagnostics reset at step start;
// - locked, activeTaskCount and userTreeTask must be quiescent.
#ifdef __cplusplus
extern "C" {
#endif
// Enforced capture barrier: unlocked, no active task, no user tree task,
// drained arena and task/sensor-task context arrays sized to the worker count.
bool spValidateRuntimeScratchBarrier(const void*world);
// Enforced candidate normalization: every runtime scratch and diagnostic field
// is empty/zero (task contexts, sensor task contexts, arena, debug sets,
// profile, task counters, lock flag and user tree task).
bool spValidateNormalizedRuntimeScratch(const void*world);
#ifdef __cplusplus
}
#endif
