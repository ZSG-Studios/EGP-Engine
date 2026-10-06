// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "precompiled.hpp"
#pragma once

class Box3DBodyImpl3D;

// Per-body live-state accessor handed to scripts inside integrate_forces() and to
// Godot core's move_and_slide(). Thin passthrough to the Box3DBodyImpl3D/b3BodyId it wraps.
class Box3DPhysicsDirectBodyState3D final : public PhysicsDirectBodyState3D {
	GDCLASS(Box3DPhysicsDirectBodyState3D, PhysicsDirectBodyState3D)

public:
	void set_body(Box3DBodyImpl3D *p_body) { body = p_body; }

	Box3DBodyImpl3D *get_body() const { return body; }

	Vector3 get_total_gravity() const override;
	real_t get_total_angular_damp() const override;
	real_t get_total_linear_damp() const override;

	Vector3 get_center_of_mass() const override;
	Vector3 get_center_of_mass_local() const override;
	Basis get_principal_inertia_axes() const override;

	real_t get_inverse_mass() const override;
	Vector3 get_inverse_inertia() const override;
	Basis get_inverse_inertia_tensor() const override;

	void set_linear_velocity(const Vector3 &p_velocity) override;
	Vector3 get_linear_velocity() const override;

	void set_angular_velocity(const Vector3 &p_velocity) override;
	Vector3 get_angular_velocity() const override;

	void set_transform(const Transform3D &p_transform) override;
	Transform3D get_transform() const override;

	Vector3 get_velocity_at_local_position(const Vector3 &p_local_position) const override;

	void apply_central_impulse(const Vector3 &p_impulse) override;
	void apply_impulse(const Vector3 &p_impulse, const Vector3 &p_position) override;
	void apply_torque_impulse(const Vector3 &p_impulse) override;

	void apply_central_force(const Vector3 &p_force) override;
	void apply_force(const Vector3 &p_force, const Vector3 &p_position) override;
	void apply_torque(const Vector3 &p_torque) override;

	void add_constant_central_force(const Vector3 &p_force) override;
	void add_constant_force(const Vector3 &p_force, const Vector3 &p_position) override;
	void add_constant_torque(const Vector3 &p_torque) override;

	void set_constant_force(const Vector3 &p_force) override;
	Vector3 get_constant_force() const override;

	void set_constant_torque(const Vector3 &p_torque) override;
	Vector3 get_constant_torque() const override;

	void set_sleep_state(bool p_enabled) override;
	bool is_sleeping() const override;

	int32_t get_contact_count() const override;

	Vector3 get_contact_local_position(int32_t p_index) const override;
	Vector3 get_contact_local_normal(int32_t p_index) const override;
	Vector3 get_contact_impulse(int32_t p_index) const override;
	int32_t get_contact_local_shape(int32_t p_index) const override;
	Vector3 get_contact_local_velocity_at_position(int32_t p_index) const override;

	RID get_contact_collider(int32_t p_index) const override;
	Vector3 get_contact_collider_position(int32_t p_index) const override;
	ObjectID get_contact_collider_id(int32_t p_index) const override;
	Object *get_contact_collider_object(int32_t p_index) const override;
	int32_t get_contact_collider_shape(int32_t p_index) const override;
	Vector3 get_contact_collider_velocity_at_position(int32_t p_index) const override;

	real_t get_step() const override;
	void integrate_forces() override;

	RequiredResult<PhysicsDirectSpaceState3D> get_space_state() override;

	void set_collision_layer(uint32_t) override;
	uint32_t get_collision_layer() const override;
	void set_collision_mask(uint32_t) override;
	uint32_t get_collision_mask() const override;

protected:
	static void _bind_methods() {}

private:
	Box3DBodyImpl3D *body = nullptr;
};
