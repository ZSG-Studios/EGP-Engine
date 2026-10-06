// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "precompiled.hpp"
#pragma once

#include "core/templates/rid_owner.h"

class Box3DAreaImpl3D;
class Box3DBodyImpl3D;
class Box3DSoftBodyImpl3D;
class Box3DJointImpl3D;
class Box3DShapeImpl3D;
class Box3DShapedObjectImpl3D;
class Box3DSpace3D;

// Entry point for every PhysicsServer3D virtual. Owns all RID registries and drives
// step()/flush_queries() across every active Box3DSpace3D.
class Box3DPhysicsServer3D final : public PhysicsServer3D {
	GDCLASS(Box3DPhysicsServer3D, PhysicsServer3D)

public:
	static Box3DPhysicsServer3D *get_singleton() { return singleton; }

	Box3DPhysicsServer3D();

	~Box3DPhysicsServer3D() override;

	Box3DShapeImpl3D *get_shape(RID p_rid) const { return shape_owner.get_or_null(p_rid); }

	Box3DBodyImpl3D *get_body(RID p_rid) const { return body_owner.get_or_null(p_rid); }

	Box3DAreaImpl3D *get_area(RID p_rid) const { return area_owner.get_or_null(p_rid); }

	Box3DSpace3D *get_space(RID p_rid) const { return space_owner.get_or_null(p_rid); }

	Box3DJointImpl3D *get_joint(RID p_rid) const { return joint_owner.get_or_null(p_rid); }

	// --- Shapes ---
	RID world_boundary_shape_create() override;
	RID sphere_shape_create() override;
	RID box_shape_create() override;
	RID capsule_shape_create() override;
	RID cylinder_shape_create() override;
	RID convex_polygon_shape_create() override;
	RID concave_polygon_shape_create() override;
	RID heightmap_shape_create() override;

	void shape_set_data(RID p_shape, const Variant &p_data) override;
	PS3DE::ShapeType shape_get_type(RID p_shape) const override;
	Variant shape_get_data(RID p_shape) const override;

	// --- Space ---
	void space_apply_explosion(RID p_space, const Vector3 &p_position, real_t p_radius, real_t p_falloff, real_t p_impulse_density, uint32_t p_collision_mask) override;
	Array space_get_contact_hit_events(RID p_space) const override;
	Array space_get_joint_events(RID p_space) const override;
	Vector3 joint_get_constraint_force(RID p_joint) const override;
	Vector3 joint_get_constraint_torque(RID p_joint) const override;

	RID space_create() override;
	void space_set_active(RID p_space, bool p_active) override;
	bool space_is_active(RID p_space) const override;
	void space_set_param(RID p_space, PS3DE::SpaceParameter p_param, real_t p_value) override;
	real_t space_get_param(RID p_space, PS3DE::SpaceParameter p_param) const override;
	PhysicsDirectSpaceState3D *space_get_direct_state(RID p_space) override;
	void space_set_debug_contacts(RID p_space, int32_t p_max_contacts) override;
	Vector<Vector3> space_get_contacts(RID p_space) const override;
	int32_t space_get_contact_count(RID p_space) const override;

	// --- Areas ---
	RID area_create() override;
	void area_set_space(RID p_area, RID p_space) override;
	RID area_get_space(RID p_area) const override;
	void area_add_shape(RID p_area, RID p_shape, const Transform3D &p_transform, bool p_disabled) override;
	void area_set_shape(RID p_area, int32_t p_shape_idx, RID p_shape) override;
	void area_set_shape_transform(RID p_area, int32_t p_shape_idx, const Transform3D &p_transform) override;
	void area_set_shape_disabled(RID p_area, int32_t p_shape_idx, bool p_disabled) override;
	int32_t area_get_shape_count(RID p_area) const override;
	RID area_get_shape(RID p_area, int32_t p_shape_idx) const override;
	Transform3D area_get_shape_transform(RID p_area, int32_t p_shape_idx) const override;
	void area_remove_shape(RID p_area, int32_t p_shape_idx) override;
	void area_clear_shapes(RID p_area) override;
	void area_attach_object_instance_id(RID p_area, ObjectID p_id) override;
	ObjectID area_get_object_instance_id(RID p_area) const override;
	void area_set_param(RID p_area, PS3DE::AreaParameter p_param, const Variant &p_value) override;
	void area_set_transform(RID p_area, const Transform3D &p_transform) override;
	Variant area_get_param(RID p_area, PS3DE::AreaParameter p_param) const override;
	Transform3D area_get_transform(RID p_area) const override;
	void area_set_collision_layer(RID p_area, uint32_t p_layer) override;
	uint32_t area_get_collision_layer(RID p_area) const override;
	void area_set_collision_mask(RID p_area, uint32_t p_mask) override;
	uint32_t area_get_collision_mask(RID p_area) const override;
	void area_set_monitorable(RID p_area, bool p_monitorable) override;
	void area_set_ray_pickable(RID p_area, bool p_enable) override;
	void area_set_monitor_callback(RID p_area, const Callable &p_callback) override;
	void area_set_area_monitor_callback(RID p_area, const Callable &p_callback) override;

