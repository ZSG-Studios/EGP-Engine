/**************************************************************************/
/*  box2d_physics_server_2d.cpp                                           */
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

#include "joints/box2d_configured_joint_2d.h"
// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_physics_server_2d.h"
#include "joints/box2d_damped_spring_joint_2d.h"
#include "joints/box2d_groove_joint_2d.h"
#include "joints/box2d_pin_joint_2d.h"
#include "shapes/box2d_capsule_shape_2d.h"
#include "shapes/box2d_circle_shape_2d.h"
#include "shapes/box2d_concave_polygon_shape_2d.h"
#include "shapes/box2d_convex_polygon_shape_2d.h"
#include "shapes/box2d_rectangle_shape_2d.h"
#include "shapes/box2d_segment_shape_2d.h"
#include "shapes/box2d_separation_ray_shape_2d.h"
#include "shapes/box2d_world_boundary_shape_2d.h"

namespace {
constexpr char PHYSICS_SERVER_NAME[] = "Box2DPhysicsServer2D";

bool make_query_primitives(const Box2DShape2D *shape, const Transform2D &transform, LocalVector<Box2DShapePrimitive> &out) {
	const Variant data = shape->get_data();
	switch (shape->get_type()) {
		case SHAPE_CIRCLE: {
			b2Circle primitive;
			if (!Box2DCircleShape2D::make_circle(transform, data, primitive)) {
				return false;
			}
			out.push_back(primitive);
			break;
		}
		case SHAPE_CAPSULE: {
			b2Capsule primitive;
			if (!Box2DCapsuleShape2D::make_capsule(transform, data, primitive)) {
				return false;
			}
			out.push_back(primitive);
			break;
		}
		case SHAPE_RECTANGLE: {
			b2Polygon primitive;
			if (!Box2DRectangleShape2D::make_rectangle(transform, data, primitive)) {
				return false;
			}
			out.push_back(primitive);
			break;
		}
		case SHAPE_CONVEX_POLYGON: {
			b2Polygon primitive;
			if (!Box2DConvexPolygonShape2D::make_polygon(transform, data, primitive)) {
				return false;
			}
			out.push_back(primitive);
			break;
		}
		case SHAPE_SEGMENT: {
			b2Segment primitive;
			if (!Box2DSegmentShape2D::make_segment(transform, data, primitive)) {
				return false;
			}
			out.push_back(primitive);
			break;
		}
		case SHAPE_CONCAVE_POLYGON: {
			ERR_FAIL_COND_V(data.get_type() != Variant::PACKED_VECTOR2_ARRAY, false);
			const PackedVector2Array points = data;
			ERR_FAIL_COND_V(points.size() % 2, false);
			for (int i = 0; i < points.size(); i += 2) {
				out.push_back(b2Segment{ to_box2d(transform.xform(points[i])), to_box2d(transform.xform(points[i + 1])) });
			}
			break;
		}
		case SHAPE_WORLD_BOUNDARY:
			ERR_PRINT_ONCE("WorldBoundaryShape2D collision queries are not supported by Box2D. Use a supported finite query shape.");
			return false;
		default:
			return false;
	}
	return !out.is_empty();
}
} //namespace

void Box2DPhysicsServer2D::_bind_methods() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ClassDB::bind_method(D_METHOD("space_get_body_move_events", "space"), &Box2DPhysicsServer2D::space_get_body_move_events);
	ClassDB::bind_method(D_METHOD("body_set_user_data", "body", "data"), &Box2DPhysicsServer2D::body_set_user_data);
	ClassDB::bind_method(D_METHOD("body_get_user_data", "body"), &Box2DPhysicsServer2D::body_get_user_data);
}

Box2DPhysicsServer2D::Box2DPhysicsServer2D() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	fixed_tick_rate = Engine::get_singleton()->get_physics_ticks_per_second();
	Engine *engine = Engine::get_singleton();

	if (engine->has_singleton(PHYSICS_SERVER_NAME)) {
		engine->remove_singleton(PHYSICS_SERVER_NAME);
	}

	engine->add_singleton(Engine::Singleton(PHYSICS_SERVER_NAME, this));
}

Box2DPhysicsServer2D::~Box2DPhysicsServer2D() {
	finish();
	Engine::get_singleton()->remove_singleton(PHYSICS_SERVER_NAME);
}

Box2DPhysicsServer2D *Box2DPhysicsServer2D::get_singleton() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return Object::cast_to<Box2DPhysicsServer2D>(PhysicsServer2D::get_singleton());
}

// Shape API
RID Box2DPhysicsServer2D::world_boundary_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DWorldBoundaryShape2D *shape = memnew(Box2DWorldBoundaryShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::separation_ray_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSeparationRayShape2D *shape = memnew(Box2DSeparationRayShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::segment_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSegmentShape2D *shape = memnew(Box2DSegmentShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::circle_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DCircleShape2D *shape = memnew(Box2DCircleShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::rectangle_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DRectangleShape2D *shape = memnew(Box2DRectangleShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::capsule_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DCapsuleShape2D *shape = memnew(Box2DCapsuleShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::convex_polygon_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DConvexPolygonShape2D *shape = memnew(Box2DConvexPolygonShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box2DPhysicsServer2D::concave_polygon_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DConcavePolygonShape2D *shape = memnew(Box2DConcavePolygonShape2D);
	RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

void Box2DPhysicsServer2D::shape_set_data(RID p_shape, const Variant &p_data) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->set_data(p_data);
}

PS2DE::ShapeType Box2DPhysicsServer2D::shape_get_type(RID p_shape) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, PS2DE::ShapeType::SHAPE_INVALID);
	return shape->get_type();
}

Variant Box2DPhysicsServer2D::shape_get_data(RID p_shape) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, Variant());
	return shape->get_data();
}

bool Box2DPhysicsServer2D::_shape_collide_compat(
		RID p_shape_A,
		const Transform2D &p_xform_A,
		const Vector2 &p_motion_A,
		RID p_shape_B,
		const Transform2D &p_xform_B,
		const Vector2 &p_motion_B,
		void *p_results,
		int32_t p_result_max,
		int32_t *p_result_count) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_NULL_V(p_result_count, false);
	*p_result_count = 0;
	ERR_FAIL_COND_V(p_result_max < 0 || (p_result_max > 0 && !p_results), false);
	const auto *shape_a = shape_owner.get_or_null(p_shape_A);
	const auto *shape_b = shape_owner.get_or_null(p_shape_B);
	ERR_FAIL_NULL_V(shape_a, false);
	ERR_FAIL_NULL_V(shape_b, false);
	LocalVector<Box2DShapePrimitive> a, b;
	if (!make_query_primitives(shape_a, p_xform_A, a) || !make_query_primitives(shape_b, p_xform_B, b)) {
		return false;
	}
	auto *points = static_cast<Vector2 *>(p_results);
	bool collided = false;
	for (const auto &primitive_a : a) {
		for (const auto &primitive_b : b) {
			const auto collision = box2d_collide_shapes(primitive_a, b2Transform_identity, primitive_b, b2Transform_identity);
			for (int i = 0; i < collision.point_count; ++i) {
				if (collision.points[i].depth < 0) {
					continue;
				}
				collided = true;
				if (*p_result_count < p_result_max) {
					points[2 * *p_result_count] = collision.points[i].point - collision.normal * collision.points[i].depth;
					points[2 * *p_result_count + 1] = collision.points[i].point;
					++*p_result_count;
				}
			}
			if (collision.point_count || (p_motion_A == Vector2() && p_motion_B == Vector2())) {
				continue;
			}
			b2ShapeCastPairInput input = {};
			input.proxyA = primitive_a.get_proxy();
			input.proxyB = primitive_b.get_proxy();
			input.transform = b2Transform_identity;
			input.translationB = to_box2d(p_motion_B - p_motion_A);
			input.maxFraction = 1;
			const auto cast = b2ShapeCast(&input);
			if (cast.hit) {
				collided = true;
				if (*p_result_count < p_result_max) {
					const Vector2 point = to_godot(cast.point) + p_motion_A * cast.fraction;
					points[2 * *p_result_count] = point;
					points[2 * *p_result_count + 1] = point;
					++*p_result_count;
				}
			}
		}
	}
	return collided;
}

// Space API
RID Box2DPhysicsServer2D::space_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = memnew(Box2DSpace2D);
	RID rid = space_owner.make_rid(space);
	space->set_rid(rid);

	RID default_area_rid = area_create();
	Box2DArea2D *default_area = area_owner.get_or_null(default_area_rid);
	ERR_FAIL_NULL_V(default_area, RID());
	space->set_default_area(default_area);
	default_area->set_space(space);

	return rid;
}

