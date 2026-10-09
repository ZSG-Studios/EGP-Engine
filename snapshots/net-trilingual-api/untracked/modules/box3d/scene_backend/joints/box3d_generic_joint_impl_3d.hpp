// SPDX-License-Identifier: MIT
#pragma once
#include "box3d_joint_impl_3d.hpp"
class Box3DGenericJointImpl3D final : public Box3DJointImpl3D {
public:
	Box3DGenericJointImpl3D(Box3DBodyImpl3D *, Box3DBodyImpl3D *, const Transform3D &, const Transform3D &);
	PS3DE::JointType get_type() const override { return PS3DE::JOINT_TYPE_6DOF; }
	void set_param(Vector3::Axis, PS3DE::G6DOFJointAxisParam, real_t);
	real_t get_param(Vector3::Axis, PS3DE::G6DOFJointAxisParam) const;
	void set_flag(Vector3::Axis, PS3DE::G6DOFJointAxisFlag, bool);
	bool get_flag(Vector3::Axis, PS3DE::G6DOFJointAxisFlag) const;
	void set_target_rotation(const Quaternion &);
	Quaternion get_target_rotation() const { return target_rotation; }

protected:
	b3JointId _create_joint_id(b3WorldId, b3BodyId, b3BodyId, b3Transform, b3Transform) override;
	void _joint_created() override;

private:
	real_t parameters[3][PS3DE::G6DOF_JOINT_MAX] = {};
	bool flags[3][PS3DE::G6DOF_JOINT_FLAG_MAX] = {};
	Quaternion target_rotation;
};