	// --- Bodies ---
	RID body_create() override;
	void body_set_space(RID p_body, RID p_space) override;
	RID body_get_space(RID p_body) const override;
	void body_set_mode(RID p_body, PS3DE::BodyMode p_mode) override;
	PS3DE::BodyMode body_get_mode(RID p_body) const override;
	void body_add_shape(RID p_body, RID p_shape, const Transform3D &p_transform, bool p_disabled) override;
	void body_set_shape(RID p_body, int32_t p_shape_idx, RID p_shape) override;
	void body_set_shape_transform(RID p_body, int32_t p_shape_idx, const Transform3D &p_transform) override;
	void body_set_shape_disabled(RID p_body, int32_t p_shape_idx, bool p_disabled) override;
	int32_t body_get_shape_count(RID p_body) const override;
	RID body_get_shape(RID p_body, int32_t p_shape_idx) const override;
	Transform3D body_get_shape_transform(RID p_body, int32_t p_shape_idx) const override;
	void body_remove_shape(RID p_body, int32_t p_shape_idx) override;
	void body_clear_shapes(RID p_body) override;
	void body_attach_object_instance_id(RID p_body, ObjectID p_id) override;
	ObjectID body_get_object_instance_id(RID p_body) const override;
	void body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) override;
	bool body_is_continuous_collision_detection_enabled(RID p_body) const override;
	void body_set_collision_layer(RID p_body, uint32_t p_layer) override;
	uint32_t body_get_collision_layer(RID p_body) const override;
	void body_set_collision_mask(RID p_body, uint32_t p_mask) override;
	uint32_t body_get_collision_mask(RID p_body) const override;
	void body_set_user_flags(RID p_body, uint32_t p_flags) override;
	uint32_t body_get_user_flags(RID p_body) const override;
	void body_set_param(RID p_body, PS3DE::BodyParameter p_param, const Variant &p_value) override;
	Variant body_get_param(RID p_body, PS3DE::BodyParameter p_param) const override;
	void body_reset_mass_properties(RID p_body) override;
	void body_set_state(RID p_body, PS3DE::BodyState p_state, const Variant &p_value) override;
	Variant body_get_state(RID p_body, PS3DE::BodyState p_state) const override;
	void body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) override;
	void body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_position) override;
	void body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) override;
	void body_apply_central_force(RID p_body, const Vector3 &p_force) override;
	void body_apply_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) override;
	void body_apply_torque(RID p_body, const Vector3 &p_torque) override;
	void body_add_constant_central_force(RID p_body, const Vector3 &p_force) override;
	void body_add_constant_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) override;
	void body_add_constant_torque(RID p_body, const Vector3 &p_torque) override;
	void body_set_constant_force(RID p_body, const Vector3 &p_force) override;
	Vector3 body_get_constant_force(RID p_body) const override;
	void body_set_constant_torque(RID p_body, const Vector3 &p_torque) override;
	Vector3 body_get_constant_torque(RID p_body) const override;
	void body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) override;
	void body_set_axis_lock(RID p_body, PS3DE::BodyAxis p_axis, bool p_lock) override;
	bool body_is_axis_locked(RID p_body, PS3DE::BodyAxis p_axis) const override;
	void body_add_collision_exception(RID p_body, RID p_excepted_body) override;
	void body_remove_collision_exception(RID p_body, RID p_excepted_body) override;
	TypedArray<RID> _body_get_collision_exceptions_compat(RID p_body) const;
	void body_set_max_contacts_reported(RID p_body, int32_t p_amount) override;
	int32_t body_get_max_contacts_reported(RID p_body) const override;
	void body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) override;
	real_t body_get_contacts_reported_depth_threshold(RID p_body) const override;
	void body_set_omit_force_integration(RID p_body, bool p_enable) override;
	bool body_is_omitting_force_integration(RID p_body) const override;
	void body_set_state_sync_callback(RID p_body, const Callable &p_callable) override;
	void body_set_force_integration_callback(RID p_body, const Callable &p_callable, const Variant &p_userdata) override;
	void body_set_ray_pickable(RID p_body, bool p_enable) override;

	bool _body_test_motion_compat(
			RID p_body,
			const Transform3D &p_from,
			const Vector3 &p_motion,
			real_t p_margin,
			int32_t p_max_collisions,
			bool p_recovery_as_collision,
			PS3DT::MotionResult *p_result) const;

	PhysicsDirectBodyState3D *body_get_direct_state(RID p_body) override;

	// --- Joints ---
	void joint_make_configured(RID p_joint, PS3DE::JointType p_type, RID p_body_a, const Transform3D &p_frame_a, RID p_body_b, const Transform3D &p_frame_b, const Dictionary &p_configuration) override;
	void joint_set_configuration(RID p_joint, const Dictionary &p_configuration) override;
	Dictionary joint_get_configuration(RID p_joint) const override;

	RID joint_create() override;
	void joint_clear(RID p_joint) override;

	void joint_make_pin(RID p_joint, RID p_body_a, const Vector3 &p_local_a, RID p_body_b, const Vector3 &p_local_b) override;
	void pin_joint_set_param(RID p_joint, PS3DE::PinJointParam p_param, real_t p_value) override;
	real_t pin_joint_get_param(RID p_joint, PS3DE::PinJointParam p_param) const override;
	void pin_joint_set_local_a(RID p_joint, const Vector3 &p_local_a) override;
	Vector3 pin_joint_get_local_a(RID p_joint) const override;
	void pin_joint_set_local_b(RID p_joint, const Vector3 &p_local_b) override;
	Vector3 pin_joint_get_local_b(RID p_joint) const override;

	void joint_make_hinge(RID p_joint, RID p_body_a, const Transform3D &p_hinge_a, RID p_body_b, const Transform3D &p_hinge_b) override;
	void joint_make_hinge_simple(RID p_joint, RID p_body_a, const Vector3 &p_pivot_a, const Vector3 &p_axis_a, RID p_body_b, const Vector3 &p_pivot_b, const Vector3 &p_axis_b) override;
	void hinge_joint_set_param(RID p_joint, PS3DE::HingeJointParam p_param, real_t p_value) override;
	real_t hinge_joint_get_param(RID p_joint, PS3DE::HingeJointParam p_param) const override;
	void hinge_joint_set_flag(RID p_joint, PS3DE::HingeJointFlag p_flag, bool p_enabled) override;
	bool hinge_joint_get_flag(RID p_joint, PS3DE::HingeJointFlag p_flag) const override;

	void joint_make_slider(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) override;
	void slider_joint_set_param(RID p_joint, PS3DE::SliderJointParam p_param, real_t p_value) override;
	real_t slider_joint_get_param(RID p_joint, PS3DE::SliderJointParam p_param) const override;

	// Cone/twist and independently limited/driven 6DOF constraints.
	void joint_make_cone_twist(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) override;
	void cone_twist_joint_set_param(RID p_joint, PS3DE::ConeTwistJointParam p_param, real_t p_value) override;
	real_t cone_twist_joint_get_param(RID p_joint, PS3DE::ConeTwistJointParam p_param) const override;

	void joint_make_generic_6dof(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) override;
	void generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisParam p_param, real_t p_value) override;
	real_t generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisParam p_param) const override;
	void generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisFlag p_flag, bool p_enable) override;
	bool generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisFlag p_flag) const override;

	PS3DE::JointType joint_get_type(RID p_joint) const override;
	void joint_disable_collisions_between_bodies(RID p_joint, bool p_disable) override;
	bool joint_is_disabled_collisions_between_bodies(RID p_joint) const override;

	// --- Soft bodies ---
	RID soft_body_create() override;
	void soft_body_update_rendering_server(RID p_body, RequiredParam<PhysicsServer3DRenderingServerHandler> p_rendering_server_handler) override;
	void soft_body_set_space(RID p_body, RID p_space) override;
	RID soft_body_get_space(RID p_body) const override;
	void soft_body_set_ray_pickable(RID p_body, bool p_enable) override;
	void soft_body_set_collision_layer(RID p_body, uint32_t p_layer) override;
	uint32_t soft_body_get_collision_layer(RID p_body) const override;
	void soft_body_set_collision_mask(RID p_body, uint32_t p_mask) override;
	uint32_t soft_body_get_collision_mask(RID p_body) const override;
	void soft_body_add_collision_exception(RID p_body, RID p_excepted_body) override;
	void soft_body_remove_collision_exception(RID p_body, RID p_excepted_body) override;
	TypedArray<RID> _soft_body_get_collision_exceptions_compat(RID p_body) const;
	void soft_body_set_state(RID p_body, PS3DE::BodyState p_state, const Variant &p_variant) override;
	Variant soft_body_get_state(RID p_body, PS3DE::BodyState p_state) const override;
	void soft_body_set_transform(RID p_body, const Transform3D &p_transform) override;
	void soft_body_set_simulation_precision(RID p_body, int32_t p_simulation_precision) override;
	int32_t soft_body_get_simulation_precision(RID p_body) const override;
	void soft_body_set_total_mass(RID p_body, real_t p_total_mass) override;
	real_t soft_body_get_total_mass(RID p_body) const override;
	void soft_body_set_linear_stiffness(RID p_body, real_t p_stiffness) override;
	real_t soft_body_get_linear_stiffness(RID p_body) const override;
	void soft_body_set_pressure_coefficient(RID p_body, real_t p_pressure_coefficient) override;
	real_t soft_body_get_pressure_coefficient(RID p_body) const override;
	void soft_body_set_damping_coefficient(RID p_body, real_t p_damping_coefficient) override;
	real_t soft_body_get_damping_coefficient(RID p_body) const override;
	void soft_body_set_drag_coefficient(RID p_body, real_t p_drag_coefficient) override;
	real_t soft_body_get_drag_coefficient(RID p_body) const override;
	void soft_body_set_mesh(RID p_body, RID p_mesh) override;
	AABB soft_body_get_bounds(RID p_body) const override;
	void soft_body_move_point(RID p_body, int32_t p_point_index, const Vector3 &p_global_position) override;
	Vector3 soft_body_get_point_global_position(RID p_body, int32_t p_point_index) const override;
	void soft_body_remove_all_pinned_points(RID p_body) override;
	void soft_body_pin_point(RID p_body, int32_t p_point_index, bool p_pin) override;
	bool soft_body_is_point_pinned(RID p_body, int32_t p_point_index) const override;

	void free_rid(RID p_rid) override;

	void set_active(bool p_active) override;
	void init() override;
	void step(real_t p_step) override;
	void sync() override;
	void flush_queries() override;
	void end_sync() override;
	void finish() override;
	bool is_flushing_queries() const override;
	int32_t get_process_info(PS3DE::ProcessInfo p_process_info) override;

	void body_get_collision_exceptions(RID body, List<RID> *out) override {
		for (RID r : _body_get_collision_exceptions_compat(body)) {
			out->push_back(r);
		}
	}
	void soft_body_get_collision_exceptions(RID body, List<RID> *out) override;
	bool body_test_motion(RID body, const PS3DT::MotionParameters &p, PS3DT::MotionResult *out = nullptr) override;
	void generic_6dof_joint_set_angular_target_rotation(RID, const Quaternion &) override;
	Quaternion generic_6dof_joint_get_angular_target_rotation(RID) const override;
	void soft_body_apply_point_impulse(RID, int, const Vector3 &) override;
	void soft_body_apply_point_force(RID, int, const Vector3 &) override;
	void soft_body_apply_central_impulse(RID, const Vector3 &) override;
	void soft_body_apply_central_force(RID, const Vector3 &) override;

