// SPDX-License-Identifier: MIT
#include "box3d_cone_twist_joint_impl_3d.hpp"

namespace {
Transform3D cone_frame(const Transform3D &p_frame) {
	// Godot twists about X; Box3D spherical joints twist about Z.
	return Transform3D(p_frame.basis * Basis(Vector3(0, 1, 0), Math::PI / 2), p_frame.origin);
}
} //namespace

Box3DConeTwistJointImpl3D::Box3DConeTwistJointImpl3D(Box3DBodyImpl3D *p_a, Box3DBodyImpl3D *p_b, const Transform3D &p_frame_a, const Transform3D &p_frame_b) :
		Box3DJointImpl3D(p_a, p_b, cone_frame(p_frame_a), cone_frame(p_frame_b)) {}

b3JointId Box3DConeTwistJointImpl3D::_create_joint_id(b3WorldId p_world, b3BodyId p_a, b3BodyId p_b, b3Transform p_frame_a, b3Transform p_frame_b) {
	b3SphericalJointDef def = b3DefaultSphericalJointDef();
	def.base.bodyIdA = p_a;
	def.base.bodyIdB = p_b;
	def.base.localFrameA = p_frame_a;
	def.base.localFrameB = p_frame_b;
	return b3CreateSphericalJoint(p_world, &def);
}

void Box3DConeTwistJointImpl3D::_apply_parameters() {
	if (!has_joint_id()) {
		return;
	}
	const real_t swing = parameters[PS3DE::CONE_TWIST_JOINT_SWING_SPAN];
	const real_t twist = parameters[PS3DE::CONE_TWIST_JOINT_TWIST_SPAN];
	b3SphericalJoint_EnableConeLimit(get_joint_id(), swing >= 0 && swing < Math::PI);
	b3SphericalJoint_SetConeLimit(get_joint_id(), float(CLAMP(swing, real_t(0), real_t(Math::PI))));
	// A span of pi or larger allows all orientations; negative spans disable the limit.
	b3SphericalJoint_EnableTwistLimit(get_joint_id(), twist >= 0 && twist < Math::PI * 0.99);
	const float twist_limit = float(CLAMP(twist, real_t(0), real_t(Math::PI * 0.99)));
	b3SphericalJoint_SetTwistLimits(get_joint_id(), -twist_limit, twist_limit);
	// Convert legacy error-reduction and softness to Soft Step tuning; numerical
	// equivalence with a different solver is not implied by this mapping.
	const real_t beta = CLAMP(parameters[PS3DE::CONE_TWIST_JOINT_BIAS] * parameters[PS3DE::CONE_TWIST_JOINT_SOFTNESS], real_t(0), real_t(0.99));
	const real_t damping = MAX(parameters[PS3DE::CONE_TWIST_JOINT_RELAXATION], real_t(0));
	const real_t hz = beta * MAX(damping, real_t(0.01)) * Engine::get_singleton()->get_physics_ticks_per_second() * 4 / (Math::PI * (1 - beta));
	b3Joint_SetConstraintTuning(get_joint_id(), float(hz), float(damping));
	b3Joint_WakeBodies(get_joint_id());
}

void Box3DConeTwistJointImpl3D::set_param(Param p_param, real_t p_value) {
	ERR_FAIL_INDEX(p_param, PS3DE::CONE_TWIST_MAX);
	ERR_FAIL_COND(!std::isfinite(p_value));
	parameters[p_param] = p_value;
	_apply_parameters();
}

real_t Box3DConeTwistJointImpl3D::get_param(Param p_param) const {
	ERR_FAIL_INDEX_V(p_param, PS3DE::CONE_TWIST_MAX, 0);
	return parameters[p_param];
}
