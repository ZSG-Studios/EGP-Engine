// SPDX-License-Identifier: MIT
#pragma once
#include <mutex>
namespace egp::box2d {
inline std::recursive_mutex &get_simulation_mutex() {
	static std::recursive_mutex mutex;
	return mutex;
}
} //namespace egp::box2d
