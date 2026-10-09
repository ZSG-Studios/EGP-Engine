// SPDX-License-Identifier: MIT
#include "box3d_task_system.h"

#include "core/error/error_macros.h"
#include "core/object/worker_thread_pool.h"

#include <cfenv>

namespace egp::box3d {
namespace {
struct PoolTask {
	WorkerThreadPool::TaskID id = WorkerThreadPool::INVALID_TASK_ID;
	b3TaskCallback *task = nullptr;
	void *context = nullptr;
};

void run_pool_task(void *p_data) {
	CRASH_COND_MSG(std::fegetround() != FE_TONEAREST, "Box3D workers require round-to-nearest floating point.");
	PoolTask *data = static_cast<PoolTask *>(p_data);
	data->task(data->context);
}
} // namespace

void *enqueue_pool_task(b3TaskCallback *p_task, void *p_context, void *p_user, const char *p_name) {
	PoolTask *data = new PoolTask{ WorkerThreadPool::INVALID_TASK_ID, p_task, p_context };
	static const String task_name("Box3D Task");
	data->id = WorkerThreadPool::get_singleton()->add_native_task(run_pool_task, data, true, task_name);
	return data;
}

void finish_pool_task(void *p_task, void *p_user) {
	if (p_task) {
		PoolTask *data = static_cast<PoolTask *>(p_task);
		WorkerThreadPool::get_singleton()->wait_for_task_completion(data->id);
		delete data;
	}
}

} // namespace egp::box3d
