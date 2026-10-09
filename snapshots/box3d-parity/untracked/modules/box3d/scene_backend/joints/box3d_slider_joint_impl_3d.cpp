// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "box3d_slider_joint_impl_3d.hpp"

#include "precompiled.hpp"

#include <box3d/box3d.h>

Box3DSliderJointImpl3D::Box3DSliderJointImpl3D(
		Box3DBodyImpl3D *p_body_a,
		Box3DBodyImpl3D *p_body_b,
		const Transform3D &p_local_frame_a,
		const Transform3D &p_local_frame_b) :
		Box3DJointImpl3D(p_body_a, p_body_b, p_local_frame_a, p_local_frame_b) {
}

b3JointId Box3DSliderJointImpl3D::_create_joint_id(b3WorldId p_world_id, b3BodyId p_body_a, b3BodyId p_body_b, b3Transform p_local_frame_a, b3Transform p_local_frame_b) {
	b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
	def.base.bodyIdA = p_body_a;
	def.base.bodyIdB = p_body_b;
	def.base.localFrameA = p_local_frame_a;
	def.base.localFrameB = p_local_frame_b;
	def.enableMotor = motor_enabled;
	def.motorSpeed = (float)motor_target_velocity;
	def.maxMotorForce = (float)motor_max_force;
	def.enableSpring = spring_enabled;
	def.hertz = (float)spring_hertz;
	def.dampingRatio = (float)spring_damping_ratio;
	def.targetTranslation = (float)spring_target_translation;

	def.enableLimit = limit_enabled;
	def.lowerTranslation = (float)limit_lower;
	def.upperTranslation = (float)limit_upper;
	return b3CreatePrismaticJoint(p_world_id, &def);
}

real_t Box3DSliderJointImpl3D::get_param(Param p_param) const {
	switch (p_param) {
		case PS3DE::SLIDER_JOINT_LINEAR_LIMIT_UPPER:
			return limit_upper;
		case PS3DE::SLIDER_JOINT_LINEAR_LIMIT_LOWER:
			return limit_lower;
		case PS3DE::SLIDER_JOINT_LIMIT_ENABLED:
			return limit_enabled;
		case PS3DE::SLIDER_JOINT_MOTOR_ENABLED:
			return motor_enabled;
		case PS3DE::SLIDER_JOINT_MOTOR_TARGET_VELOCITY:
			return motor_target_velocity;
		case PS3DE::SLIDER_JOINT_MOTOR_MAX_FORCE:
			return motor_max_force;
		case PS3DE::SLIDER_JOINT_SPRING_ENABLED:
			return spring_enabled;
		case PS3DE::SLIDER_JOINT_SPRING_HERTZ:
			return spring_hertz;
		case PS3DE::SLIDER_JOINT_SPRING_DAMPING_RATIO:
			return spring_damping_ratio;
		case PS3DE::SLIDER_JOINT_SPRING_TARGET_TRANSLATION:
			return spring_target_translation;
		default:
			return 0.0;
	}
}

void Box3DSliderJointImpl3D::set_param(Param p_param, real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_LIMIT_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_MOTOR_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_MOTOR_MAX_FORCE && p_value < 0);
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_SPRING_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_SPRING_HERTZ && p_value < 0);
	ERR_FAIL_COND(p_param == PS3DE::SLIDER_JOINT_SPRING_DAMPING_RATIO && p_value < 0);
	switch (p_param) {
		case PS3DE::SLIDER_JOINT_LINEAR_LIMIT_UPPER:
			limit_upper = p_value;
			_apply_limit();
			break;
		case PS3DE::SLIDER_JOINT_LINEAR_LIMIT_LOWER:
			limit_lower = p_value;
			_apply_limit();
			break;
		case PS3DE::SLIDER_JOINT_LIMIT_ENABLED:
			limit_enabled = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_EnableLimit(get_joint_id(), limit_enabled);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_MOTOR_ENABLED:
			motor_enabled = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_EnableMotor(get_joint_id(), motor_enabled);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_MOTOR_TARGET_VELOCITY:
			motor_target_velocity = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_SetMotorSpeed(get_joint_id(), (float)motor_target_velocity);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_MOTOR_MAX_FORCE:
			motor_max_force = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_SetMaxMotorForce(get_joint_id(), (float)motor_max_force);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_SPRING_ENABLED:
			spring_enabled = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_EnableSpring(get_joint_id(), spring_enabled);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_SPRING_HERTZ:
			spring_hertz = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_SetSpringHertz(get_joint_id(), (float)spring_hertz);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_SPRING_DAMPING_RATIO:
			spring_damping_ratio = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_SetSpringDampingRatio(get_joint_id(), (float)spring_damping_ratio);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		case PS3DE::SLIDER_JOINT_SPRING_TARGET_TRANSLATION:
			spring_target_translation = p_value;
			if (has_joint_id()) {
				b3PrismaticJoint_SetTargetTranslation(get_joint_id(), (float)spring_target_translation);
				b3Joint_WakeBodies(get_joint_id());
			}
			break;
		default:
			ERR_PRINT("Invalid slider joint parameter.");
			break;
	}
}

void Box3DSliderJointImpl3D::_apply_limit() {
	if (has_joint_id()) {
		b3PrismaticJoint_SetLimits(get_joint_id(), (float)limit_lower, (float)limit_upper);
	}
}
