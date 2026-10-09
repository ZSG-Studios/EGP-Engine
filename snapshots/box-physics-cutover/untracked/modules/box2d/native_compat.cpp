// SPDX-License-Identifier: MIT
#include "bodies/box2d_direct_body_state_2d.h"
#include "box2d_physics_server_2d.h"
namespace egp::box2d {
thread_local HashSet<RID> motion_exclude_bodies;
thread_local HashSet<ObjectID> motion_exclude_objects;
} //namespace egp::box2d

bool Box2DPhysicsServer2D::shape_collide(RID a, const Transform2D &ta, const Vector2 &ma, RID b, const Transform2D &tb, const Vector2 &mb, Vector2 *r, int max, int &count) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return _shape_collide_compat(a, ta, ma, b, tb, mb, r, max, &count);
}
bool Box2DPhysicsServer2D::body_collide_shape(RID body, int index, RID shape, const Transform2D &transform, const Vector2 &motion, Vector2 *r, int max, int &count) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	count = 0;
	auto *owner = get_body(body);
	ERR_FAIL_NULL_V(owner, false);
	ERR_FAIL_INDEX_V(index, owner->get_shape_count(), false);
	return shape_collide(body_get_shape(body, index), owner->get_transform() * owner->get_shape_transform(index), Vector2(), shape, transform, motion, r, max, count);
}
void Box2DPhysicsServer2D::body_get_collision_exceptions(RID body, List<RID> *out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_NULL(out);
	TypedArray<RID> values = _body_get_collision_exceptions_compat(body);
	for (int i = 0; i < values.size(); i++) {
		out->push_back(values[i]);
	}
}
bool Box2DPhysicsServer2D::body_test_motion(RID body, const PS2DT::MotionParameters &p, PS2DT::MotionResult *out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	PS2DT::MotionResult temporary;
	HashSet<RID> previous_bodies;
	HashSet<ObjectID> previous_objects;
	previous_bodies = egp::box2d::motion_exclude_bodies;
	previous_objects = egp::box2d::motion_exclude_objects;
	egp::box2d::motion_exclude_bodies = p.exclude_bodies;
	egp::box2d::motion_exclude_objects = p.exclude_objects;
	const bool hit = _body_test_motion_compat(body, p.from, p.motion, p.margin, p.collide_separation_ray, p.recovery_as_collision, out ? out : &temporary);
	egp::box2d::motion_exclude_bodies = previous_bodies;
	egp::box2d::motion_exclude_objects = previous_objects;
	return hit;
}
bool Box2DDirectSpaceState2D::intersect_ray(const PS2DT::RayParameters &p, PS2DT::RayResult &out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_ray(p.from, p.to, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, p.hit_from_inside, &out);
}
int Box2DDirectSpaceState2D::intersect_point(const PS2DT::PointParameters &p, PS2DT::ShapeResult *out, int max) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_point(p.position, uint64_t(p.canvas_instance_id), p.collision_mask, p.collide_with_bodies, p.collide_with_areas, out, max);
}
int Box2DDirectSpaceState2D::intersect_shape(const PS2DT::ShapeParameters &p, PS2DT::ShapeResult *out, int max) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _intersect_shape(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, out, max);
}
bool Box2DDirectSpaceState2D::cast_motion(const PS2DT::ShapeParameters &p, real_t &safe, real_t &unsafe) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _cast_motion(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, &safe, &unsafe);
}
bool Box2DDirectSpaceState2D::collide_shape(const PS2DT::ShapeParameters &p, Vector2 *out, int max, int &count) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _collide_shape(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, out, max, &count);
}
bool Box2DDirectSpaceState2D::rest_info(const PS2DT::ShapeParameters &p, PS2DT::ShapeRestInfo *out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	query_exclude = p.exclude;
	return _rest_info(p.shape_rid, p.transform, p.motion, p.margin, p.collision_mask, p.collide_with_bodies, p.collide_with_areas, out);
}
void Box2DDirectBodyState2D::set_collision_layer(uint32_t layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_collision_layer(layer);
}
uint32_t Box2DDirectBodyState2D::get_collision_layer() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_collision_layer();
}
void Box2DDirectBodyState2D::set_collision_mask(uint32_t mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	body->set_collision_mask(mask);
}
uint32_t Box2DDirectBodyState2D::get_collision_mask() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_collision_mask();
}
Vector2 Box2DDirectBodyState2D::get_contact_local_velocity_at_position(int index) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return body->get_velocity_at_point(body->get_contact_local_position(index));
}
