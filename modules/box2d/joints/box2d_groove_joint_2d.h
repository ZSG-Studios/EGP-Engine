// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_joint_2d.h"

class Box2DGrooveJoint2D : public Box2DJoint2D {
public:
	Box2DGrooveJoint2D(const Vector2 &p_a_groove1, const Vector2 &p_a_groove2, const Vector2 &p_b_anchor, Box2DBody2D *p_body_a, Box2DBody2D *p_body_b);

private:
	b2WheelJointDef wheel_def = b2DefaultWheelJointDef();
};