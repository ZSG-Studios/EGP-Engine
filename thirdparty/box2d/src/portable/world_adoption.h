// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Adopts a validated candidate root into a live native world that was freshly
// created by b2CreateWorld in this process. The live world keeps its own
// adoption state: world index and generation, inUse, worker count, task
// enqueue/finish callbacks and user task context, scheduler and recording,
// plus its freshly created per-worker task contexts, stack arena and debug
// sets. Every simulation container of the candidate (cold arrays, solver sets,
// islands, constraint graph, shapes, chains, sensors, broadphase, all seven ID
// pools and the event arrays) is deep-copied into native allocations of exact
// capacity, and the placeholder containers created by b2CreateWorld are freed,
// so b2World_Step and b2DestroyWorld operate on native-owned storage. The root
// scalars and bindings are copied last. Requires the full Box2D library; it is
// compiled only into executables that link it.
bool spAdoptCandidateWorld(void*live_world,const void*candidate);
// The same adoption with `slack` extra capacity (0..4096) added to every
// non-empty native array, ID-pool free array and event array. Capacity is
// storage only; the fixture uses it to qualify capacity-remapped layouts.
bool spAdoptCandidateWorldWithSlack(void*live_world,const void*candidate,int slack);
#ifdef __cplusplus
}
#endif
