// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "../bodies/box2d_body_2d.h"

class Box2DJoint2D {
public:
	Box2DJoint2D() = default;
	explicit Box2DJoint2D(PS2DE::JointType p_type, Box2DBody2D *p_body_a, Box2DBody2D *p_body_b) :
			type(p_type), body_a(p_body_a), body_b(p_body_b) {}

	virtual ~Box2DJoint2D();

	PS2DE::JointType get_type() const { return type; }

	void copy_settings_from(Box2DJoint2D *p_joint);

	b2JointId get_joint_id() const { return joint_id; }
	void destroy_joint();
	void forget_body(Box2DBody2D *body) {
		if (body_a == body || body_b == body) {
			destroy_joint();
			body_a = nullptr;
			body_b = nullptr;
			space = nullptr;
		}
	}

	void set_rid(RID p_rid) { rid = p_rid; }
	RID get_rid() const { return rid; }

	void disable_collisions_between_bodies(bool p_disabled);
	bool is_disabled_collisions_between_bodies() const { return disabled_collisions_between_bodies; }

	void set_hertz(real_t p_value);
	real_t get_hertz() const;
	void set_damping_ratio(real_t p_value);
	real_t get_damping_ratio() const;

protected:
	/// Joint softness is tuned per joint, so the project-wide setting is applied at creation.
	void apply_constraint_tuning();

	Box2DBody2D *body_a = nullptr;
	Box2DBody2D *body_b = nullptr;
	PS2DE::JointType type = PS2DE::JOINT_TYPE_MAX;

	Box2DSpace2D *space = nullptr;
	b2JointId joint_id = b2_nullJointId;

	real_t hertz = -1;
	real_t damping_ratio = -1;

	bool disabled_collisions_between_bodies = true;
	RID rid;
};
