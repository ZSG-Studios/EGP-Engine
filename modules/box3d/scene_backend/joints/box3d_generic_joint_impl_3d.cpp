// SPDX-License-Identifier: MIT
#include "box3d_generic_joint_impl_3d.hpp"

#include "../misc/type_conversions.hpp"

#include <cfloat>
static_assert(PS3DE::G6DOF_JOINT_MAX == B3_GENERIC_PARAM_COUNT);
static_assert(PS3DE::G6DOF_JOINT_FLAG_MAX == B3_GENERIC_FLAG_COUNT);
Box3DGenericJointImpl3D::Box3DGenericJointImpl3D(Box3DBodyImpl3D *a, Box3DBodyImpl3D *b, const Transform3D &fa, const Transform3D &fb) : Box3DJointImpl3D(a, b, fa, fb) {
	for (int axis = 0; axis < 3; ++axis) {
		auto *p = parameters[axis];
		p[PS3DE::G6DOF_JOINT_LINEAR_LIMIT_SOFTNESS] = 0.7;
		p[PS3DE::G6DOF_JOINT_LINEAR_RESTITUTION] = 0.5;
		p[PS3DE::G6DOF_JOINT_LINEAR_DAMPING] = 1;
		p[PS3DE::G6DOF_JOINT_LINEAR_SPRING_STIFFNESS] = 0.01;
		p[PS3DE::G6DOF_JOINT_LINEAR_SPRING_DAMPING] = 0.01;
		p[PS3DE::G6DOF_JOINT_ANGULAR_LIMIT_SOFTNESS] = 0.5;
		p[PS3DE::G6DOF_JOINT_ANGULAR_DAMPING] = 1;
		p[PS3DE::G6DOF_JOINT_ANGULAR_ERP] = 0.5;
		p[PS3DE::G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT] = 300;
		p[PS3DE::G6DOF_JOINT_LINEAR_DRIVE_FORCE_LIMIT] = FLT_MAX;
		p[PS3DE::G6DOF_JOINT_ANGULAR_DRIVE_TORQUE_LIMIT] = FLT_MAX;
		flags[axis][PS3DE::G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT] = true;
		flags[axis][PS3DE::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT] = true;
	}
}
b3JointId Box3DGenericJointImpl3D::_create_joint_id(b3WorldId world, b3BodyId a, b3BodyId b, b3Transform fa, b3Transform fb) {
	auto def = b3DefaultWeldJointDef();
	def.base.bodyIdA = a;
	def.base.bodyIdB = b;
	def.base.localFrameA = fa;
	def.base.localFrameB = fb;
	return b3CreateGenericJoint(world, &def);
}
void Box3DGenericJointImpl3D::_joint_created() {
	for (int axis = 0; axis < 3; ++axis) {
		for (int p = 0; p < PS3DE::G6DOF_JOINT_MAX; ++p) {
			b3GenericJoint_SetParam(get_joint_id(), axis, p, float(CLAMP(parameters[axis][p], real_t(-FLT_MAX), real_t(FLT_MAX))));
		}
		for (int f = 0; f < PS3DE::G6DOF_JOINT_FLAG_MAX; ++f) {
			b3GenericJoint_SetFlag(get_joint_id(), axis, f, flags[axis][f]);
		}
	}
	b3GenericJoint_SetTargetRotation(get_joint_id(), godot_to_b3(target_rotation));
}
void Box3DGenericJointImpl3D::set_param(Vector3::Axis axis, PS3DE::G6DOFJointAxisParam p, real_t value) {
	ERR_FAIL_INDEX(axis, 3);
	ERR_FAIL_INDEX(p, PS3DE::G6DOF_JOINT_MAX);
	ERR_FAIL_COND(std::isnan(value));
	parameters[axis][p] = value;
	if (has_joint_id()) {
		b3GenericJoint_SetParam(get_joint_id(), axis, p, float(CLAMP(value, real_t(-FLT_MAX), real_t(FLT_MAX))));
	}
}
real_t Box3DGenericJointImpl3D::get_param(Vector3::Axis axis, PS3DE::G6DOFJointAxisParam p) const {
	ERR_FAIL_INDEX_V(axis, 3, 0);
	ERR_FAIL_INDEX_V(p, PS3DE::G6DOF_JOINT_MAX, 0);
	return parameters[axis][p];
}
void Box3DGenericJointImpl3D::set_flag(Vector3::Axis axis, PS3DE::G6DOFJointAxisFlag flag, bool value) {
	ERR_FAIL_INDEX(axis, 3);
	ERR_FAIL_INDEX(flag, PS3DE::G6DOF_JOINT_FLAG_MAX);
	flags[axis][flag] = value;
	if (has_joint_id()) {
		b3GenericJoint_SetFlag(get_joint_id(), axis, flag, value);
	}
}
bool Box3DGenericJointImpl3D::get_flag(Vector3::Axis axis, PS3DE::G6DOFJointAxisFlag flag) const {
	ERR_FAIL_INDEX_V(axis, 3, false);
	ERR_FAIL_INDEX_V(flag, PS3DE::G6DOF_JOINT_FLAG_MAX, false);
	return flags[axis][flag];
}
void Box3DGenericJointImpl3D::set_target_rotation(const Quaternion &rotation) {
	ERR_FAIL_COND(!rotation.is_finite() || !rotation.is_normalized());
	target_rotation = rotation;
	if (has_joint_id()) {
		b3GenericJoint_SetTargetRotation(get_joint_id(), godot_to_b3(rotation));
	}
}
