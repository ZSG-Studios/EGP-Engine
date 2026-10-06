// SPDX-License-Identifier: MIT
#pragma once
#include "box2d_joint_2d.h"
class Box2DConfiguredJoint2D final : public Box2DJoint2D {
public:
	Box2DConfiguredJoint2D(PS2DE::JointType p_type, Box2DBody2D *p_a, Box2DBody2D *p_b, const Transform2D &p_frame_a, const Transform2D &p_frame_b);
	bool set_configuration(const Dictionary &p_configuration);
	Dictionary get_configuration() const { return configuration.duplicate(); }
	static Dictionary default_configuration(PS2DE::JointType p_type);
	static bool validate_configuration(PS2DE::JointType p_type, const Dictionary &p_configuration);
	void rebuild();

private:
	void _apply_configuration(const Dictionary &p_changed);
	Dictionary configuration;
	Transform2D frame_a;
	Transform2D frame_b;
};
