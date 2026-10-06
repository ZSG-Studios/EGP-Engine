// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_direct_body_state_2d.h"

#include "../box2d_physics_server_2d.h"
#include "../spaces/box2d_physics_direct_space_state_2d.h"
#include "../spaces/box2d_query.h"

Vector2 Box2DDirectBodyState2D::get_total_gravity() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND_V(!body->in_space(), Vector2());
	return body->get_total_gravity();
}

real_t Box2DDirectBodyState2D::get_total_angular_damp() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND_V(!body->in_space(), 0.0);
	return body->get_total_angular_damp();
}

real_t Box2DDirectBodyState2D::get_total_linear_damp() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND_V(!body->in_space(), 0.0);
	return body->get_total_linear_damp();
}

Vector2 Box2DDirectBodyState2D::get_center_of_mass() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_center_of_mass_global();
}

Vector2 Box2DDirectBodyState2D::get_center_of_mass_local() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_center_of_mass();
}

real_t Box2DDirectBodyState2D::get_inverse_mass() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_inverse_mass();
}

real_t Box2DDirectBodyState2D::get_inverse_inertia() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_inverse_inertia();
}

void Box2DDirectBodyState2D::set_linear_velocity(const Vector2 &p_velocity) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_linear_velocity(p_velocity);
}

Vector2 Box2DDirectBodyState2D::get_linear_velocity() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_linear_velocity();
}

void Box2DDirectBodyState2D::set_angular_velocity(real_t p_velocity) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_angular_velocity(p_velocity);
}

real_t Box2DDirectBodyState2D::get_angular_velocity() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_angular_velocity();
}

void Box2DDirectBodyState2D::set_transform(const Transform2D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_transform(p_transform);
}

Transform2D Box2DDirectBodyState2D::get_transform() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_transform();
}

Vector2 Box2DDirectBodyState2D::get_velocity_at_local_position(const Vector2 &p_position) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_velocity_at_local_point(p_position);
}

void Box2DDirectBodyState2D::apply_central_impulse(const Vector2 &p_impulse) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_impulse_center(p_impulse);
}

void Box2DDirectBodyState2D::apply_impulse(const Vector2 &p_impulse, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_impulse(p_impulse, p_position);
}

void Box2DDirectBodyState2D::apply_torque_impulse(real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_torque_impulse(p_torque);
}

void Box2DDirectBodyState2D::apply_central_force(const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_central_force(p_force);
}

void Box2DDirectBodyState2D::apply_force(const Vector2 &p_force, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_force(p_force, p_position);
}

void Box2DDirectBodyState2D::apply_torque(real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->apply_torque(p_torque);
}

void Box2DDirectBodyState2D::add_constant_central_force(const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->add_constant_central_force(p_force);
}

void Box2DDirectBodyState2D::add_constant_force(const Vector2 &p_force, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->add_constant_force(p_force, p_position);
}

void Box2DDirectBodyState2D::add_constant_torque(real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->add_constant_torque(p_torque);
}

void Box2DDirectBodyState2D::set_constant_force(const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_constant_force(p_force);
}

Vector2 Box2DDirectBodyState2D::get_constant_force() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_constant_force();
}

void Box2DDirectBodyState2D::set_constant_torque(real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_constant_torque(p_torque);
}

real_t Box2DDirectBodyState2D::get_constant_torque() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_constant_torque();
}

void Box2DDirectBodyState2D::set_sleep_state(bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_sleep_state(p_enable);
}

bool Box2DDirectBodyState2D::is_sleeping() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->is_sleeping();
}

int Box2DDirectBodyState2D::get_contact_count() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_count();
}

Vector2 Box2DDirectBodyState2D::get_contact_local_position(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_local_position(p_contact_idx);
}

Vector2 Box2DDirectBodyState2D::get_contact_local_normal(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_local_normal(p_contact_idx);
}

int Box2DDirectBodyState2D::get_contact_local_shape(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_local_shape(p_contact_idx);
}

RID Box2DDirectBodyState2D::get_contact_collider(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_collider(p_contact_idx);
}

Vector2 Box2DDirectBodyState2D::get_contact_collider_position(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_collider_position(p_contact_idx);
}

ObjectID Box2DDirectBodyState2D::get_contact_collider_id(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return ObjectID(body->get_contact_collider_id(p_contact_idx));
}

int Box2DDirectBodyState2D::get_contact_collider_shape(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_collider_shape(p_contact_idx);
}

Vector2 Box2DDirectBodyState2D::get_contact_impulse(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_impulse(p_contact_idx);
}

Vector2 Box2DDirectBodyState2D::get_contact_collider_velocity_at_position(int p_contact_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_contact_collider_velocity_at_position(p_contact_idx);
}

RequiredResult<PhysicsDirectSpaceState2D> Box2DDirectBodyState2D::get_space_state() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_space()->get_direct_state();
}

real_t Box2DDirectBodyState2D::get_step() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_space()->get_last_step();
}