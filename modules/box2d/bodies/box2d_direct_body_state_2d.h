// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_body_2d.h"

#include "modules/box2d/precompiled.h"

using namespace PhysicsServer2DEnums;

class Box2DBody2D;

class Box2DDirectBodyState2D : public PhysicsDirectBodyState2D {
	GDCLASS(Box2DDirectBodyState2D, PhysicsDirectBodyState2D);

	Box2DBody2D *body = nullptr;

protected:
	static void _bind_methods() {}

public:
	void set_collision_layer(uint32_t layer) override;
	uint32_t get_collision_layer() const override;
	void set_collision_mask(uint32_t mask) override;
	uint32_t get_collision_mask() const override;
	Vector2 get_contact_local_velocity_at_position(int index) const override;
	Box2DDirectBodyState2D() = default;
	explicit Box2DDirectBodyState2D(Box2DBody2D *p_body) :
			body(p_body) {}

	Vector2 get_total_gravity() const override;
	real_t get_total_angular_damp() const override;
	real_t get_total_linear_damp() const override;

	Vector2 get_center_of_mass() const override;
	Vector2 get_center_of_mass_local() const override;
	real_t get_inverse_mass() const override;
	real_t get_inverse_inertia() const override;

	void set_linear_velocity(const Vector2 &p_velocity) override;
	Vector2 get_linear_velocity() const override;

	void set_angular_velocity(real_t p_velocity) override;
	real_t get_angular_velocity() const override;

	void set_transform(const Transform2D &p_transform) override;
	Transform2D get_transform() const override;

	Vector2 get_velocity_at_local_position(const Vector2 &p_position) const override;

	void apply_central_impulse(const Vector2 &p_impulse) override;
	void apply_impulse(const Vector2 &p_impulse, const Vector2 &p_position) override;
	void apply_torque_impulse(real_t p_torque) override;

	void apply_central_force(const Vector2 &p_force) override;
	void apply_force(const Vector2 &p_force, const Vector2 &p_position) override;
	void apply_torque(real_t p_torque) override;

	void add_constant_central_force(const Vector2 &p_force) override;
	void add_constant_force(const Vector2 &p_force, const Vector2 &p_position) override;
	void add_constant_torque(real_t p_torque) override;
	void set_constant_force(const Vector2 &p_force) override;
	Vector2 get_constant_force() const override;
	void set_constant_torque(real_t p_torque) override;
	real_t get_constant_torque() const override;

	void set_sleep_state(bool p_enable) override;
	bool is_sleeping() const override;

	int get_contact_count() const override;
	Vector2 get_contact_local_position(int p_contact_idx) const override;
	Vector2 get_contact_local_normal(int p_contact_idx) const override;
	int get_contact_local_shape(int p_contact_idx) const override;
	RID get_contact_collider(int p_contact_idx) const override;
	Vector2 get_contact_collider_position(int p_contact_idx) const override;
	ObjectID get_contact_collider_id(int p_contact_idx) const override;
	int get_contact_collider_shape(int p_contact_idx) const override;
	Vector2 get_contact_impulse(int p_contact_idx) const override;
	Vector2 get_contact_collider_velocity_at_position(int p_contact_idx) const override;

	RequiredResult<PhysicsDirectSpaceState2D> get_space_state() override;

	real_t get_step() const override;
};
