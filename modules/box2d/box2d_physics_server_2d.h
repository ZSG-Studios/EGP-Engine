/**************************************************************************/
/*  box2d_physics_server_2d.h                                             */
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

// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "bodies/box2d_area_2d.h"
#include "bodies/box2d_body_2d.h"
#include "box2d_globals.h"
#include "box2d_project_settings.h"
#include "joints/box2d_joint_2d.h"
#include "spaces/box2d_space_2d.h"

#include "modules/box2d/precompiled.h"

using namespace PhysicsServer2DEnums;

class Box2DPhysicsServer2D : public PhysicsServer2D {
	friend class Box2DLocalReplay;
	GDCLASS(Box2DPhysicsServer2D, PhysicsServer2D);

public:
	bool shape_collide(RID, const Transform2D &, const Vector2 &, RID, const Transform2D &, const Vector2 &, Vector2 *, int, int &) override;
	bool body_collide_shape(RID, int, RID, const Transform2D &, const Vector2 &, Vector2 *, int, int &) override;
	void body_get_collision_exceptions(RID, List<RID> *) override;
	bool body_test_motion(RID, const PS2DT::MotionParameters &, PS2DT::MotionResult *) override;
	static Box2DPhysicsServer2D *get_singleton();

	RID world_boundary_shape_create() override;
	RID separation_ray_shape_create() override;
	RID segment_shape_create() override;
	RID circle_shape_create() override;
	RID rectangle_shape_create() override;
	RID capsule_shape_create() override;
	RID convex_polygon_shape_create() override;
	RID concave_polygon_shape_create() override;
	void shape_set_data(RID p_shape, const Variant &p_data) override;
	PS2DE::ShapeType shape_get_type(RID p_shape) const override;
	Variant shape_get_data(RID p_shape) const override;
	bool _shape_collide_compat(RID p_shape_A, const Transform2D &p_xform_A, const Vector2 &p_motion_A, RID p_shape_B, const Transform2D &p_xform_B, const Vector2 &p_motion_B, void *p_results, int32_t p_result_max, int32_t *p_result_count);

	void space_apply_explosion(RID p_space, const Vector2 &p_position, real_t p_radius, real_t p_falloff, real_t p_impulse_density, uint32_t p_collision_mask) override;
	Array space_get_contact_hit_events(RID p_space) const override;
	Array space_get_joint_events(RID p_space) const override;
	Vector2 joint_get_constraint_force(RID p_joint) const override;
	real_t joint_get_constraint_torque(RID p_joint) const override;

	RID space_create() override;
	void space_set_active(RID p_space, bool p_active) override;
	bool space_is_active(RID p_space) const override;
	void space_set_param(RID p_space, PS2DE::SpaceParameter p_param, real_t p_value) override;
	real_t space_get_param(RID p_space, PS2DE::SpaceParameter p_param) const override;
	PhysicsDirectSpaceState2D *space_get_direct_state(RID p_space) override;
	void space_set_debug_contacts(RID p_space, int p_max_contacts) override;
	Vector<Vector2> space_get_contacts(RID p_space) const override;
	int space_get_contact_count(RID p_space) const override;

	Array space_get_body_move_events(RID p_space) const;