void Box2DPhysicsServer2D::space_set_active(RID p_space, bool p_active) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);

	if (p_active) {
		active_spaces.insert(space);
	} else {
		active_spaces.erase(space);
	}
}

bool Box2DPhysicsServer2D::space_is_active(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, false);
	return active_spaces.has(space);
}

void Box2DPhysicsServer2D::space_set_param(RID p_space, PS2DE::SpaceParameter p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	space->set_param(p_param, p_value);
}

real_t Box2DPhysicsServer2D::space_get_param(RID p_space, PS2DE::SpaceParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0);
	return space->get_param(p_param);
}

PhysicsDirectSpaceState2D *Box2DPhysicsServer2D::space_get_direct_state(RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, {});
	return space->get_direct_state();
}

void Box2DPhysicsServer2D::space_set_debug_contacts(RID p_space, int p_max_contacts) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	return space->set_max_debug_contacts(p_max_contacts);
}

Vector<Vector2> Box2DPhysicsServer2D::space_get_contacts(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, PackedVector2Array());
	return space->get_debug_contacts();
}

int Box2DPhysicsServer2D::space_get_contact_count(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0);
	return space->get_debug_contact_count();
}

Array Box2DPhysicsServer2D::space_get_body_move_events(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	TracyZoneScoped("Box2DPhysicsServer2D::space_get_body_move_events");
	Box2DSpace2D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, {});
	ERR_FAIL_COND_V(space->is_locked(), {});

	b2BodyEvents body_events = b2World_GetBodyEvents(space->get_world_id());

	Array results;
	results.resize(body_events.moveCount * 2);
	int index = 0;

	for (int i = 0; i < body_events.moveCount; ++i) {
		const b2BodyMoveEvent *event = body_events.moveEvents + i;
		Box2DCollisionObject2D *object = static_cast<Box2DCollisionObject2D *>(event->userData);
		Box2DBody2D *body = object->as_body();
		if (!body) {
			continue;
		}
		results[index++] = body->get_user_data();
		results[index++] = body->get_transform();
	}

	return results;
}

// Area API
RID Box2DPhysicsServer2D::area_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = memnew(Box2DArea2D);
	RID rid = area_owner.make_rid(area);
	area->set_rid(rid);
	return rid;
}

void Box2DPhysicsServer2D::area_set_space(RID p_area, RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box2DSpace2D *space = nullptr;
	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}

	area->set_space(space);
}

RID Box2DPhysicsServer2D::area_get_space(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());

	Box2DSpace2D *space = area->get_space();
	if (!space) {
		return RID();
	}

	return space->get_rid();
}

void Box2DPhysicsServer2D::area_add_shape(RID p_area, RID p_shape, const Transform2D &p_transform, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	area->add_shape(shape, p_transform, p_disabled);
}

void Box2DPhysicsServer2D::area_set_shape(RID p_area, int p_shape_idx, RID p_shape) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	area->set_shape(p_shape_idx, shape);
}

void Box2DPhysicsServer2D::area_set_shape_transform(RID p_area, int p_shape_idx, const Transform2D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_transform(p_shape_idx, p_transform);
}

void Box2DPhysicsServer2D::area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_disabled(p_shape_idx, p_disabled);
}

int Box2DPhysicsServer2D::area_get_shape_count(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_shape_count();
}

RID Box2DPhysicsServer2D::area_get_shape(RID p_area, int p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());
	return area->get_shape_rid(p_shape_idx);
}

Transform2D Box2DPhysicsServer2D::area_get_shape_transform(RID p_area, int p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform2D());
	return area->get_shape_transform(p_shape_idx);
}

void Box2DPhysicsServer2D::area_remove_shape(RID p_area, int p_shape_idx) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->remove_shape(p_shape_idx);
}

void Box2DPhysicsServer2D::area_clear_shapes(RID p_area) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->clear_shapes();
}

void Box2DPhysicsServer2D::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_instance_id(ObjectID(p_id));
}

ObjectID Box2DPhysicsServer2D::area_get_object_instance_id(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, ObjectID());
	return area->get_canvas_instance_id();
}

void Box2DPhysicsServer2D::area_attach_canvas_instance_id(RID p_area, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_canvas_instance_id(ObjectID(p_id));
}

ObjectID Box2DPhysicsServer2D::area_get_canvas_instance_id(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, ObjectID());
	return area->get_canvas_instance_id();
}