protected:
	void soft_body_set_shrinking_factor(RID, real_t) override;
	real_t soft_body_get_shrinking_factor(RID) const override;
	static void _bind_methods() {}

private:
	Box3DShapedObjectImpl3D *_get_shaped_object(RID p_rid) const;

	// Godot passes a space RID to the area API to reach that space's default area.
	RID _resolve_area_rid(RID p_rid) const;

	// Destroys every collision exception referencing a body, both directions.
	void _clear_collision_exceptions(Box3DBodyImpl3D *p_body);

	static Box3DPhysicsServer3D *singleton;

	mutable RID_PtrOwner<Box3DSpace3D> space_owner;

	mutable RID_PtrOwner<Box3DBodyImpl3D> body_owner;
	mutable RID_PtrOwner<Box3DSoftBodyImpl3D> soft_body_owner;

	mutable RID_PtrOwner<Box3DAreaImpl3D> area_owner;

	mutable RID_PtrOwner<Box3DShapeImpl3D> shape_owner;

	mutable RID_PtrOwner<Box3DJointImpl3D> joint_owner;

	HashSet<Box3DSpace3D *> active_spaces;
	// Narrows the reverse lookup when freeing a body, since exceptions are stored one-sided.
	HashSet<Box3DBodyImpl3D *> bodies_with_exceptions;
	bool active = true;
	int fixed_tick_rate = 0;
};
