// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "objects/box3d_body_impl_3d.hpp"
#include "objects/box3d_physics_direct_body_state_3d.hpp"
#include "precompiled.hpp"
#include "spaces/box3d_physics_direct_space_state_3d.hpp"
#include "spaces/box3d_space_3d.hpp"

#include "servers/box3d_physics_server_3d.hpp"

bool Box3DPhysicsDirectSpaceState3D::intersect_ray(const PS3DT::RayParameters &p, PS3DT::RayResult &r) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_ray(p.from, p.to, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, p.hit_from_inside, p.hit_back_faces, p.pick_ray, &r);
}
int Box3DPhysicsDirectSpaceState3D::intersect_point(const PS3DT::PointParameters &p, PS3DT::ShapeResult *r, int max) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_point(p.position, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, r, max);
}
int Box3DPhysicsDirectSpaceState3D::intersect_shape(const PS3DT::ShapeParameters &p, PS3DT::ShapeResult *r, int max) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_shape(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, r, max);
}
bool Box3DPhysicsDirectSpaceState3D::cast_motion(const PS3DT::ShapeParameters &p, real_t &safe, real_t &unsafe, PS3DT::ShapeRestInfo *r) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _cast_motion(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, &safe, &unsafe, r);
}
bool Box3DPhysicsDirectSpaceState3D::collide_shape(const PS3DT::ShapeParameters &p, Vector3 *r, int max, int &count) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _collide_shape(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, r, max, &count);
}
bool Box3DPhysicsDirectSpaceState3D::rest_info(const PS3DT::ShapeParameters &p, PS3DT::ShapeRestInfo *r) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _rest_info(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, r);
}
bool Box3DPhysicsServer3D::body_test_motion(RID p_body, const PS3DT::MotionParameters &p, PS3DT::MotionResult *out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = get_body(p_body);
	ERR_FAIL_NULL_V(body, false);
	ERR_FAIL_NULL_V(body->get_space(), false);
	auto *state = body->get_space()->get_direct_state();
	state->query_exclude = p.exclude_bodies;
	state->motion_exclude_objects = p.exclude_objects;
	PS3DT::MotionResult temporary;
	const bool hit = _body_test_motion_compat(p_body, p.from, p.motion, p.margin, p.max_collisions, p.collide_separation_ray, p.recovery_as_collision, out ? out : &temporary);
	state->query_exclude.clear();
	state->motion_exclude_objects.clear();
	return hit;
}
void Box3DPhysicsDirectBodyState3D::set_collision_layer(uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	body->set_collision_layer(p_layer);
}
uint32_t Box3DPhysicsDirectBodyState3D::get_collision_layer() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	return body->get_collision_layer();
}
void Box3DPhysicsDirectBodyState3D::set_collision_mask(uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	body->set_collision_mask(p_mask);
}
uint32_t Box3DPhysicsDirectBodyState3D::get_collision_mask() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	return body->get_collision_mask();
}
