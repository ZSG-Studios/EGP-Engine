// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "box3d_globals.hpp"

#include "precompiled.hpp"

#include <box3d/constants.h>

#include <algorithm>

namespace {
const char *WORKER_COUNT_SETTING = "physics/box3d/worker_count";
} // namespace

void box3d_initialize() {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings->has_setting(WORKER_COUNT_SETTING)) {
		settings->set_setting(WORKER_COUNT_SETTING, 1);
	}
	settings->set_initial_value(WORKER_COUNT_SETTING, 1);
	// Spaces capture the count at creation, so a change only applies after a restart.
	settings->set_restart_if_changed(WORKER_COUNT_SETTING, true);
	Dictionary info;
	info["name"] = WORKER_COUNT_SETTING;
	info["type"] = Variant::INT;
	info["hint"] = PROPERTY_HINT_RANGE;
	info["hint_string"] = "0,32,1";
	settings->set_custom_property_info(PropertyInfo(Variant::INT, WORKER_COUNT_SETTING, PROPERTY_HINT_RANGE, "1,32,1"));
}

void box3d_deinitialize() {
	// No global Box3D shutdown is required.
}

int box3d_worker_count() {
	static const int count = []() {
		const int setting = (int)ProjectSettings::get_singleton()->get_setting_with_override(
				WORKER_COUNT_SETTING);
		return std::clamp(setting, 1, B3_MAX_WORKERS);
	}();
	return count;
}
