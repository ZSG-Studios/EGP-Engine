// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "box3d_pin_joint_impl_3d.hpp"

#include "precompiled.hpp"

#include <box3d/box3d.h>

Box3DPinJointImpl3D::Box3DPinJointImpl3D(
		Box3DBodyImpl3D *p_body_a,
		Box3DBodyImpl3D *p_body_b,
		const Transform3D &p_local_frame_a,
		const Transform3D &p_local_frame_b) :
		Box3DJointImpl3D(p_body_a, p_body_b, p_local_frame_a, p_local_frame_b) {
}

b3JointId Box3DPinJointImpl3D::_create_joint_id(b3WorldId p_world_id, b3BodyId p_body_a, b3BodyId p_body_b, b3Transform p_local_frame_a, b3Transform p_local_frame_b) {
	b3SphericalJointDef def = b3DefaultSphericalJointDef();
	def.base.bodyIdA = p_body_a;
	def.base.bodyIdB = p_body_b;
	def.base.localFrameA = p_local_frame_a;
	def.base.localFrameB = p_local_frame_b;
	def.enableSpring = true;
	def.hertz = (float)spring_hertz;
	def.dampingRatio = (float)damping;
	return b3CreateSphericalJoint(p_world_id, &def);
}

real_t Box3DPinJointImpl3D::get_param(Param p_param) const {
	switch (p_param) {
		case PS3DE::PIN_JOINT_DAMPING:
			return damping;
		case PS3DE::PIN_JOINT_SPRING_HERTZ:
			return spring_hertz;
		default:
			return 0.0;
	}
}

void Box3DPinJointImpl3D::set_param(Param p_param, real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value) || p_value < 0);
	switch (p_param) {
		case PS3DE::PIN_JOINT_DAMPING:
			damping = p_value;
			if (has_joint_id()) {
				b3SphericalJoint_SetSpringDampingRatio(get_joint_id(), (float)damping);
			}
			break;
		case PS3DE::PIN_JOINT_SPRING_HERTZ:
			spring_hertz = p_value;
			if (has_joint_id()) {
				b3SphericalJoint_SetSpringHertz(get_joint_id(), (float)spring_hertz);
			}
			break;
		default:
			break;
	}
}
