// SPDX-License-Identifier: MIT
#pragma once

#include <mutex>

namespace egp::box3d {
// All native adapters share Box3D's process-global world slots and unit scale.
// Recursive entry permits owner callbacks and helpers during a guarded step.
inline std::recursive_mutex &get_simulation_mutex() {
	static std::recursive_mutex mutex;
	return mutex;
}
} // namespace egp::box3d