void Box2DPhysicsServer2D::area_set_param(RID p_area, PS2DE::AreaParameter p_param, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		Box2DSpace2D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box2DArea2D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL(area);

	switch (p_param) {
		case AreaParameter::AREA_PARAM_GRAVITY_OVERRIDE_MODE:
			// TODO: add variant type checks
			area->set_gravity_override_mode((PS2DE::AreaSpaceOverrideMode)(int)p_value);
			break;
		case AreaParameter::AREA_PARAM_GRAVITY:
			area->set_gravity_strength(p_value);
			break;
		case AreaParameter::AREA_PARAM_GRAVITY_VECTOR:
			area->set_gravity_direction(p_value);
			break;
		case AreaParameter::AREA_PARAM_GRAVITY_IS_POINT:
			area->set_gravity_point_enabled(p_value);
			break;
		case AreaParameter::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE:
			area->set_gravity_point_unit_distance(p_value);
			break;
		case AreaParameter::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE:
			area->set_linear_damp_override_mode((PS2DE::AreaSpaceOverrideMode)(int)p_value);
			break;
		case AreaParameter::AREA_PARAM_LINEAR_DAMP:
			area->set_linear_damp(p_value);
			break;
		case AreaParameter::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE:
			area->set_angular_damp_override_mode((PS2DE::AreaSpaceOverrideMode)(int)p_value);
			break;
		case AreaParameter::AREA_PARAM_ANGULAR_DAMP:
			area->set_angular_damp(p_value);
			break;
		case AreaParameter::AREA_PARAM_PRIORITY:
			area->set_priority(p_value);
			break;
		default:
			break;
	}
}

void Box2DPhysicsServer2D::area_set_transform(RID p_area, const Transform2D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_transform(p_transform);
}

Variant Box2DPhysicsServer2D::area_get_param(RID p_area, PS2DE::AreaParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	RID area_rid = p_area;

	if (space_owner.owns(area_rid)) {
		Box2DSpace2D *space = space_owner.get_or_null(area_rid);
		area_rid = space->get_default_area()->get_rid();
	}

	Box2DArea2D *area = area_owner.get_or_null(area_rid);
	ERR_FAIL_NULL_V(area, Variant());

	switch (p_param) {
		case AreaParameter::AREA_PARAM_GRAVITY_OVERRIDE_MODE:
			return area->get_gravity_override_mode();
		case AreaParameter::AREA_PARAM_GRAVITY:
			return area->get_gravity_strength();
		case AreaParameter::AREA_PARAM_GRAVITY_VECTOR:
			return area->get_gravity_direction();
		case AreaParameter::AREA_PARAM_GRAVITY_IS_POINT:
			return area->get_gravity_point_enabled();
		case AreaParameter::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE:
			return area->get_gravity_point_unit_distance();
		case AreaParameter::AREA_PARAM_LINEAR_DAMP_OVERRIDE_MODE:
			return area->get_linear_damp_override_mode();
		case AreaParameter::AREA_PARAM_LINEAR_DAMP:
			return area->get_linear_damp();
		case AreaParameter::AREA_PARAM_ANGULAR_DAMP_OVERRIDE_MODE:
			return area->get_angular_damp_override_mode();
		case AreaParameter::AREA_PARAM_ANGULAR_DAMP:
			return area->get_angular_damp();
		case AreaParameter::AREA_PARAM_PRIORITY:
			return area->get_priority();
		default:
			ERR_FAIL_V(Variant());
	}
}

Transform2D Box2DPhysicsServer2D::area_get_transform(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform2D());
	return area->get_transform();
}

void Box2DPhysicsServer2D::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_collision_layer(p_layer);
}

uint32_t Box2DPhysicsServer2D::area_get_collision_layer(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_layer();
}

void Box2DPhysicsServer2D::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_collision_mask(p_mask);
}

uint32_t Box2DPhysicsServer2D::area_get_collision_mask(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_mask();
}

void Box2DPhysicsServer2D::area_set_monitorable(RID p_area, bool p_monitorable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	return area->set_monitorable(p_monitorable);
}

void Box2DPhysicsServer2D::area_set_pickable(RID p_area, bool p_pickable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_pickable(p_pickable);
}

void Box2DPhysicsServer2D::body_set_pickable(RID p_body, bool p_pickable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_pickable(p_pickable);
}

void Box2DPhysicsServer2D::area_set_monitor_callback(RID p_area, const Callable &p_callback) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_body_monitor_callback(p_callback);
}

void Box2DPhysicsServer2D::area_set_area_monitor_callback(RID p_area, const Callable &p_callback) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DArea2D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_area_monitor_callback(p_callback);
}

// Body API
RID Box2DPhysicsServer2D::body_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = memnew(Box2DBody2D);
	RID rid = body_owner.make_rid(body);
	body->set_rid(rid);
	return rid;
}

void Box2DPhysicsServer2D::body_set_space(RID p_body, RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box2DSpace2D *space = nullptr;
	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}

	body->set_space(space);
}

RID Box2DPhysicsServer2D::body_get_space(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	Box2DSpace2D *space = body->get_space();
	if (!space) {
		return RID();
	}

	return space->get_rid();
}

void Box2DPhysicsServer2D::body_set_mode(RID p_body, PS2DE::BodyMode p_mode) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_mode(p_mode);
}

PS2DE::BodyMode Box2DPhysicsServer2D::body_get_mode(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, BODY_MODE_STATIC);
	return body->get_mode();
}

void Box2DPhysicsServer2D::body_set_user_data(RID p_body, const Variant &p_variant) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_user_data(p_variant);
}

Variant Box2DPhysicsServer2D::body_get_user_data(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());
	return body->get_user_data();
}

void Box2DPhysicsServer2D::body_add_shape(RID p_body, RID p_shape, const Transform2D &p_transform, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	body->add_shape(shape, p_transform, p_disabled);
}

void Box2DPhysicsServer2D::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box2DShape2D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);

	body->set_shape(p_shape_idx, shape);
}

void Box2DPhysicsServer2D::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform2D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shape_transform(p_shape_idx, p_transform);
}

int Box2DPhysicsServer2D::body_get_shape_count(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_shape_count();
}

RID Box2DPhysicsServer2D::body_get_shape(RID p_body, int p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());
	return body->get_shape_rid(p_shape_idx);
}

Transform2D Box2DPhysicsServer2D::body_get_shape_transform(RID p_body, int p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Transform2D());
	return body->get_shape_transform(p_shape_idx);
}

void Box2DPhysicsServer2D::body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_shape_disabled(p_shape_idx, p_disabled);
}

void Box2DPhysicsServer2D::body_set_shape_as_one_way_collision(RID p_body, int32_t p_shape_idx, bool p_enable, real_t p_margin, const Vector2 &p_direction) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_shape_one_way_collision(p_shape_idx, p_enable, p_margin, p_direction);
}

void Box2DPhysicsServer2D::body_remove_shape(RID p_body, int p_shape_idx) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->remove_shape(p_shape_idx);
}

void Box2DPhysicsServer2D::body_clear_shapes(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->clear_shapes();
}