	RID area_create() override;
	void area_set_space(RID p_area, RID p_space) override;
	RID area_get_space(RID p_area) const override;
	void area_add_shape(RID p_area, RID p_shape, const Transform2D &p_transform, bool p_disabled) override;
	void area_set_shape(RID p_area, int p_shape_idx, RID p_shape) override;
	void area_set_shape_transform(RID p_area, int p_shape_idx, const Transform2D &p_transform) override;
	void area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) override;
	int area_get_shape_count(RID p_area) const override;
	RID area_get_shape(RID p_area, int p_shape_idx) const override;
	Transform2D area_get_shape_transform(RID p_area, int p_shape_idx) const override;
	void area_remove_shape(RID p_area, int p_shape_idx) override;
	void area_clear_shapes(RID p_area) override;
	void area_attach_object_instance_id(RID p_area, ObjectID p_id) override;
	ObjectID area_get_object_instance_id(RID p_area) const override;
	void area_attach_canvas_instance_id(RID p_area, ObjectID p_id) override;
	ObjectID area_get_canvas_instance_id(RID p_area) const override;
	void area_set_param(RID p_area, PS2DE::AreaParameter p_param, const Variant &p_value) override;
	void area_set_transform(RID p_area, const Transform2D &p_transform) override;
	Variant area_get_param(RID p_area, PS2DE::AreaParameter p_param) const override;
	Transform2D area_get_transform(RID p_area) const override;
	void area_set_collision_layer(RID p_area, uint32_t p_layer) override;
	uint32_t area_get_collision_layer(RID p_area) const override;
	void area_set_collision_mask(RID p_area, uint32_t p_mask) override;
	uint32_t area_get_collision_mask(RID p_area) const override;
	void area_set_monitorable(RID p_area, bool p_monitorable) override;
	void area_set_pickable(RID p_area, bool p_pickable) override;
	void area_set_monitor_callback(RID p_area, const Callable &p_callback) override;
	void area_set_area_monitor_callback(RID p_area, const Callable &p_callback) override;

	RID body_create() override;
	void body_set_space(RID p_body, RID p_space) override;
	RID body_get_space(RID p_body) const override;
	void body_set_mode(RID p_body, PS2DE::BodyMode p_mode) override;
	PS2DE::BodyMode body_get_mode(RID p_body) const override;

	void body_set_user_data(RID p_body, const Variant &p_variant) const;
	Variant body_get_user_data(RID p_body) const;

	void body_add_shape(RID p_body, RID p_shape, const Transform2D &p_transform, bool p_disabled) override;
	void body_set_shape(RID p_body, int p_shape_idx, RID p_shape) override;
	void body_set_shape_transform(RID p_body, int p_shape_idx, const Transform2D &p_transform) override;
	int body_get_shape_count(RID p_body) const override;
	RID body_get_shape(RID p_body, int p_shape_idx) const override;
	Transform2D body_get_shape_transform(RID p_body, int p_shape_idx) const override;
	void body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) override;
	void body_set_shape_as_one_way_collision(RID p_body, int32_t p_shape_idx, bool p_enable, real_t p_margin, const Vector2 &p_direction) override;
	void body_remove_shape(RID p_body, int p_shape_idx) override;
	void body_clear_shapes(RID p_body) override;
	void body_attach_object_instance_id(RID p_body, ObjectID p_id) override;
	ObjectID body_get_object_instance_id(RID p_body) const override;
	void body_attach_canvas_instance_id(RID p_body, ObjectID p_id) override;
	ObjectID body_get_canvas_instance_id(RID p_body) const override;

	void body_set_continuous_collision_detection_mode(RID p_body, PS2DE::CCDMode p_mode) override;
	PS2DE::CCDMode body_get_continuous_collision_detection_mode(RID p_body) const override;
	void body_set_collision_layer(RID p_body, uint32_t p_layer) override;
	uint32_t body_get_collision_layer(RID p_body) const override;
	void body_set_collision_mask(RID p_body, uint32_t p_mask) override;
	uint32_t body_get_collision_mask(RID p_body) const override;
	void body_set_collision_priority(RID p_body, real_t p_priority) override;
	real_t body_get_collision_priority(RID p_body) const override;
	void body_set_param(RID p_body, PS2DE::BodyParameter p_param, const Variant &p_value) override;
	Variant body_get_param(RID p_body, PS2DE::BodyParameter p_param) const override;
	void body_reset_mass_properties(RID p_body) override;
	void body_set_state(RID p_body, PS2DE::BodyState p_state, const Variant &p_value) override;
	Variant body_get_state(RID p_body, PS2DE::BodyState p_state) const override;

	void body_apply_central_impulse(RID p_body, const Vector2 &p_impulse) override;
	void body_apply_torque_impulse(RID p_body, real_t p_impulse) override;
	void body_apply_impulse(RID p_body, const Vector2 &p_impulse, const Vector2 &p_position) override;
	void body_apply_central_force(RID p_body, const Vector2 &p_force) override;
	void body_apply_force(RID p_body, const Vector2 &p_force, const Vector2 &p_position) override;
	void body_apply_torque(RID p_body, real_t p_torque) override;
	void body_add_constant_central_force(RID p_body, const Vector2 &p_force) override;
	void body_add_constant_force(RID p_body, const Vector2 &p_force, const Vector2 &p_position) override;
	void body_add_constant_torque(RID p_body, real_t p_torque) override;
	void body_set_constant_force(RID p_body, const Vector2 &p_force) override;
	Vector2 body_get_constant_force(RID p_body) const override;
	void body_set_constant_torque(RID p_body, real_t p_torque) override;
	real_t body_get_constant_torque(RID p_body) const override;
	void body_set_axis_velocity(RID p_body, const Vector2 &p_axis_velocity) override;
	void body_add_collision_exception(RID p_body, RID p_excepted_body) override;
	void body_remove_collision_exception(RID p_body, RID p_excepted_body) override;
	TypedArray<RID> _body_get_collision_exceptions_compat(RID p_body) const;
	void body_set_max_contacts_reported(RID p_body, int p_amount) override;
	int body_get_max_contacts_reported(RID p_body) const override;
	void body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) override;
	real_t body_get_contacts_reported_depth_threshold(RID p_body) const override;
	void body_set_omit_force_integration(RID p_body, bool p_enable) override;
	bool body_is_omitting_force_integration(RID p_body) const override;
	void body_set_state_sync_callback(RID p_body, const Callable &p_callable) override;
	void body_set_force_integration_callback(RID p_body, const Callable &p_callable, const Variant &p_userdata) override;

	/// This method is unused.
	bool _body_collide_shape_compat(RID p_body, int32_t p_body_shape, RID p_shape, const Transform2D &p_shape_xform, const Vector2 &p_motion, void *p_results, int32_t p_result_max, int32_t *p_result_count) { return false; }

	void body_set_pickable(RID p_body, bool p_pickable) override;
	PhysicsDirectBodyState2D *body_get_direct_state(RID p_body) override;
	bool _body_test_motion_compat(RID p_body, const Transform2D &p_from, const Vector2 &p_motion, real_t p_margin, bool p_collide_separation_ray, bool p_recovery_as_collision, PS2DT::MotionResult *p_result) const;

	void joint_make_configured(RID p_joint, PS2DE::JointType p_type, RID p_body_a, const Transform2D &p_frame_a, RID p_body_b, const Transform2D &p_frame_b, const Dictionary &p_configuration) override;
	void joint_set_configuration(RID p_joint, const Dictionary &p_configuration) override;
	Dictionary joint_get_configuration(RID p_joint) const override;

	RID joint_create() override;
	void joint_clear(RID p_joint) override;
	void joint_set_param(RID p_joint, PS2DE::JointParam p_param, real_t p_value) override;
	real_t joint_get_param(RID p_joint, PS2DE::JointParam p_param) const override;
	void joint_disable_collisions_between_bodies(RID p_joint, const bool p_disable) override;
	bool joint_is_disabled_collisions_between_bodies(RID p_joint) const override;

	void joint_make_pin(RID p_joint, const Vector2 &p_anchor, RID p_body_a, RID p_body_b) override;
	void joint_make_groove(RID p_joint, const Vector2 &p_a_groove1, const Vector2 &p_a_groove2, const Vector2 &p_b_anchor, RID p_body_a, RID p_body_b) override;
	void joint_make_damped_spring(RID p_joint, const Vector2 &p_anchor_a, const Vector2 &p_anchor_b, RID p_body_a, RID p_body_b) override;

	void pin_joint_set_flag(RID p_joint, PS2DE::PinJointFlag p_flag, bool p_enabled) override;
	bool pin_joint_get_flag(RID p_joint, PS2DE::PinJointFlag p_flag) const override;
	void pin_joint_set_param(RID p_joint, PS2DE::PinJointParam p_param, real_t p_value) override;
	real_t pin_joint_get_param(RID p_joint, PS2DE::PinJointParam p_param) const override;

	void damped_spring_joint_set_param(RID p_joint, PS2DE::DampedSpringParam p_param, real_t p_value) override;
	real_t damped_spring_joint_get_param(RID p_joint, PS2DE::DampedSpringParam p_param) const override;
	PS2DE::JointType joint_get_type(RID p_joint) const override;

	void free_rid(RID p_rid) override;
	void set_active(bool p_active) override;
	void init() override;
	void step(real_t p_step) override;
	void sync() override {}
	void flush_queries() override;
	void end_sync() override {}
	void finish() override;
	bool is_flushing_queries() const override;
	int get_process_info(PS2DE::ProcessInfo p_process_info) override;

	Box2DPhysicsServer2D();
	~Box2DPhysicsServer2D();

	Box2DShape2D *get_shape(RID p_rid) const { return shape_owner.get_or_null(p_rid); }
	Box2DBody2D *get_body(RID p_rid) const { return body_owner.get_or_null(p_rid); }
	Box2DCollisionObject2D *get_object(RID rid) const {
		if (auto *body = body_owner.get_or_null(rid)) {
			return body;
		}
		return area_owner.get_or_null(rid);
	}

private:
	static void _bind_methods();

	int fixed_tick_rate = 60;
	bool active = true;
	bool flushing_queries = false;

	LocalVector<Box2DBody2D *> bodies_to_delete;
	LocalVector<Box2DArea2D *> areas_to_delete;

	mutable RID_PtrOwner<Box2DSpace2D> space_owner;
	mutable RID_PtrOwner<Box2DBody2D> body_owner;
	mutable RID_PtrOwner<Box2DArea2D> area_owner;
	mutable RID_PtrOwner<Box2DShape2D> shape_owner;
	mutable RID_PtrOwner<Box2DJoint2D> joint_owner;

	HashSet<Box2DSpace2D *> active_spaces;

	static Box2DPhysicsServer2D *singleton;
};
