// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_joint_2d.h"

#include "../box2d_project_settings.h"

Box2DJoint2D::~Box2DJoint2D() {
	destroy_joint();
}

void Box2DJoint2D::apply_constraint_tuning() {
	if (hertz < 0) {
		hertz = Box2DProjectSettings::get_joint_hertz();
	}
	if (damping_ratio < 0) {
		damping_ratio = Box2DProjectSettings::get_joint_damping_ratio();
	}
	if (!b2Joint_IsValid(joint_id)) {
		return;
	}

	b2Joint_SetConstraintTuning(
			joint_id,
			hertz,
			damping_ratio);
}

void Box2DJoint2D::copy_settings_from(Box2DJoint2D *p_joint) {
	set_rid(p_joint->get_rid());
	hertz = p_joint->hertz;
	damping_ratio = p_joint->damping_ratio;
	apply_constraint_tuning();
	disable_collisions_between_bodies(p_joint->is_disabled_collisions_between_bodies());
}

void Box2DJoint2D::destroy_joint() {
	if (b2Joint_IsValid(joint_id)) {
		b2DestroyJoint(joint_id, true);
	}
	joint_id = b2_nullJointId;
}

void Box2DJoint2D::disable_collisions_between_bodies(bool p_disabled) {
	disabled_collisions_between_bodies = p_disabled;

	if (b2Joint_IsValid(joint_id)) {
		b2Joint_SetCollideConnected(joint_id, !disabled_collisions_between_bodies);
	}
}

void Box2DJoint2D::set_hertz(real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0);
	hertz = p_value;
	apply_constraint_tuning();
	if (b2Joint_IsValid(joint_id)) {
		b2Joint_WakeBodies(joint_id);
	}
}

real_t Box2DJoint2D::get_hertz() const {
	return hertz < 0 ? Box2DProjectSettings::get_joint_hertz() : hertz;
}

real_t Box2DJoint2D::get_damping_ratio() const {
	return damping_ratio < 0 ? Box2DProjectSettings::get_joint_damping_ratio() : damping_ratio;
}
void Box2DJoint2D::set_damping_ratio(real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0);
	damping_ratio = p_value;
	apply_constraint_tuning();
	if (b2Joint_IsValid(joint_id)) {
		b2Joint_WakeBodies(joint_id);
	}
}