void Box2DPhysicsServer2D::body_attach_object_instance_id(RID p_body, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_instance_id(ObjectID(p_id));
}

ObjectID Box2DPhysicsServer2D::body_get_object_instance_id(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, ObjectID());
	return body->get_instance_id();
}

void Box2DPhysicsServer2D::body_attach_canvas_instance_id(RID p_body, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_canvas_instance_id(ObjectID(p_id));
}

ObjectID Box2DPhysicsServer2D::body_get_canvas_instance_id(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, ObjectID());
	return body->get_canvas_instance_id();
}

void Box2DPhysicsServer2D::body_set_continuous_collision_detection_mode(RID p_body, PS2DE::CCDMode p_mode) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND(p_mode != PS2DE::CCD_MODE_DISABLED && p_mode != PS2DE::CCD_MODE_CAST_SHAPE);

	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	body->set_bullet(p_mode == CCDMode::CCD_MODE_CAST_SHAPE);
}

PS2DE::CCDMode Box2DPhysicsServer2D::body_get_continuous_collision_detection_mode(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, CCDMode::CCD_MODE_DISABLED);
	return body->get_bullet() ? CCDMode::CCD_MODE_CAST_SHAPE : CCDMode::CCD_MODE_DISABLED;
}

void Box2DPhysicsServer2D::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_layer(p_layer);
}

uint32_t Box2DPhysicsServer2D::body_get_collision_layer(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_layer();
}

void Box2DPhysicsServer2D::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_mask(p_mask);
}

uint32_t Box2DPhysicsServer2D::body_get_collision_mask(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_mask();
}

void Box2DPhysicsServer2D::body_set_collision_priority(RID p_body, real_t p_priority) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_character_collision_priority(p_priority);
}

real_t Box2DPhysicsServer2D::body_get_collision_priority(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);
	return body->get_character_collision_priority();
}

void Box2DPhysicsServer2D::body_set_param(RID p_body, PS2DE::BodyParameter p_param, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	switch (p_param) {
		case BodyParameter::BODY_PARAM_BOUNCE:
			body->set_bounce(p_value);
			break;
		case BodyParameter::BODY_PARAM_FRICTION:
			body->set_friction(p_value);
			break;
		case BodyParameter::BODY_PARAM_MASS:
			body->set_mass(p_value);
			break;
		case BodyParameter::BODY_PARAM_INERTIA:
			body->set_inertia(p_value);
			break;
		case BodyParameter::BODY_PARAM_CENTER_OF_MASS:
			body->set_center_of_mass(p_value);
			break;
		case BodyParameter::BODY_PARAM_GRAVITY_SCALE:
			body->set_gravity_scale(p_value);
			break;
		case BodyParameter::BODY_PARAM_LINEAR_DAMP_MODE:
			body->set_linear_damp_mode((BodyDampMode)(int)p_value);
			break;
		case BodyParameter::BODY_PARAM_ANGULAR_DAMP_MODE:
			body->set_angular_damp_mode((BodyDampMode)(int)p_value);
			break;
		case BodyParameter::BODY_PARAM_LINEAR_DAMP:
			body->set_linear_damping(p_value);
			break;
		case BodyParameter::BODY_PARAM_ANGULAR_DAMP:
			body->set_angular_damping(p_value);
			break;
		case PS2DE::BODY_PARAM_SLEEP_THRESHOLD:
			ERR_FAIL_COND(p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT);
			body->set_sleep_threshold((real_t)p_value);
			break;
		case PS2DE::BODY_PARAM_HIT_EVENTS_ENABLED:
			ERR_FAIL_COND(p_value.get_type() != Variant::BOOL);
			body->set_hit_events_enabled((bool)p_value);
			break;
		case PS2DE::BODY_PARAM_CONTACT_REPORT_SPECULATIVE:
			ERR_FAIL_COND(p_value.get_type() != Variant::BOOL);
			body->set_contact_report_speculative((bool)p_value);
			break;
		default:
			break;
	}
}

Variant Box2DPhysicsServer2D::body_get_param(RID p_body, PS2DE::BodyParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	switch (p_param) {
		case BodyParameter::BODY_PARAM_BOUNCE:
			return body->get_bounce();
		case BodyParameter::BODY_PARAM_FRICTION:
			return body->get_friction();
		case BodyParameter::BODY_PARAM_MASS:
			return body->get_mass();
		case BodyParameter::BODY_PARAM_INERTIA:
			return body->get_inertia();
		case BodyParameter::BODY_PARAM_CENTER_OF_MASS:
			return body->get_center_of_mass();
		case BodyParameter::BODY_PARAM_GRAVITY_SCALE:
			return body->get_gravity_scale();
		case BodyParameter::BODY_PARAM_LINEAR_DAMP_MODE:
			return body->get_linear_damp_mode();
		case BodyParameter::BODY_PARAM_ANGULAR_DAMP_MODE:
			return body->get_angular_damp_mode();
		case BodyParameter::BODY_PARAM_LINEAR_DAMP:
			return body->get_linear_damping();
		case BodyParameter::BODY_PARAM_ANGULAR_DAMP:
			return body->get_angular_damping();
		case PS2DE::BODY_PARAM_SLEEP_THRESHOLD:
			return body->get_sleep_threshold();
		case PS2DE::BODY_PARAM_HIT_EVENTS_ENABLED:
			return body->get_hit_events_enabled();
		case PS2DE::BODY_PARAM_CONTACT_REPORT_SPECULATIVE:
			return body->get_contact_report_speculative();
		default:
			return Variant();
	}
}

void Box2DPhysicsServer2D::body_reset_mass_properties(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->reset_mass();
}

void Box2DPhysicsServer2D::body_set_state(RID p_body, PS2DE::BodyState p_state, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	switch (p_state) {
		case BodyState::BODY_STATE_TRANSFORM:
			body->set_transform(p_value);
			break;
		case BodyState::BODY_STATE_LINEAR_VELOCITY:
			body->set_linear_velocity(p_value);
			break;
		case BodyState::BODY_STATE_ANGULAR_VELOCITY:
			body->set_angular_velocity(p_value);
			break;
		case BodyState::BODY_STATE_SLEEPING:
			body->set_sleep_state(p_value);
			break;
		case BodyState::BODY_STATE_CAN_SLEEP:
			body->set_sleep_enabled(p_value);
			break;
		default:
			break;
	}
}

Variant Box2DPhysicsServer2D::body_get_state(RID p_body, PS2DE::BodyState p_state) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	switch (p_state) {
		case BodyState::BODY_STATE_TRANSFORM:
			return body->get_transform();
		case BodyState::BODY_STATE_LINEAR_VELOCITY:
			return body->get_linear_velocity();
		case BodyState::BODY_STATE_ANGULAR_VELOCITY:
			return body->get_angular_velocity();
		case BodyState::BODY_STATE_SLEEPING:
			return body->is_sleeping();
		case BodyState::BODY_STATE_CAN_SLEEP:
			return body->can_sleep();
		default:
			return Variant();
	}
}

