// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_joint_2d.h"

class Box2DDampedSpringJoint2D : public Box2DJoint2D {
public:
	Box2DDampedSpringJoint2D(const Vector2 &p_anchor_a, const Vector2 &p_anchor_b, Box2DBody2D *p_body_a, Box2DBody2D *p_body_b);

	void set_rest_length(real_t p_length);
	real_t get_rest_length() const { return distance_def.length; }

	void set_damping_ratio(real_t p_damping);
	real_t get_damping_ratio() const { return distance_def.dampingRatio; }

	void set_stiffness(real_t p_stiffness);
	real_t get_stiffness() const { return stiffness; }

	void update_stiffness();

private:
	b2DistanceJointDef distance_def = b2DefaultDistanceJointDef();
	real_t stiffness = 20.0;
};