// SPDX-License-Identifier: MIT
#pragma once
#include "box3d_joint_impl_3d.hpp"
class Box3DConfiguredJoint3D final : public Box3DJointImpl3D {
public:
	Box3DConfiguredJoint3D(PS3DE::JointType p_type, Box3DBodyImpl3D *p_a, Box3DBodyImpl3D *p_b, const Transform3D &p_frame_a, const Transform3D &p_frame_b);
	PS3DE::JointType get_type() const override { return type; }
	bool set_configuration(const Dictionary &p_configuration);
	Dictionary get_configuration() const { return configuration.duplicate(); }
	static Dictionary default_configuration(PS3DE::JointType p_type);
	static bool validate_configuration(PS3DE::JointType p_type, const Dictionary &p_configuration);

protected:
	b3JointId _create_joint_id(b3WorldId p_world, b3BodyId p_a, b3BodyId p_b, b3Transform p_frame_a, b3Transform p_frame_b) override;

private:
	void _apply_configuration(const Dictionary &p_changed);
	Dictionary configuration;
	PS3DE::JointType type;
};