void Box2DPhysicsServer2D::body_apply_central_impulse(RID p_body, const Vector2 &p_impulse) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_impulse_center(p_impulse);
}

void Box2DPhysicsServer2D::body_apply_torque_impulse(RID p_body, real_t p_impulse) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque_impulse(p_impulse);
}

void Box2DPhysicsServer2D::body_apply_impulse(RID p_body, const Vector2 &p_impulse, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_impulse(p_impulse, p_position);
}

void Box2DPhysicsServer2D::body_apply_central_force(RID p_body, const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central_force(p_force);
}

void Box2DPhysicsServer2D::body_apply_force(RID p_body, const Vector2 &p_force, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_force(p_force, p_position);
}

void Box2DPhysicsServer2D::body_apply_torque(RID p_body, real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque(p_torque);
}

void Box2DPhysicsServer2D::body_add_constant_central_force(RID p_body, const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_central_force(p_force);
}

void Box2DPhysicsServer2D::body_add_constant_force(RID p_body, const Vector2 &p_force, const Vector2 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_force(p_force, p_position);
}

void Box2DPhysicsServer2D::body_add_constant_torque(RID p_body, real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_torque(p_torque);
}

void Box2DPhysicsServer2D::body_set_constant_force(RID p_body, const Vector2 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_force(p_force);
}

Vector2 Box2DPhysicsServer2D::body_get_constant_force(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector2());
	return body->get_constant_force();
}

void Box2DPhysicsServer2D::body_set_constant_torque(RID p_body, real_t p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_torque(p_torque);
}

real_t Box2DPhysicsServer2D::body_get_constant_torque(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);
	return body->get_constant_torque();
}

void Box2DPhysicsServer2D::body_set_axis_velocity(RID p_body, const Vector2 &p_axis_velocity) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Vector2 axis = p_axis_velocity.normalized();
	Vector2 linear_velocity = body->get_linear_velocity();
	linear_velocity -= axis * axis.dot(linear_velocity);
	linear_velocity += p_axis_velocity;
	body->set_linear_velocity(linear_velocity);
}

void Box2DPhysicsServer2D::body_add_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_collision_exception(p_excepted_body);
}

void Box2DPhysicsServer2D::body_remove_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->remove_collision_exception(p_excepted_body);
}

TypedArray<RID> Box2DPhysicsServer2D::_body_get_collision_exceptions_compat(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, {});
	return body->get_collision_exceptions();
}

void Box2DPhysicsServer2D::body_set_max_contacts_reported(RID p_body, int p_amount) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_max_contacts_reported(p_amount);
}

int Box2DPhysicsServer2D::body_get_max_contacts_reported(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_max_contacts_reported();
}

void Box2DPhysicsServer2D::body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_COND(!std::isfinite(p_threshold) || p_threshold < 0);
	body->set_contact_depth_threshold(p_threshold);
}

real_t Box2DPhysicsServer2D::body_get_contacts_reported_depth_threshold(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0.0);
	return body->get_contact_depth_threshold();
}

void Box2DPhysicsServer2D::body_set_omit_force_integration(RID p_body, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_omit_force_integration(p_enable);
}

bool Box2DPhysicsServer2D::body_is_omitting_force_integration(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	const Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_omitting_force_integration();
}

void Box2DPhysicsServer2D::body_set_state_sync_callback(RID p_body, const Callable &p_callable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_state_sync_callback(p_callable);
}

void Box2DPhysicsServer2D::body_set_force_integration_callback(RID p_body, const Callable &p_callable, const Variant &p_userdata) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	return body->set_force_integration_callback(p_callable, p_userdata);
}

PhysicsDirectBodyState2D *Box2DPhysicsServer2D::body_get_direct_state(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, nullptr);
	return body->get_direct_state();
}

static thread_local LocalVector<CharacterCollideResult> character_collide_results;

bool Box2DPhysicsServer2D::_body_test_motion_compat(
		RID p_body,
		const Transform2D &p_from,
		const Vector2 &p_motion,
		real_t p_margin,
		bool p_collide_separation_ray,
		bool p_recovery_as_collision,
		PS2DT::MotionResult *p_result) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);

	Transform2D transform = p_from;

	Vector2 recovery = Vector2();

	p_margin = MAX(p_margin, 0.0001f);

	// 1) Recover from overlaps
	const int iterations = 8;
	const real_t recover_ratio = 0.5f;
	const real_t min_contact_depth = B2_LINEAR_SLOP;

	bool recovered = false;

	for (int i = 0; i < iterations; i++) {
		int count = body->character_collide(transform, p_margin, character_collide_results);

		if (count == 0) {
			break;
		}

		bool any_collided = false;

		for (CharacterCollideResult &collision : character_collide_results) {
			if (collision.depth < min_contact_depth) {
				continue;
			}

			Box2DShapeInstance *shape = collision.other_shape;
			if (shape->should_filter_one_way_collision(p_motion, collision.normal, collision.depth)) {
				continue;
			}

			any_collided = true;
			recovered = true;

			real_t recover_amount = collision.depth * recover_ratio;
			Vector2 recover_step = collision.normal * recover_amount;
			recovery += recover_step;
			transform.set_origin(transform.get_origin() + recover_step);
		}

		if (!any_collided) {
			break;
		}
	}

	// 2) Shape cast
	CharacterCastResult cast_result = body->character_cast(transform, p_margin, p_motion);

	if (!p_result) {
		return cast_result.hit || (recovered && p_recovery_as_collision);
	}

	// 3) Get rest info
	// Change from Godot Physics: use the collision information from the shape cast if it exists.
	if (cast_result.hit) {
		real_t safe_fraction = box2d_compute_safe_fraction(cast_result.unsafe_fraction, p_motion.length());
		Box2DCollisionObject2D *object = cast_result.other_shape->get_collision_object();

		p_result->travel = (p_motion * safe_fraction) + recovery;
		p_result->remainder = p_motion * (1 - safe_fraction);

		p_result->collision_depth = 0.0f;
		p_result->collision_point = cast_result.point;
		p_result->collision_normal = cast_result.normal;

		p_result->collision_safe_fraction = safe_fraction;
		p_result->collision_unsafe_fraction = cast_result.unsafe_fraction;
		p_result->collision_local_shape = cast_result.shape->get_index();

		p_result->collider_id = ObjectID(object->get_instance_id());
		p_result->collider = object->get_rid();
		p_result->collider_shape = cast_result.other_shape->get_index();

		Box2DBody2D *collider_body = object->as_body();
		if (collider_body) {
			p_result->collider_velocity = collider_body->get_velocity_at_point(cast_result.point);
		} else {
			p_result->collider_velocity = Vector2();
		}

		return true;
	} else {
		int deepest_index = -1;

		if (recovered && p_recovery_as_collision) {
			transform.set_origin(transform.get_origin() + p_motion);
			body->character_collide(transform, p_margin, character_collide_results);

			for (uint32_t i = 0; i < character_collide_results.size(); i++) {
				if (deepest_index == -1) {
					deepest_index = i;
					continue;
				}
				if (character_collide_results[i].depth > character_collide_results[deepest_index].depth) {
					deepest_index = i;
				}
			}
		}

		if (deepest_index < 0) {
			p_result->travel = p_motion + recovery;
			p_result->remainder = Vector2();
			p_result->collision_safe_fraction = 1.0f;
			p_result->collision_unsafe_fraction = 1.0f;
			p_result->collision_depth = 0.0f;
			return false;
		}

		CharacterCollideResult &rest_collision = character_collide_results[deepest_index];

		p_result->travel = p_motion + recovery;
		p_result->remainder = Vector2();

		p_result->collision_point = rest_collision.point;
		p_result->collision_normal = rest_collision.normal;
		p_result->collision_depth = rest_collision.depth;

		p_result->collision_safe_fraction = 1.0f;
		p_result->collision_unsafe_fraction = 1.0f;
		p_result->collision_local_shape = rest_collision.shape->get_index();

		Box2DCollisionObject2D *object = rest_collision.other_shape->get_collision_object();
		p_result->collider_id = ObjectID(object->get_instance_id());
		p_result->collider = object->get_rid();
		p_result->collider_shape = rest_collision.other_shape->get_index();

		Box2DBody2D *collider_body = object->as_body();
		if (collider_body) {
			p_result->collider_velocity = collider_body->get_velocity_at_point(rest_collision.point);
		} else {
			p_result->collider_velocity = Vector2();
		}

		return true;
	}

	return false;
}

