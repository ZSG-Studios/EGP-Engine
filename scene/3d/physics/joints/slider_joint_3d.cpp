/**************************************************************************/
/*  slider_joint_3d.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "slider_joint_3d.h"

#include "core/object/class_db.h"

void SliderJoint3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_param", "param", "value"), &SliderJoint3D::set_param);
	ClassDB::bind_method(D_METHOD("set_limit_enabled", "enabled"), &SliderJoint3D::set_limit_enabled);
	ClassDB::bind_method(D_METHOD("is_limit_enabled"), &SliderJoint3D::is_limit_enabled);
	ClassDB::bind_method(D_METHOD("set_motor_enabled", "enabled"), &SliderJoint3D::set_motor_enabled);
	ClassDB::bind_method(D_METHOD("is_motor_enabled"), &SliderJoint3D::is_motor_enabled);
	ClassDB::bind_method(D_METHOD("set_spring_enabled", "enabled"), &SliderJoint3D::set_spring_enabled);
	ClassDB::bind_method(D_METHOD("is_spring_enabled"), &SliderJoint3D::is_spring_enabled);
	ClassDB::bind_method(D_METHOD("get_param", "param"), &SliderJoint3D::get_param);

	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "linear_limit/upper_distance", PROPERTY_HINT_RANGE, "-1024,1024,0.01,suffix:m"), "set_param", "get_param", PARAM_LINEAR_LIMIT_UPPER);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "linear_limit/lower_distance", PROPERTY_HINT_RANGE, "-1024,1024,0.01,suffix:m"), "set_param", "get_param", PARAM_LINEAR_LIMIT_LOWER);

	BIND_ENUM_CONSTANT(PARAM_LINEAR_LIMIT_UPPER);
	BIND_ENUM_CONSTANT(PARAM_LINEAR_LIMIT_LOWER);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "linear_limit/enabled"), "set_limit_enabled", "is_limit_enabled");
	BIND_ENUM_CONSTANT(PARAM_LIMIT_ENABLED);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "motor/enabled"), "set_motor_enabled", "is_motor_enabled");
	BIND_ENUM_CONSTANT(PARAM_MOTOR_ENABLED);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "motor/target_velocity", PROPERTY_HINT_RANGE, "-1000,1000,0.01,or_greater,or_less"), "set_param", "get_param", PARAM_MOTOR_TARGET_VELOCITY);
	BIND_ENUM_CONSTANT(PARAM_MOTOR_TARGET_VELOCITY);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "motor/max_force", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater"), "set_param", "get_param", PARAM_MOTOR_MAX_FORCE);
	BIND_ENUM_CONSTANT(PARAM_MOTOR_MAX_FORCE);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spring/enabled"), "set_spring_enabled", "is_spring_enabled");
	BIND_ENUM_CONSTANT(PARAM_SPRING_ENABLED);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "spring/frequency", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater"), "set_param", "get_param", PARAM_SPRING_HERTZ);
	BIND_ENUM_CONSTANT(PARAM_SPRING_HERTZ);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "spring/damping_ratio", PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater"), "set_param", "get_param", PARAM_SPRING_DAMPING_RATIO);
	BIND_ENUM_CONSTANT(PARAM_SPRING_DAMPING_RATIO);
	ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, "spring/target_translation", PROPERTY_HINT_RANGE, "-1000,1000,0.01,or_greater,or_less"), "set_param", "get_param", PARAM_SPRING_TARGET_TRANSLATION);
	BIND_ENUM_CONSTANT(PARAM_SPRING_TARGET_TRANSLATION);
	BIND_ENUM_CONSTANT(PARAM_MAX);
}

void SliderJoint3D::set_param(Param p_param, real_t p_value) {
	ERR_FAIL_INDEX(p_param, PARAM_MAX);
	ERR_FAIL_COND_MSG(!Math::is_finite(p_value), "Joint parameters must be finite.");
	ERR_FAIL_COND(p_param == PARAM_LIMIT_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PARAM_MOTOR_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PARAM_MOTOR_MAX_FORCE && p_value < 0);
	ERR_FAIL_COND(p_param == PARAM_SPRING_ENABLED && p_value != 0 && p_value != 1);
	ERR_FAIL_COND(p_param == PARAM_SPRING_HERTZ && p_value < 0);
	ERR_FAIL_COND(p_param == PARAM_SPRING_DAMPING_RATIO && p_value < 0);

	params[p_param] = p_value;
	if (is_configured()) {
		PhysicsServer3D::get_singleton()->slider_joint_set_param(get_rid(), PS3DE::SliderJointParam(p_param), p_value);
	}
	update_gizmos();
}

real_t SliderJoint3D::get_param(Param p_param) const {
	ERR_FAIL_INDEX_V(p_param, PARAM_MAX, 0);
	return params[p_param];
}

void SliderJoint3D::_configure_joint(RID p_joint, PhysicsBody3D *body_a, PhysicsBody3D *body_b) {
	Transform3D gt = get_global_transform();
	Transform3D ainv = body_a->get_global_transform().affine_inverse();

	Transform3D local_a = ainv * gt;
	local_a.orthonormalize();
	Transform3D local_b = gt;

	if (body_b) {
		Transform3D binv = body_b->get_global_transform().affine_inverse();
		local_b = binv * gt;
	}

	local_b.orthonormalize();

	PhysicsServer3D::get_singleton()->joint_make_slider(p_joint, body_a->get_rid(), local_a, body_b ? body_b->get_rid() : RID(), local_b);
	for (int i = 0; i < PARAM_MAX; i++) {
		PhysicsServer3D::get_singleton()->slider_joint_set_param(p_joint, PS3DE::SliderJointParam(i), params[i]);
	}
}

SliderJoint3D::SliderJoint3D() {
	params[PARAM_LIMIT_ENABLED] = 1;
	params[PARAM_MOTOR_ENABLED] = 0;
	params[PARAM_MOTOR_TARGET_VELOCITY] = 0;
	params[PARAM_MOTOR_MAX_FORCE] = 0;
	params[PARAM_SPRING_ENABLED] = 0;
	params[PARAM_SPRING_HERTZ] = 0;
	params[PARAM_SPRING_DAMPING_RATIO] = 1;
	params[PARAM_SPRING_TARGET_TRANSLATION] = 0;

	params[PARAM_LINEAR_LIMIT_UPPER] = 1.0;
	params[PARAM_LINEAR_LIMIT_LOWER] = -1.0;
}
