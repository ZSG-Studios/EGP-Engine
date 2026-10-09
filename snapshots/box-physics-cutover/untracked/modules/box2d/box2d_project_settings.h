// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "modules/box2d/precompiled.h"

using namespace PhysicsServer2DEnums;

enum Box2DMixingRule : int32_t {
	MIXING_RULE_GODOT,
	MIXING_RULE_BOX2D,
	MIXING_RULE_MAX,
};

class Box2DProjectSettings {
public:
	static void register_settings();

	static int32_t get_max_threads();

	static int get_pixels_per_meter();

	static int get_substeps();

	static real_t get_contact_hertz();

	static real_t get_contact_damping_ratio();

	static real_t get_joint_hertz();

	static real_t get_joint_damping_ratio();

	static Box2DMixingRule get_friction_mixing_rule();

	static Box2DMixingRule get_restitution_mixing_rule();

	static bool get_presolve_enabled();
};