// Joint API
RID Box2DPhysicsServer2D::joint_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = memnew(Box2DJoint2D);
	RID joint_rid = joint_owner.make_rid(joint);
	joint->set_rid(joint_rid);
	return joint_rid;
}

void Box2DPhysicsServer2D::joint_clear(RID p_joint) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);

	if (joint->get_type() != JOINT_TYPE_MAX) {
		Box2DJoint2D *empty_joint = memnew(Box2DJoint2D);
		empty_joint->set_rid(joint->get_rid());

		memdelete(joint);
		joint_owner.replace(p_joint, empty_joint);
	}
}

void Box2DPhysicsServer2D::joint_set_param(RID p_joint, PS2DE::JointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	if (joint->get_type() >= PS2DE::JOINT_TYPE_DISTANCE && joint->get_type() <= PS2DE::JOINT_TYPE_WHEEL) {
		ERR_FAIL_COND(p_param != PS2DE::JOINT_PARAM_HERTZ && p_param != PS2DE::JOINT_PARAM_DAMPING_RATIO);
		Dictionary configuration;
		configuration[p_param == PS2DE::JOINT_PARAM_HERTZ ? "constraint_hertz" : "constraint_damping_ratio"] = p_value;
		static_cast<Box2DConfiguredJoint2D *>(joint)->set_configuration(configuration);
		return;
	}

	switch (p_param) {
		case JointParam::JOINT_PARAM_HERTZ:
			joint->set_hertz(p_value);
			break;
		case JointParam::JOINT_PARAM_DAMPING_RATIO:
			joint->set_damping_ratio(p_value);
			break;
		default:
			break;
	}
}

real_t Box2DPhysicsServer2D::joint_get_param(RID p_joint, PS2DE::JointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);
	if (joint->get_type() >= PS2DE::JOINT_TYPE_DISTANCE && joint->get_type() <= PS2DE::JOINT_TYPE_WHEEL) {
		ERR_FAIL_COND_V(p_param != PS2DE::JOINT_PARAM_HERTZ && p_param != PS2DE::JOINT_PARAM_DAMPING_RATIO, 0);
		return static_cast<Box2DConfiguredJoint2D *>(joint)->get_configuration()[p_param == PS2DE::JOINT_PARAM_HERTZ ? "constraint_hertz" : "constraint_damping_ratio"];
	}

	switch (p_param) {
		case JointParam::JOINT_PARAM_HERTZ:
			return joint->get_hertz();
		case JointParam::JOINT_PARAM_DAMPING_RATIO:
			return joint->get_damping_ratio();
		default:
			ERR_FAIL_V(0.0);
	}
}

void Box2DPhysicsServer2D::joint_disable_collisions_between_bodies(RID p_joint, const bool p_disable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->disable_collisions_between_bodies(p_disable);
}

bool Box2DPhysicsServer2D::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return joint->is_disabled_collisions_between_bodies();
}

void Box2DPhysicsServer2D::joint_make_pin(RID p_joint, const Vector2 &p_anchor, RID p_body_a, RID p_body_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box2DBody2D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box2DBody2D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(p_body_b.is_valid() && !body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box2DJoint2D *new_joint = memnew(Box2DPinJoint2D(p_anchor, body_a, body_b));
	new_joint->copy_settings_from(old_joint);
	joint_owner.replace(p_joint, new_joint);

	memdelete(old_joint);
}

void Box2DPhysicsServer2D::joint_make_groove(RID p_joint, const Vector2 &p_a_groove1, const Vector2 &p_a_groove2, const Vector2 &p_b_anchor, RID p_body_a, RID p_body_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box2DBody2D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box2DBody2D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box2DJoint2D *new_joint = memnew(Box2DGrooveJoint2D(p_a_groove1, p_a_groove2, p_b_anchor, body_a, body_b));
	new_joint->copy_settings_from(old_joint);
	joint_owner.replace(p_joint, new_joint);

	memdelete(old_joint);
}

void Box2DPhysicsServer2D::joint_make_damped_spring(RID p_joint, const Vector2 &p_anchor_a, const Vector2 &p_anchor_b, RID p_body_a, RID p_body_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *old_joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(old_joint);

	Box2DBody2D *body_a = body_owner.get_or_null(p_body_a);
	ERR_FAIL_NULL(body_a);

	Box2DBody2D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(p_body_b.is_valid() && !body_b);
	ERR_FAIL_COND(body_a == body_b);

	Box2DJoint2D *new_joint = memnew(Box2DDampedSpringJoint2D(p_anchor_a, p_anchor_b, body_a, body_b));
	new_joint->copy_settings_from(old_joint);
	joint_owner.replace(p_joint, new_joint);

	memdelete(old_joint);
}

void Box2DPhysicsServer2D::pin_joint_set_flag(RID p_joint, PS2DE::PinJointFlag p_flag, bool p_enabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_PIN);

	Box2DPinJoint2D *pin_joint = static_cast<Box2DPinJoint2D *>(joint);
	switch (p_flag) {
		case PinJointFlag::PIN_JOINT_FLAG_ANGULAR_LIMIT_ENABLED:
			pin_joint->set_limit_enabled(p_enabled);
			break;
		case PinJointFlag::PIN_JOINT_FLAG_MOTOR_ENABLED:
			pin_joint->set_motor_enabled(p_enabled);
			break;
		default:
			break;
	}
}

