// SPDX-License-Identifier: MIT
#pragma once

#include "box3d/box3d.h"

namespace egp::box3d {

// Runs Box3D solver tasks on Godot's WorkerThreadPool instead of Box3D's internal
// scheduler threads, so physics shares the engine's worker budget. Box3D results do
// not depend on how its tasks are scheduled.
void *enqueue_pool_task(b3TaskCallback *p_task, void *p_context, void *p_user, const char *p_name);
void finish_pool_task(void *p_task, void *p_user);

} // namespace egp::box3d
