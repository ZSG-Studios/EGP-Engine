// SPDX-License-Identifier: MIT
#pragma once
#include "box3d_joint_impl_3d.hpp"

class Box3DConeTwistJointImpl3D final : public Box3DJointImpl3D {
public:
	using Param = PS3DE::ConeTwistJointParam;
	Box3DConeTwistJointImpl3D(Box3DBodyImpl3D *p_body_a, Box3DBodyImpl3D *p_body_b, const Transform3D &p_frame_a, const Transform3D &p_frame_b);
	PS3DE::JointType get_type() const override { return PS3DE::JOINT_TYPE_CONE_TWIST; }
	void set_param(Param p_param, real_t p_value);
	real_t get_param(Param p_param) const;

protected:
	b3JointId _create_joint_id(b3WorldId p_world, b3BodyId p_a, b3BodyId p_b, b3Transform p_frame_a, b3Transform p_frame_b) override;
	void _joint_created() override { _apply_parameters(); }

private:
	void _apply_parameters();
	real_t parameters[PS3DE::CONE_TWIST_MAX] = { Math::PI / 4, Math::PI, 0.3, 0.8, 1.0 };
};