bool Box2DPhysicsServer2D::pin_joint_get_flag(RID p_joint, PS2DE::PinJointFlag p_flag) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_PIN, false);

	Box2DPinJoint2D *pin_joint = static_cast<Box2DPinJoint2D *>(joint);
	switch (p_flag) {
		case PinJointFlag::PIN_JOINT_FLAG_ANGULAR_LIMIT_ENABLED:
			return pin_joint->get_limit_enabled();
		case PinJointFlag::PIN_JOINT_FLAG_MOTOR_ENABLED:
			return pin_joint->get_motor_enabled();
		default:
			ERR_FAIL_V(false);
	}
}

void Box2DPhysicsServer2D::pin_joint_set_param(RID p_joint, PS2DE::PinJointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_PIN);

	Box2DPinJoint2D *pin_joint = static_cast<Box2DPinJoint2D *>(joint);

	switch (p_param) {
		case PinJointParam::PIN_JOINT_LIMIT_UPPER:
			pin_joint->set_upper_limit(p_value);
			break;
		case PinJointParam::PIN_JOINT_LIMIT_LOWER:
			pin_joint->set_lower_limit(p_value);
			break;
		case PinJointParam::PIN_JOINT_MOTOR_TARGET_VELOCITY:
			pin_joint->set_motor_speed(p_value);
			break;
		default:
			break;
	}
}

real_t Box2DPhysicsServer2D::pin_joint_get_param(RID p_joint, PS2DE::PinJointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);
	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_PIN, 0.0);

	Box2DPinJoint2D *pin_joint = static_cast<Box2DPinJoint2D *>(joint);

	switch (p_param) {
		case PinJointParam::PIN_JOINT_LIMIT_UPPER:
			return pin_joint->get_upper_limit();
		case PinJointParam::PIN_JOINT_LIMIT_LOWER:
			return pin_joint->get_lower_limit();
		case PinJointParam::PIN_JOINT_MOTOR_TARGET_VELOCITY:
			return pin_joint->get_motor_speed();
		default:
			ERR_FAIL_V(0.0);
	}
}

void Box2DPhysicsServer2D::damped_spring_joint_set_param(RID p_joint, PS2DE::DampedSpringParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	ERR_FAIL_COND(joint->get_type() != JOINT_TYPE_DAMPED_SPRING);

	Box2DDampedSpringJoint2D *spring_joint = static_cast<Box2DDampedSpringJoint2D *>(joint);

	switch (p_param) {
		case DampedSpringParam::DAMPED_SPRING_REST_LENGTH:
			spring_joint->set_rest_length(p_value);
			break;
		case DampedSpringParam::DAMPED_SPRING_STIFFNESS:
			spring_joint->set_stiffness(p_value);
			break;
		case DampedSpringParam::DAMPED_SPRING_DAMPING:
			spring_joint->set_damping_ratio(p_value);
			break;
		default:
			break;
	}
}

real_t Box2DPhysicsServer2D::damped_spring_joint_get_param(RID p_joint, PS2DE::DampedSpringParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0.0);
	ERR_FAIL_COND_V(joint->get_type() != JOINT_TYPE_DAMPED_SPRING, 0.0);

	Box2DDampedSpringJoint2D *spring_joint = static_cast<Box2DDampedSpringJoint2D *>(joint);

	switch (p_param) {
		case DampedSpringParam::DAMPED_SPRING_REST_LENGTH:
			return spring_joint->get_rest_length();
		case DampedSpringParam::DAMPED_SPRING_STIFFNESS:
			return spring_joint->get_stiffness();
		case DampedSpringParam::DAMPED_SPRING_DAMPING:
			return spring_joint->get_damping_ratio();
		default:
			ERR_FAIL_V(0.0);
	}
}

PS2DE::JointType Box2DPhysicsServer2D::joint_get_type(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DJoint2D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, JointType::JOINT_TYPE_MAX);
	return joint->get_type();
}

void Box2DPhysicsServer2D::free_rid(RID p_rid) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	if (shape_owner.owns(p_rid)) {
		Box2DShape2D *shape = shape_owner.get_or_null(p_rid);
		ERR_FAIL_NULL(shape);
		shape_owner.free(p_rid);
		memdelete(shape);
	} else if (body_owner.owns(p_rid)) {
		Box2DBody2D *body = body_owner.get_or_null(p_rid);
		ERR_FAIL_NULL(body);
		for (RID rid : joint_owner.get_owned_list()) {
			joint_owner.get_or_null(rid)->forget_body(body);
		}
		body->free();
		body_owner.free(p_rid);
		bodies_to_delete.push_back(body);
		//memdelete(body);
	} else if (area_owner.owns(p_rid)) {
		Box2DArea2D *area = area_owner.get_or_null(p_rid);
		ERR_FAIL_NULL(area);
		area->free();
		area_owner.free(p_rid);
		//memdelete(area);
		areas_to_delete.push_back(area);
	} else if (space_owner.owns(p_rid)) {
		Box2DSpace2D *space = space_owner.get_or_null(p_rid);
		ERR_FAIL_NULL(space);
		ERR_FAIL_COND_MSG(flushing_queries, "Cannot free a physics space during callbacks.");
		for (auto *object : box2d_sorted(space->objects)) {
			if (object != space->get_default_area()) {
				object->set_space(nullptr);
			}
		}
		Box2DArea2D *default_area = space->get_default_area();
		if (default_area) {
			free_rid(default_area->get_rid());
			space->set_default_area(nullptr);
		}
		active_spaces.erase(space);
		space_owner.free(p_rid);
		memdelete(space);
	} else if (joint_owner.owns(p_rid)) {
		Box2DJoint2D *joint = joint_owner.get_or_null(p_rid);
		joint_owner.free(p_rid);
		memdelete(joint);
	} else {
		ERR_FAIL_MSG("Attempted to free invalid RID.");
	}
}

void Box2DPhysicsServer2D::set_active(bool p_active) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	active = p_active;
}

void Box2DPhysicsServer2D::init() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	// Runs before the first world, body or shape def is built, which the length unit requires.
	box2d_set_pixels_per_meter(Box2DProjectSettings::get_pixels_per_meter());
}

