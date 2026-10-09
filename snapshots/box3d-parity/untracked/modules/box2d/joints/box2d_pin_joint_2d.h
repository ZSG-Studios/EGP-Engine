// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_joint_2d.h"

class Box2DPinJoint2D : public Box2DJoint2D {
public:
	Box2DPinJoint2D(const Vector2 &p_pos, Box2DBody2D *p_body_a, Box2DBody2D *p_body_b);

	void set_upper_limit(real_t p_limit);
	real_t get_upper_limit() const { return revolute_def.upperAngle; }

	void set_lower_limit(real_t p_limit);
	real_t get_lower_limit() const { return revolute_def.lowerAngle; }

	void set_motor_speed(real_t p_speed);
	real_t get_motor_speed() const { return revolute_def.motorSpeed; }

	void set_motor_enabled(bool p_enabled);
	bool get_motor_enabled() const { return revolute_def.enableMotor; }

	void set_limit_enabled(bool p_enabled);
	bool get_limit_enabled() const { return revolute_def.enableLimit; }

private:
	b2RevoluteJointDef revolute_def = b2DefaultRevoluteJointDef();
};