void Box2DPhysicsServer2D::step(real_t p_step) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND_MSG(Engine::get_singleton()->get_physics_ticks_per_second() != fixed_tick_rate, "Box2D tick rate cannot change while running.");
	const real_t fixed_step = 1.0f / fixed_tick_rate;
	ERR_FAIL_COND_MSG(!Math::is_finite(p_step) || Math::abs(p_step - fixed_step) > fixed_step * 0.00001f, "Box2D requires its fixed physics timestep.");
	ERR_FAIL_COND_MSG(std::fegetround() != FE_TONEAREST, "Box2D requires round-to-nearest floating point.");
	ERR_FAIL_COND_MSG(b2GetLengthUnitsPerMeter() != Box2DProjectSettings::get_pixels_per_meter(), "Box2D length units changed after initialization.");
	p_step = fixed_step;

	TracyZoneScoped("Step");

	// Exception joints cannot be created while a world is locked, so changes made since the last
	// step land here, in time to take effect this step.
	for (Box2DSpace2D *active_space : box2d_sorted(active_spaces)) {
		active_space->rebuild_exception_joints(this);
	}

	if (active) {
		for (Box2DSpace2D *active_space : box2d_sorted(active_spaces)) {
			active_space->step(p_step);
		}
	}

	for (Box2DArea2D *p_area : areas_to_delete) {
		memdelete(p_area);
	}
	areas_to_delete.clear();

	for (Box2DBody2D *p_body : bodies_to_delete) {
		memdelete(p_body);
	}
	bodies_to_delete.clear();

#ifdef TRACY_ENABLE
	FrameMark;
#endif
}

void Box2DPhysicsServer2D::flush_queries() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	TracyZoneScoped("Flush Queries");

	if (!active) {
		return;
	}

	flushing_queries = true;

	for (Box2DSpace2D *space : box2d_sorted(active_spaces)) {
		space->sync_state();
	}

	flushing_queries = false;
}

void Box2DPhysicsServer2D::finish() {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	for (RID rid : joint_owner.get_owned_list()) {
		free_rid(rid);
	}
	for (RID rid : body_owner.get_owned_list()) {
		free_rid(rid);
	}
	for (RID rid : space_owner.get_owned_list()) {
		free_rid(rid);
	}
	for (RID rid : area_owner.get_owned_list()) {
		free_rid(rid);
	}
	for (RID rid : shape_owner.get_owned_list()) {
		free_rid(rid);
	}
	for (auto *area : areas_to_delete) {
		memdelete(area);
	}
	areas_to_delete.clear();
	for (auto *body : bodies_to_delete) {
		memdelete(body);
	}
	bodies_to_delete.clear();
}

bool Box2DPhysicsServer2D::is_flushing_queries() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return flushing_queries;
}

int Box2DPhysicsServer2D::get_process_info(PS2DE::ProcessInfo p_process_info) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	int total = 0;
	for (auto *space : box2d_sorted(active_spaces)) {
		const auto counters = b2World_GetCounters(space->get_world_id());
		switch (p_process_info) {
			case INFO_ACTIVE_OBJECTS:
				total += b2World_GetAwakeBodyCount(space->get_world_id());
				break;
			case INFO_COLLISION_PAIRS:
				total += counters.contactCount;
				break;
			case INFO_ISLAND_COUNT:
				total += counters.islandCount;
				break;
			default:
				return 0;
		}
	}
	return total;
}

void Box2DPhysicsServer2D::joint_make_configured(RID p_joint, PS2DE::JointType p_type, RID p_body_a, const Transform2D &p_frame_a, RID p_body_b, const Transform2D &p_frame_b, const Dictionary &p_configuration) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	ERR_FAIL_COND(!joint_owner.owns(p_joint));
	auto *a = body_owner.get_or_null(p_body_a);
	auto *b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(!a || (p_body_b.is_valid() && !b) || a == b);
	ERR_FAIL_COND(!p_frame_a.is_finite() || !p_frame_b.is_finite());
	if (!Box2DConfiguredJoint2D::validate_configuration(p_type, p_configuration)) {
		return;
	}
	ERR_FAIL_COND(!a->in_space() || (b && a->get_space() != b->get_space()));
	auto *joint = memnew(Box2DConfiguredJoint2D(p_type, a, b, p_frame_a, p_frame_b));
	joint->set_rid(p_joint);
	joint->set_configuration(p_configuration);
	memdelete(joint_owner.get_or_null(p_joint));
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}
void Box2DPhysicsServer2D::joint_set_configuration(RID p_joint, const Dictionary &p_configuration) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND(!base || base->get_type() < PS2DE::JOINT_TYPE_DISTANCE || base->get_type() > PS2DE::JOINT_TYPE_WHEEL);
	static_cast<Box2DConfiguredJoint2D *>(base)->set_configuration(p_configuration);
}
Dictionary Box2DPhysicsServer2D::joint_get_configuration(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND_V(!base || base->get_type() < PS2DE::JOINT_TYPE_DISTANCE || base->get_type() > PS2DE::JOINT_TYPE_WHEEL, Dictionary());
	return static_cast<Box2DConfiguredJoint2D *>(base)->get_configuration();
}

void Box2DPhysicsServer2D::space_apply_explosion(RID p_space, const Vector2 &p_position, real_t p_radius, real_t p_falloff, real_t p_impulse_density, uint32_t p_collision_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	ERR_FAIL_COND(!p_position.is_finite() || !Math::is_finite(p_radius) || !Math::is_finite(p_falloff) || !Math::is_finite(p_impulse_density) || p_radius <= 0 || p_falloff < 0);
	auto def = b2DefaultExplosionDef();
	def.position = to_box2d(p_position);
	def.radius = p_radius;
	def.falloff = p_falloff;
	def.impulsePerLength = p_impulse_density;
	def.maskBits = p_collision_mask;
	b2World_Explode(space->get_world_id(), &def);
}
Array Box2DPhysicsServer2D::space_get_contact_hit_events(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, Array());
	return space->get_contact_hit_events().duplicate(true);
}
Array Box2DPhysicsServer2D::space_get_joint_events(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, Array());
	return space->get_joint_events().duplicate(true);
}
Vector2 Box2DPhysicsServer2D::joint_get_constraint_force(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector2());
	if (!b2Joint_IsValid(joint->get_joint_id())) {
		return Vector2();
	}
	return to_godot(b2Joint_GetConstraintForce(joint->get_joint_id()));
}
real_t Box2DPhysicsServer2D::joint_get_constraint_torque(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	auto *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, real_t());
	if (!b2Joint_IsValid(joint->get_joint_id())) {
		return real_t();
	}
	return b2Joint_GetConstraintTorque(joint->get_joint_id());
}
