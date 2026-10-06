#include "../joints/box3d_configured_joint_3d.hpp"
// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "../joints/box3d_cone_twist_joint_impl_3d.hpp"
#include "../joints/box3d_filter_joint_impl_3d.hpp"
#include "../joints/box3d_generic_joint_impl_3d.hpp"
#include "../joints/box3d_hinge_joint_impl_3d.hpp"
#include "../joints/box3d_joint_impl_3d.hpp"
#include "../joints/box3d_pin_joint_impl_3d.hpp"
#include "../joints/box3d_slider_joint_impl_3d.hpp"
#include "../misc/type_conversions.hpp"
#include "../objects/box3d_area_impl_3d.hpp"
#include "../objects/box3d_body_impl_3d.hpp"
#include "../objects/box3d_physics_direct_body_state_3d.hpp"
#include "../objects/box3d_shaped_object_impl_3d.hpp"
#include "../objects/box3d_soft_body_impl_3d.hpp"
#include "../shapes/box3d_box_shape_impl_3d.hpp"
#include "../shapes/box3d_capsule_shape_impl_3d.hpp"
#include "../shapes/box3d_concave_polygon_shape_impl_3d.hpp"
#include "../shapes/box3d_convex_polygon_shape_impl_3d.hpp"
#include "../shapes/box3d_cylinder_shape_impl_3d.hpp"
#include "../shapes/box3d_heightmap_shape_impl_3d.hpp"
#include "../shapes/box3d_shape_impl_3d.hpp"
#include "../shapes/box3d_sphere_shape_impl_3d.hpp"
#include "../shapes/box3d_world_boundary_shape_impl_3d.hpp"
#include "../spaces/box3d_physics_direct_space_state_3d.hpp"
#include "../spaces/box3d_space_3d.hpp"
#include "box3d_physics_server_3d.hpp"
#include "precompiled.hpp"

#include <box3d/box3d.h>

Box3DPhysicsServer3D *Box3DPhysicsServer3D::singleton = nullptr;

Box3DPhysicsServer3D::Box3DPhysicsServer3D() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	singleton = this;
	fixed_tick_rate = Engine::get_singleton()->get_physics_ticks_per_second();
}

Box3DPhysicsServer3D::~Box3DPhysicsServer3D() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (singleton == this) {
		singleton = nullptr;
	}
}

Box3DShapedObjectImpl3D *Box3DPhysicsServer3D::_get_shaped_object(RID p_rid) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (Box3DBodyImpl3D *body = body_owner.get_or_null(p_rid)) {
		return body;
	}
	if (Box3DAreaImpl3D *area = area_owner.get_or_null(p_rid)) {
		return area;
	}
	return nullptr;
}

void Box3DPhysicsServer3D::_clear_collision_exceptions(Box3DBodyImpl3D *p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	for (const KeyValue<RID, Box3DFilterJointImpl3D *> &entry : p_body->get_collision_exceptions()) {
		memdelete(entry.value);
	}
	p_body->get_collision_exceptions().clear();

	// Exceptions are stored one-sided, so other bodies may still reference this one.
	const RID rid = p_body->get_rid();
	for (Box3DBodyImpl3D *other : box3d_sorted(bodies_with_exceptions)) {
		if (other == p_body) {
			continue;
		}
		HashMap<RID, Box3DFilterJointImpl3D *>::Iterator entry = other->get_collision_exceptions().find(rid);
		if (entry) {
			memdelete(entry->value);
			other->get_collision_exceptions().remove(entry);
		}
	}
	bodies_with_exceptions.erase(p_body);
}

RID Box3DPhysicsServer3D::_resolve_area_rid(RID p_rid) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	const Box3DSpace3D *space = space_owner.get_or_null(p_rid);
	if (space == nullptr) {
		return p_rid;
	}
	const Box3DAreaImpl3D *default_area = space->get_default_area();
	ERR_FAIL_NULL_V(default_area, p_rid);
	return default_area->get_rid();
}

// --- Shapes ---

RID Box3DPhysicsServer3D::world_boundary_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DWorldBoundaryShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::sphere_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DSphereShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::box_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DBoxShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::capsule_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DCapsuleShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::cylinder_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DCylinderShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::convex_polygon_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DConvexPolygonShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::concave_polygon_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DConcavePolygonShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

RID Box3DPhysicsServer3D::heightmap_shape_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *shape = memnew(Box3DHeightMapShapeImpl3D);
	const RID rid = shape_owner.make_rid(shape);
	shape->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::shape_set_data(RID p_shape, const Variant &p_data) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->set_data(p_data);
	for (auto *owner : box3d_sorted(shape->get_owners())) {
		owner->refresh_shape(shape);
	}
}

PS3DE::ShapeType Box3DPhysicsServer3D::shape_get_type(RID p_shape) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, PS3DE::SHAPE_INVALID);
	return shape->get_type();
}

Variant Box3DPhysicsServer3D::shape_get_data(RID p_shape) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, Variant());
	return shape->get_data();
}

// --- Space ---

RID Box3DPhysicsServer3D::space_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = memnew(Box3DSpace3D);
	const RID rid = space_owner.make_rid(space);
	space->set_rid(rid);

	const RID default_area_rid = area_create();
	Box3DAreaImpl3D *default_area = area_owner.get_or_null(default_area_rid);
	ERR_FAIL_NULL_V(default_area, rid);
	default_area->set_default_area(true);
	default_area->set_space(space);
	space->set_default_area(default_area);

	return rid;
}

void Box3DPhysicsServer3D::space_set_active(RID p_space, bool p_active) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	space->set_active(p_active);
	if (p_active) {
		active_spaces.insert(space);
	} else {
		active_spaces.erase(space);
	}
}

bool Box3DPhysicsServer3D::space_is_active(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, false);
	return space->is_active();
}

void Box3DPhysicsServer3D::space_set_param(RID p_space, PS3DE::SpaceParameter p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	space->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::space_get_param(RID p_space, PS3DE::SpaceParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0.0);
	return space->get_param(p_param);
}

PhysicsDirectSpaceState3D *Box3DPhysicsServer3D::space_get_direct_state(RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, nullptr);
	return space->get_direct_state();
}

void Box3DPhysicsServer3D::space_set_debug_contacts(RID p_space, int32_t p_max_contacts) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	space->set_max_debug_contacts(p_max_contacts);
}

Vector<Vector3> Box3DPhysicsServer3D::space_get_contacts(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, Vector<Vector3>());
	return space->get_debug_contacts();
}

int32_t Box3DPhysicsServer3D::space_get_contact_count(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0);
	return space->get_debug_contact_count();
}

// --- Areas ---

RID Box3DPhysicsServer3D::area_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *area = memnew(Box3DAreaImpl3D);
	const RID rid = area_owner.make_rid(area);
	area->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::area_set_space(RID p_area, RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	Box3DSpace3D *old_space = area->get_space();
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_COND_MSG(p_space.is_valid() && !space, "Invalid Box3D space RID.");
	if (old_space == space) {
		return;
	}
	if (old_space != nullptr) {
		old_space->forget_object(area);
		old_space->unregister_area(area);
	}
	area->set_space(space);
	if (space != nullptr) {
		space->register_area(area);
	}
}

RID Box3DPhysicsServer3D::area_get_space(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());
	Box3DSpace3D *space = area->get_space();
	return space != nullptr ? space->get_rid() : RID();
}

void Box3DPhysicsServer3D::area_add_shape(RID p_area, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->add_owner(area);
	area->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::area_set_shape(RID p_area, int32_t p_shape_idx, RID p_shape) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->add_owner(area);
	area->set_shape(p_shape_idx, shape);
}

void Box3DPhysicsServer3D::area_set_shape_transform(RID p_area, int32_t p_shape_idx, const Transform3D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_transform(p_shape_idx, p_transform);
}

void Box3DPhysicsServer3D::area_set_shape_disabled(RID p_area, int32_t p_shape_idx, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_disabled(p_shape_idx, p_disabled);
}

int32_t Box3DPhysicsServer3D::area_get_shape_count(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_shape_count();
}

RID Box3DPhysicsServer3D::area_get_shape(RID p_area, int32_t p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());
	Box3DShapeImpl3D *shape = area->get_shape(p_shape_idx);
	return shape != nullptr ? shape->get_rid() : RID();
}

Transform3D Box3DPhysicsServer3D::area_get_shape_transform(RID p_area, int32_t p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform3D());
	return area->get_shape_transform(p_shape_idx);
}

void Box3DPhysicsServer3D::area_remove_shape(RID p_area, int32_t p_shape_idx) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::area_clear_shapes(RID p_area) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->clear_shapes();
}

void Box3DPhysicsServer3D::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_instance_id(p_id);
}

ObjectID Box3DPhysicsServer3D::area_get_object_instance_id(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL_V(area, ObjectID());
	return ObjectID(area->get_instance_id());
}

void Box3DPhysicsServer3D::area_set_param(RID p_area, PS3DE::AreaParameter p_param, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_param(p_param, p_value);
}

void Box3DPhysicsServer3D::area_set_transform(RID p_area, const Transform3D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_transform(p_transform);
}

Variant Box3DPhysicsServer3D::area_get_param(RID p_area, PS3DE::AreaParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL_V(area, Variant());
	return area->get_param(p_param);
}

Transform3D Box3DPhysicsServer3D::area_get_transform(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL_V(area, Transform3D());
	return area->get_transform();
}

void Box3DPhysicsServer3D::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_layer(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_layer();
}

void Box3DPhysicsServer3D::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_mask(RID p_area) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_mask();
}

void Box3DPhysicsServer3D::area_set_monitorable(RID p_area, bool p_monitorable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_monitorable(p_monitorable);
}

void Box3DPhysicsServer3D::area_set_ray_pickable(RID p_area, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *area = get_area(p_area);
	ERR_FAIL_NULL(area);
	area->set_ray_pickable(p_enable);
}

void Box3DPhysicsServer3D::area_set_monitor_callback(RID p_area, const Callable &p_callback) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_body_monitor_callback(p_callback);
}

void Box3DPhysicsServer3D::area_set_area_monitor_callback(RID p_area, const Callable &p_callback) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DAreaImpl3D *area = area_owner.get_or_null(_resolve_area_rid(p_area));
	ERR_FAIL_NULL(area);
	area->set_area_monitor_callback(p_callback);
}

// --- Bodies ---

RID Box3DPhysicsServer3D::body_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = memnew(Box3DBodyImpl3D);
	const RID rid = body_owner.make_rid(body);
	body->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::body_set_space(RID p_body, RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Box3DSpace3D *old_space = body->get_space();
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_COND_MSG(p_space.is_valid() && !space, "Invalid Box3D space RID.");
	if (old_space == space) {
		return;
	}
	if (old_space != nullptr) {
		old_space->forget_object(body);
		old_space->unregister_body(body);
	}
	body->set_space(space);
	if (space != nullptr) {
		space->register_body(body);
	}
	for (RID rid : joint_owner.get_owned_list()) {
		auto *joint = joint_owner.get_or_null(rid);
		if (joint && (joint->get_body_a() == body || joint->get_body_b() == body)) {
			joint->rebuild();
		}
	}
	for (auto *other : box3d_sorted(bodies_with_exceptions)) {
		for (const auto &entry : other->get_collision_exceptions()) {
			if (entry.value->get_body_a() == body || entry.value->get_body_b() == body) {
				entry.value->rebuild();
			}
		}
	}
}

RID Box3DPhysicsServer3D::body_get_space(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());
	Box3DSpace3D *space = body->get_space();
	return space != nullptr ? space->get_rid() : RID();
}

void Box3DPhysicsServer3D::body_set_mode(RID p_body, PS3DE::BodyMode p_mode) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_mode(p_mode);
}

PS3DE::BodyMode Box3DPhysicsServer3D::body_get_mode(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, PS3DE::BODY_MODE_STATIC);
	return body->get_mode();
}

void Box3DPhysicsServer3D::body_add_shape(RID p_body, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->add_owner(body);
	body->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::body_set_shape(RID p_body, int32_t p_shape_idx, RID p_shape) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->add_owner(body);
	body->set_shape(p_shape_idx, shape);
}

void Box3DPhysicsServer3D::body_set_shape_transform(RID p_body, int32_t p_shape_idx, const Transform3D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shape_transform(p_shape_idx, p_transform);
}

void Box3DPhysicsServer3D::body_set_shape_disabled(RID p_body, int32_t p_shape_idx, bool p_disabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shape_disabled(p_shape_idx, p_disabled);
}

int32_t Box3DPhysicsServer3D::body_get_shape_count(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_shape_count();
}

RID Box3DPhysicsServer3D::body_get_shape(RID p_body, int32_t p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());
	Box3DShapeImpl3D *shape = body->get_shape(p_shape_idx);
	return shape != nullptr ? shape->get_rid() : RID();
}

Transform3D Box3DPhysicsServer3D::body_get_shape_transform(RID p_body, int32_t p_shape_idx) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Transform3D());
	return body->get_shape_transform(p_shape_idx);
}

void Box3DPhysicsServer3D::body_remove_shape(RID p_body, int32_t p_shape_idx) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::body_clear_shapes(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->clear_shapes();
}

void Box3DPhysicsServer3D::body_attach_object_instance_id(RID p_body, ObjectID p_id) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (auto *soft = soft_body_owner.get_or_null(p_body)) {
		soft->set_instance(p_id);
		return;
	}
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_instance_id(p_id);
}

ObjectID Box3DPhysicsServer3D::body_get_object_instance_id(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (auto *soft = soft_body_owner.get_or_null(p_body)) {
		return ObjectID(soft->get_instance_id());
	}
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, ObjectID());
	return ObjectID(body->get_instance_id());
}

void Box3DPhysicsServer3D::body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_ccd_enabled(p_enable);
}

bool Box3DPhysicsServer3D::body_is_continuous_collision_detection_enabled(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_ccd_enabled();
}

void Box3DPhysicsServer3D::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_layer(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_layer();
}

void Box3DPhysicsServer3D::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_mask(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_mask();
}

void Box3DPhysicsServer3D::body_set_user_flags(RID p_body, uint32_t p_flags) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	// Not used by Box3D.
}

uint32_t Box3DPhysicsServer3D::body_get_user_flags(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	return 0;
}

void Box3DPhysicsServer3D::body_set_param(RID p_body, PS3DE::BodyParameter p_param, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	switch (p_param) {
		case PS3DE::BODY_PARAM_BOUNCE:
			body->set_bounce(p_value);
			break;
		case PS3DE::BODY_PARAM_FRICTION:
			body->set_friction(p_value);
			break;
		case PS3DE::BODY_PARAM_MASS:
			body->set_mass(p_value);
			break;
		case PS3DE::BODY_PARAM_INERTIA:
			body->set_inertia(p_value);
			break;
		case PS3DE::BODY_PARAM_CENTER_OF_MASS:
			body->set_center_of_mass(p_value);
			break;
		case PS3DE::BODY_PARAM_GRAVITY_SCALE:
			body->set_gravity_scale(p_value);
			break;
		case PS3DE::BODY_PARAM_LINEAR_DAMP_MODE:
			ERR_FAIL_COND(int(p_value) < PS3DE::BODY_DAMP_MODE_COMBINE || int(p_value) > PS3DE::BODY_DAMP_MODE_REPLACE);
			body->set_linear_damp_mode(PS3DE::BodyDampMode(int(p_value)));
			break;
		case PS3DE::BODY_PARAM_ANGULAR_DAMP_MODE:
			ERR_FAIL_COND(int(p_value) < PS3DE::BODY_DAMP_MODE_COMBINE || int(p_value) > PS3DE::BODY_DAMP_MODE_REPLACE);
			body->set_angular_damp_mode(PS3DE::BodyDampMode(int(p_value)));
			break;
		case PS3DE::BODY_PARAM_LINEAR_DAMP:
			body->set_linear_damping(p_value);
			break;
		case PS3DE::BODY_PARAM_ANGULAR_DAMP:
			body->set_angular_damping(p_value);
			break;
		case PS3DE::BODY_PARAM_SLEEP_THRESHOLD:
			ERR_FAIL_COND(p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT);
			body->set_sleep_threshold((real_t)p_value);
			break;
		case PS3DE::BODY_PARAM_HIT_EVENTS_ENABLED:
			ERR_FAIL_COND(p_value.get_type() != Variant::BOOL);
			body->set_hit_events_enabled((bool)p_value);
			break;
		case PS3DE::BODY_PARAM_CONTACT_REPORT_SPECULATIVE:
			ERR_FAIL_COND(p_value.get_type() != Variant::BOOL);
			body->set_contact_report_speculative((bool)p_value);
			break;
		case PS3DE::BODY_PARAM_CCD_SAFETY_FACTOR:
			ERR_FAIL_COND(p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT);
			body->set_ccd_safety_factor((real_t)p_value);
			break;
		case PS3DE::BODY_PARAM_CONTACT_RECYCLING_ENABLED:
			ERR_FAIL_COND(p_value.get_type() != Variant::BOOL);
			body->set_contact_recycling_enabled((bool)p_value);
			break;
		default:
			break;
	}
}

Variant Box3DPhysicsServer3D::body_get_param(RID p_body, PS3DE::BodyParameter p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());
	switch (p_param) {
		case PS3DE::BODY_PARAM_BOUNCE:
			return body->get_bounce();
		case PS3DE::BODY_PARAM_FRICTION:
			return body->get_friction();
		case PS3DE::BODY_PARAM_MASS:
			return body->get_mass();
		case PS3DE::BODY_PARAM_INERTIA:
			return body->get_inertia();
		case PS3DE::BODY_PARAM_CENTER_OF_MASS:
			return body->get_center_of_mass();
		case PS3DE::BODY_PARAM_GRAVITY_SCALE:
			return body->get_gravity_scale();
		case PS3DE::BODY_PARAM_LINEAR_DAMP_MODE:
			return body->get_linear_damp_mode();
		case PS3DE::BODY_PARAM_ANGULAR_DAMP_MODE:
			return body->get_angular_damp_mode();
		case PS3DE::BODY_PARAM_LINEAR_DAMP:
			return body->get_linear_damping();
		case PS3DE::BODY_PARAM_ANGULAR_DAMP:
			return body->get_angular_damping();
		case PS3DE::BODY_PARAM_SLEEP_THRESHOLD:
			return body->get_sleep_threshold();
		case PS3DE::BODY_PARAM_HIT_EVENTS_ENABLED:
			return body->get_hit_events_enabled();
		case PS3DE::BODY_PARAM_CONTACT_REPORT_SPECULATIVE:
			return body->get_contact_report_speculative();
		case PS3DE::BODY_PARAM_CCD_SAFETY_FACTOR:
			return body->get_ccd_safety_factor();
		case PS3DE::BODY_PARAM_CONTACT_RECYCLING_ENABLED:
			return body->get_contact_recycling_enabled();
		default:
			return Variant();
	}
}

void Box3DPhysicsServer3D::body_reset_mass_properties(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_mass_from_shapes();
}

void Box3DPhysicsServer3D::body_set_state(RID p_body, PS3DE::BodyState p_state, const Variant &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	switch (p_state) {
		case PS3DE::BODY_STATE_TRANSFORM:
			body->set_transform(p_value);
			break;
		case PS3DE::BODY_STATE_LINEAR_VELOCITY:
			body->set_linear_velocity(p_value);
			break;
		case PS3DE::BODY_STATE_ANGULAR_VELOCITY:
			body->set_angular_velocity(p_value);
			break;
		case PS3DE::BODY_STATE_SLEEPING:
			body->set_sleeping(p_value);
			break;
		case PS3DE::BODY_STATE_CAN_SLEEP:
			body->set_sleep_enabled(p_value);
			break;
		default:
			break;
	}
}

Variant Box3DPhysicsServer3D::body_get_state(RID p_body, PS3DE::BodyState p_state) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());
	switch (p_state) {
		case PS3DE::BODY_STATE_TRANSFORM:
			return body->get_transform();
		case PS3DE::BODY_STATE_LINEAR_VELOCITY:
			return body->get_linear_velocity();
		case PS3DE::BODY_STATE_ANGULAR_VELOCITY:
			return body->get_angular_velocity();
		case PS3DE::BODY_STATE_SLEEPING:
			return body->is_sleeping();
		case PS3DE::BODY_STATE_CAN_SLEEP:
			return body->is_sleep_enabled();
		default:
			return Variant();
	}
}

void Box3DPhysicsServer3D::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_impulse(p_impulse, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_central_force(RID p_body, const Vector3 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central_force(p_force);
}

void Box3DPhysicsServer3D::body_apply_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_force(p_force, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque(RID p_body, const Vector3 &p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque(p_torque);
}

void Box3DPhysicsServer3D::body_add_constant_central_force(RID p_body, const Vector3 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_central_force(p_force);
}

void Box3DPhysicsServer3D::body_add_constant_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_force(p_force, p_position);
}

void Box3DPhysicsServer3D::body_add_constant_torque(RID p_body, const Vector3 &p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_constant_torque(p_torque);
}

void Box3DPhysicsServer3D::body_set_constant_force(RID p_body, const Vector3 &p_force) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_force(p_force);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_force(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_force();
}

void Box3DPhysicsServer3D::body_set_constant_torque(RID p_body, const Vector3 &p_torque) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_torque(p_torque);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_torque(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_torque();
}

void Box3DPhysicsServer3D::body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	const Vector3 axis = p_axis_velocity.normalized();
	Vector3 velocity = body->get_linear_velocity();
	velocity -= axis * axis.dot(velocity);
	velocity += p_axis_velocity;
	body->set_linear_velocity(velocity);
}

void Box3DPhysicsServer3D::body_set_axis_lock(RID p_body, PS3DE::BodyAxis p_axis, bool p_lock) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_axis_lock(p_axis, p_lock);
}

bool Box3DPhysicsServer3D::body_is_axis_locked(RID p_body, PS3DE::BodyAxis p_axis) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->get_axis_lock(p_axis);
}

void Box3DPhysicsServer3D::body_add_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (soft_body_owner.owns(p_body)) {
		soft_body_add_collision_exception(p_body, p_excepted_body);
		return;
	}
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	if (soft_body_owner.owns(p_excepted_body)) {
		ERR_FAIL_NULL(body);
		body->get_soft_exceptions().insert(p_excepted_body);
		body->rebuild_shapes();
		return;
	}
	Box3DBodyImpl3D *excepted = body_owner.get_or_null(p_excepted_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_NULL(excepted);
	ERR_FAIL_COND(body == excepted);
	if (body->get_collision_exceptions().has(p_excepted_body)) {
		return;
	}

	auto *joint = memnew(Box3DFilterJointImpl3D(body, excepted));
	joint->rebuild();

	// Godot records the exception on the requesting body only, matching GodotBody3D, even
	// though the filter joint it backs stops collision in both directions.
	body->get_collision_exceptions().insert(p_excepted_body, joint);
	bodies_with_exceptions.insert(body);
}

void Box3DPhysicsServer3D::body_remove_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (soft_body_owner.owns(p_body)) {
		soft_body_remove_collision_exception(p_body, p_excepted_body);
		return;
	}
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	if (body->get_soft_exceptions().erase(p_excepted_body)) {
		body->rebuild_shapes();
		return;
	}

	HashMap<RID, Box3DFilterJointImpl3D *>::Iterator entry = body->get_collision_exceptions().find(p_excepted_body);
	if (!entry) {
		return;
	}
	Box3DFilterJointImpl3D *joint = entry->value;
	body->get_collision_exceptions().remove(entry);
	// Re-enabling first makes Box3D re-query the broad-phase, so a pair that is already
	// overlapping regains a contact instead of staying interpenetrated.
	joint->set_collision_disabled(false);
	memdelete(joint);

	if (body->get_collision_exceptions().is_empty()) {
		bodies_with_exceptions.erase(body);
	}
}

TypedArray<RID> Box3DPhysicsServer3D::_body_get_collision_exceptions_compat(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (soft_body_owner.owns(p_body)) {
		return _soft_body_get_collision_exceptions_compat(p_body);
	}
	TypedArray<RID> exceptions;
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, exceptions);
	for (const KeyValue<RID, Box3DFilterJointImpl3D *> &entry : body->get_collision_exceptions()) {
		exceptions.push_back(entry.key);
	}
	for (RID other : body->get_soft_exceptions()) {
		exceptions.push_back(other);
	}
	return exceptions;
}

void Box3DPhysicsServer3D::body_set_max_contacts_reported(RID p_body, int32_t p_amount) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_max_contacts_reported(p_amount);
}

int32_t Box3DPhysicsServer3D::body_get_max_contacts_reported(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_max_contacts_reported();
}

void Box3DPhysicsServer3D::body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_COND(!std::isfinite(p_threshold) || p_threshold < 0);
	body->set_contact_depth_threshold(p_threshold);
}

real_t Box3DPhysicsServer3D::body_get_contacts_reported_depth_threshold(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_contact_depth_threshold();
}

void Box3DPhysicsServer3D::body_set_omit_force_integration(RID p_body, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_omit_force_integration(p_enable);
}

bool Box3DPhysicsServer3D::body_is_omitting_force_integration(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_omitting_force_integration();
}

void Box3DPhysicsServer3D::body_set_state_sync_callback(RID p_body, const Callable &p_callable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_state_sync_callback(p_callable);
}

void Box3DPhysicsServer3D::body_set_force_integration_callback(RID p_body, const Callable &p_callable, const Variant &p_userdata) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_force_integration_callback(p_callable, p_userdata);
}

void Box3DPhysicsServer3D::body_set_ray_pickable(RID p_body, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = get_body(p_body);
	ERR_FAIL_NULL(body);
	body->set_ray_pickable(p_enable);
}

bool Box3DPhysicsServer3D::_body_test_motion_compat(
		RID p_body,
		const Transform3D &p_from,
		const Vector3 &p_motion,
		real_t p_margin,
		int32_t p_max_collisions,
		bool p_recovery_as_collision,
		PS3DT::MotionResult *p_result) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	Box3DSpace3D *space = body->get_space();
	ERR_FAIL_NULL_V(space, false);

	return space->get_direct_state()->test_body_motion(*body, p_from, p_motion, p_margin, p_max_collisions, p_recovery_as_collision, p_result);
}

PhysicsDirectBodyState3D *Box3DPhysicsServer3D::body_get_direct_state(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DBodyImpl3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, nullptr);
	if (!body->has_body_id()) {
		return nullptr;
	}
	return body->get_direct_state_or_null();
}

// --- Joints ---

RID Box3DPhysicsServer3D::joint_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	// Placeholder RID: the concrete Box3DJointImpl3D is only constructed once
	// joint_make_pin/joint_make_hinge/joint_make_slider is called (see below), since
	// only then are the joint type and both body RIDs known.
	const RID rid = joint_owner.make_rid(nullptr);
	return rid;
}

void Box3DPhysicsServer3D::joint_clear(RID p_joint) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DJointImpl3D *joint = joint_owner.get_or_null(p_joint);
	if (joint != nullptr) {
		memdelete(joint);
		joint_owner.replace(p_joint, nullptr);
	}
}

void Box3DPhysicsServer3D::joint_make_pin(RID p_joint, RID p_body_a, const Vector3 &p_local_a, RID p_body_b, const Vector3 &p_local_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	joint_clear(p_joint);

	Box3DBodyImpl3D *body_a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !body_b, "Invalid Box3D joint body RID.");

	auto *joint = memnew(Box3DPinJointImpl3D(body_a, body_b, Transform3D(Basis(), p_local_a), Transform3D(Basis(), p_local_b)));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::pin_joint_set_param(RID p_joint, PS3DE::PinJointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL(joint);
	joint->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::pin_joint_get_param(RID p_joint, PS3DE::PinJointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL_V(joint, 0.0);
	return joint->get_param(p_param);
}

void Box3DPhysicsServer3D::pin_joint_set_local_a(RID p_joint, const Vector3 &p_local_a) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	// PinJoint3D writes its anchors before naming its bodies, so the joint may not exist
	// yet; joint_make_pin receives the same anchors and applies them.
	if (joint == nullptr) {
		return;
	}
	joint->set_local_frame_a(Transform3D(joint->get_local_frame_a().basis, p_local_a));
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_a(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	if (joint == nullptr) {
		return Vector3();
	}
	return joint->get_local_frame_a().origin;
}

void Box3DPhysicsServer3D::pin_joint_set_local_b(RID p_joint, const Vector3 &p_local_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	// See pin_joint_set_local_a.
	if (joint == nullptr) {
		return;
	}
	joint->set_local_frame_b(Transform3D(joint->get_local_frame_b().basis, p_local_b));
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_b(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_PIN ? static_cast<Box3DPinJointImpl3D *>(base_joint) : nullptr;
	if (joint == nullptr) {
		return Vector3();
	}
	return joint->get_local_frame_b().origin;
}

void Box3DPhysicsServer3D::joint_make_hinge(RID p_joint, RID p_body_a, const Transform3D &p_hinge_a, RID p_body_b, const Transform3D &p_hinge_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	joint_clear(p_joint);

	Box3DBodyImpl3D *body_a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !body_b, "Invalid Box3D joint body RID.");

	// Box3D's revolute joint rotates about the LOCAL Z axis of the joint frames; Godot's
	// HingeJoint3D convention uses local Z as the hinge axis too (the incoming p_hinge_a/b
	// transforms are the joint frames as Godot core builds them for HingeJoint3D), so no
	// remap is required here.
	auto *joint = memnew(Box3DHingeJointImpl3D(body_a, body_b, p_hinge_a, p_hinge_b));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::joint_make_hinge_simple(RID p_joint, RID p_body_a, const Vector3 &p_pivot_a, const Vector3 &p_axis_a, RID p_body_b, const Vector3 &p_pivot_b, const Vector3 &p_axis_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	joint_clear(p_joint);

	Box3DBodyImpl3D *body_a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !body_b, "Invalid Box3D joint body RID.");

	// Build a joint-frame basis that maps local Z to the given hinge axis (shortest-arc
	// rotation from +Z), since Box3D's revolute joint always rotates about local Z.
	const Vector3 z_axis(0, 0, 1);
	const Quaternion rotation_a = Quaternion(z_axis, p_axis_a.normalized());
	const Quaternion rotation_b = Quaternion(z_axis, p_axis_b.normalized());

	const Transform3D frame_a(Basis(rotation_a), p_pivot_a);
	const Transform3D frame_b(Basis(rotation_b), p_pivot_b);

	auto *joint = memnew(Box3DHingeJointImpl3D(body_a, body_b, frame_a, frame_b));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::hinge_joint_set_param(RID p_joint, PS3DE::HingeJointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_HINGE ? static_cast<Box3DHingeJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL(joint);
	joint->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::hinge_joint_get_param(RID p_joint, PS3DE::HingeJointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_HINGE ? static_cast<Box3DHingeJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL_V(joint, 0.0);
	return joint->get_param(p_param);
}

void Box3DPhysicsServer3D::hinge_joint_set_flag(RID p_joint, PS3DE::HingeJointFlag p_flag, bool p_enabled) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_HINGE ? static_cast<Box3DHingeJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL(joint);
	joint->set_flag(p_flag, p_enabled);
}

bool Box3DPhysicsServer3D::hinge_joint_get_flag(RID p_joint, PS3DE::HingeJointFlag p_flag) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_HINGE ? static_cast<Box3DHingeJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL_V(joint, false);
	return joint->get_flag(p_flag);
}

void Box3DPhysicsServer3D::joint_make_slider(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	joint_clear(p_joint);

	Box3DBodyImpl3D *body_a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !body_b, "Invalid Box3D joint body RID.");

	// Box3D's prismatic joint slides along the LOCAL X axis of local frame A, matching
	// Godot's SliderJoint3D convention (which also slides along local X), so no axis
	// remap is required here.
	auto *joint = memnew(Box3DSliderJointImpl3D(body_a, body_b, p_local_ref_a, p_local_ref_b));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::slider_joint_set_param(RID p_joint, PS3DE::SliderJointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_SLIDER ? static_cast<Box3DSliderJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL(joint);
	joint->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::slider_joint_get_param(RID p_joint, PS3DE::SliderJointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base_joint = joint_owner.get_or_null(p_joint);
	auto *joint = base_joint && base_joint->get_type() == PS3DE::JOINT_TYPE_SLIDER ? static_cast<Box3DSliderJointImpl3D *>(base_joint) : nullptr;
	ERR_FAIL_NULL_V(joint, 0.0);
	return joint->get_param(p_param);
}

void Box3DPhysicsServer3D::joint_make_cone_twist(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	ERR_FAIL_COND(!joint_owner.owns(p_joint));
	Box3DBodyImpl3D *body_a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *body_b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(body_a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !body_b, "Invalid Box3D joint body RID.");
	joint_clear(p_joint);
	auto *joint = memnew(Box3DConeTwistJointImpl3D(body_a, body_b, p_local_ref_a, p_local_ref_b));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::cone_twist_joint_set_param(RID p_joint, PS3DE::ConeTwistJointParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND(!base || base->get_type() != PS3DE::JOINT_TYPE_CONE_TWIST);
	static_cast<Box3DConeTwistJointImpl3D *>(base)->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::cone_twist_joint_get_param(RID p_joint, PS3DE::ConeTwistJointParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND_V(!base || base->get_type() != PS3DE::JOINT_TYPE_CONE_TWIST, 0);
	return static_cast<Box3DConeTwistJointImpl3D *>(base)->get_param(p_param);
}

void Box3DPhysicsServer3D::joint_make_generic_6dof(RID p_joint, RID p_body_a, const Transform3D &p_local_ref_a, RID p_body_b, const Transform3D &p_local_ref_b) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	ERR_FAIL_COND(!joint_owner.owns(p_joint));
	Box3DBodyImpl3D *a = body_owner.get_or_null(p_body_a);
	Box3DBodyImpl3D *b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_NULL(a);
	ERR_FAIL_COND_MSG(p_body_b.is_valid() && !b, "Invalid Box3D joint body RID.");
	joint_clear(p_joint);
	auto *joint = memnew(Box3DGenericJointImpl3D(a, b, p_local_ref_a, p_local_ref_b));
	joint->set_rid(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisParam p_param, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF);
	static_cast<Box3DGenericJointImpl3D *>(base)->set_param(p_axis, p_param, p_value);
}

real_t Box3DPhysicsServer3D::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisParam p_param) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND_V(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF, 0.0);
	return static_cast<Box3DGenericJointImpl3D *>(base)->get_param(p_axis, p_param);
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisFlag p_flag, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF);
	static_cast<Box3DGenericJointImpl3D *>(base)->set_flag(p_axis, p_flag, p_enable);
}

bool Box3DPhysicsServer3D::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, PS3DE::G6DOFJointAxisFlag p_flag) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND_V(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF, false);
	return static_cast<Box3DGenericJointImpl3D *>(base)->get_flag(p_axis, p_flag);
}

PS3DE::JointType Box3DPhysicsServer3D::joint_get_type(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DJointImpl3D *joint = joint_owner.get_or_null(p_joint);
	if (joint == nullptr) {
		return PS3DE::JOINT_TYPE_MAX;
	}
	return joint->get_type();
}

void Box3DPhysicsServer3D::joint_disable_collisions_between_bodies(RID p_joint, bool p_disable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DJointImpl3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_collision_disabled(p_disable);
}

bool Box3DPhysicsServer3D::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	Box3DJointImpl3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return joint->is_collision_disabled();
}

// --- Soft bodies ---

RID Box3DPhysicsServer3D::soft_body_create() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = memnew(Box3DSoftBodyImpl3D);
	RID rid = soft_body_owner.make_rid(body);
	body->set_rid(rid);
	return rid;
}

void Box3DPhysicsServer3D::soft_body_update_rendering_server(RID p_body, RequiredParam<PhysicsServer3DRenderingServerHandler> p_rendering_server_handler) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	EXTRACT_PARAM_OR_FAIL(handler, p_rendering_server_handler);
	body->update_rendering(handler);
}

void Box3DPhysicsServer3D::soft_body_set_space(RID p_body, RID p_space) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	auto *space = p_space.is_valid() ? space_owner.get_or_null(p_space) : nullptr;
	ERR_FAIL_COND(p_space.is_valid() && !space);
	body->set_space(space);
}

RID Box3DPhysicsServer3D::soft_body_get_space(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());
	return body->get_space() ? body->get_space()->get_rid() : RID();
}

void Box3DPhysicsServer3D::soft_body_set_ray_pickable(RID p_body, bool p_enable) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_pickable(p_enable);
}

void Box3DPhysicsServer3D::soft_body_set_collision_layer(RID p_body, uint32_t p_layer) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::soft_body_get_collision_layer(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_layer();
}

void Box3DPhysicsServer3D::soft_body_set_collision_mask(RID p_body, uint32_t p_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::soft_body_get_collision_mask(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_mask();
}

void Box3DPhysicsServer3D::soft_body_add_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_COND(!body_owner.owns(p_excepted_body) && !soft_body_owner.owns(p_excepted_body));
	body->set_exception(p_excepted_body, true);
}

void Box3DPhysicsServer3D::soft_body_remove_collision_exception(RID p_body, RID p_excepted_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_exception(p_excepted_body, false);
}

TypedArray<RID> Box3DPhysicsServer3D::_soft_body_get_collision_exceptions_compat(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, TypedArray<RID>());
	TypedArray<RID> result;
	for (RID other : body->get_exceptions()) {
		result.push_back(other);
	}
	return result;
}

void Box3DPhysicsServer3D::soft_body_set_state(RID p_body, PS3DE::BodyState p_state, const Variant &p_variant) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_state(p_state, p_variant);
}

Variant Box3DPhysicsServer3D::soft_body_get_state(RID p_body, PS3DE::BodyState p_state) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());
	return body->get_state(p_state);
}

void Box3DPhysicsServer3D::soft_body_set_transform(RID p_body, const Transform3D &p_transform) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_transform(p_transform);
}

void Box3DPhysicsServer3D::soft_body_set_simulation_precision(RID p_body, int32_t p_simulation_precision) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_precision(p_simulation_precision);
}

int32_t Box3DPhysicsServer3D::soft_body_get_simulation_precision(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_precision();
}

void Box3DPhysicsServer3D::soft_body_set_total_mass(RID p_body, real_t p_total_mass) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_total_mass(p_total_mass);
}

real_t Box3DPhysicsServer3D::soft_body_get_total_mass(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_total_mass();
}

void Box3DPhysicsServer3D::soft_body_set_linear_stiffness(RID p_body, real_t p_stiffness) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_stiffness(p_stiffness);
}

real_t Box3DPhysicsServer3D::soft_body_get_linear_stiffness(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_stiffness();
}

void Box3DPhysicsServer3D::soft_body_set_pressure_coefficient(RID p_body, real_t p_pressure_coefficient) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_pressure(p_pressure_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_pressure_coefficient(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_pressure();
}

void Box3DPhysicsServer3D::soft_body_set_damping_coefficient(RID p_body, real_t p_damping_coefficient) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_damping(p_damping_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_damping_coefficient(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_damping();
}

void Box3DPhysicsServer3D::soft_body_set_drag_coefficient(RID p_body, real_t p_drag_coefficient) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_drag(p_drag_coefficient);
}

real_t Box3DPhysicsServer3D::soft_body_get_drag_coefficient(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_drag();
}

void Box3DPhysicsServer3D::soft_body_set_mesh(RID p_body, RID p_mesh) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_mesh(p_mesh);
}

AABB Box3DPhysicsServer3D::soft_body_get_bounds(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, AABB());
	return body->get_bounds();
}

void Box3DPhysicsServer3D::soft_body_move_point(RID p_body, int32_t p_point_index, const Vector3 &p_global_position) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->move_point(p_point_index, p_global_position);
}

Vector3 Box3DPhysicsServer3D::soft_body_get_point_global_position(RID p_body, int32_t p_point_index) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_point(p_point_index);
}

void Box3DPhysicsServer3D::soft_body_remove_all_pinned_points(RID p_body) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->unpin_all();
}

void Box3DPhysicsServer3D::soft_body_pin_point(RID p_body, int32_t p_point_index, bool p_pin) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->pin_point(p_point_index, p_pin);
}

bool Box3DPhysicsServer3D::soft_body_is_point_pinned(RID p_body, int32_t p_point_index) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_pinned(p_point_index);
}

void Box3DPhysicsServer3D::soft_body_apply_point_impulse(RID p_body, int p_point, const Vector3 &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_point(p_point, p_value, true);
}

void Box3DPhysicsServer3D::soft_body_apply_point_force(RID p_body, int p_point, const Vector3 &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_point(p_point, p_value, false);
}

void Box3DPhysicsServer3D::soft_body_apply_central_impulse(RID p_body, const Vector3 &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central(p_value, true);
}

void Box3DPhysicsServer3D::soft_body_apply_central_force(RID p_body, const Vector3 &p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central(p_value, false);
}

void Box3DPhysicsServer3D::soft_body_set_shrinking_factor(RID p_body, real_t p_value) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shrinking(p_value);
}

real_t Box3DPhysicsServer3D::soft_body_get_shrinking_factor(RID p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_shrinking();
}

void Box3DPhysicsServer3D::soft_body_get_collision_exceptions(RID p_body, List<RID> *p_out) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *body = soft_body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_NULL(p_out);
	for (RID other : body->get_exceptions()) {
		p_out->push_back(other);
	}
}

// --- Lifecycle ---

void Box3DPhysicsServer3D::free_rid(RID p_rid) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (Box3DSoftBodyImpl3D *soft = soft_body_owner.get_or_null(p_rid)) {
		for (RID rid : body_owner.get_owned_list()) {
			if (auto *body = body_owner.get_or_null(rid); body && body->get_soft_exceptions().erase(p_rid)) {
				body->rebuild_shapes();
			}
		}
		for (RID rid : soft_body_owner.get_owned_list()) {
			if (auto *other = soft_body_owner.get_or_null(rid); other && other != soft && other->get_exceptions().has(p_rid)) {
				other->set_exception(p_rid, false);
			}
		}
		memdelete(soft);
		soft_body_owner.free(p_rid);
		return;
	}
	// Joints and shapes free before bodies/areas that reference them; bodies/areas free
	// before shapes they hold (mirrors JoltPhysicsServer3DExtension::free_rid's ordering).
	if (joint_owner.owns(p_rid)) {
		if (Box3DJointImpl3D *joint = joint_owner.get_or_null(p_rid)) {
			memdelete(joint);
		}
		// joint_clear() keeps a null placeholder so the RID can be rebound.
		// Free the owned RID even when no concrete joint remains.
		joint_owner.free(p_rid);
		return;
	}

	if (Box3DBodyImpl3D *body = body_owner.get_or_null(p_rid)) {
		for (RID rid : soft_body_owner.get_owned_list()) {
			if (auto *soft = soft_body_owner.get_or_null(rid); soft && soft->get_exceptions().has(p_rid)) {
				soft->set_exception(p_rid, false);
			}
		}
		_clear_collision_exceptions(body);
		for (RID rid : joint_owner.get_owned_list()) {
			if (auto *joint = joint_owner.get_or_null(rid)) {
				joint->forget_body(body);
			}
		}
		if (Box3DSpace3D *space = body->get_space()) {
			space->forget_object(body);
			space->unregister_body(body);
		}
		memdelete(body);
		body_owner.free(p_rid);
		return;
	}

	if (Box3DAreaImpl3D *area = area_owner.get_or_null(p_rid)) {
		if (Box3DSpace3D *space = area->get_space()) {
			space->forget_object(area);
			space->unregister_area(area);
		}
		memdelete(area);
		area_owner.free(p_rid);
		return;
	}

	if (Box3DShapeImpl3D *shape = shape_owner.get_or_null(p_rid)) {
		for (auto *owner : box3d_sorted(shape->get_owners())) {
			owner->remove_shape(shape);
		}
		memdelete(shape);
		shape_owner.free(p_rid);
		return;
	}

	if (Box3DSpace3D *space = space_owner.get_or_null(p_rid)) {
		ERR_FAIL_COND_MSG(space->is_flushing_queries(), "Cannot free the active Box3D space from a physics callback.");
		for (RID rid : soft_body_owner.get_owned_list()) {
			if (auto *soft = soft_body_owner.get_or_null(rid); soft && soft->get_space() == space) {
				soft->set_space(nullptr);
			}
		}
		for (RID rid : body_owner.get_owned_list()) {
			if (auto *body = body_owner.get_or_null(rid); body && body->get_space() == space) {
				body_set_space(rid, RID());
			}
		}
		for (RID rid : area_owner.get_owned_list()) {
			if (auto *area = area_owner.get_or_null(rid); area && area->get_space() == space && area != space->get_default_area()) {
				area_set_space(rid, RID());
			}
		}
		Box3DAreaImpl3D *default_area = space->get_default_area();
		if (default_area != nullptr) {
			const RID default_area_rid = default_area->get_rid();
			space->unregister_area(default_area);
			memdelete(default_area);
			area_owner.free(default_area_rid);
		}
		active_spaces.erase(space);
		memdelete(space);
		space_owner.free(p_rid);
		return;
	}
}

void Box3DPhysicsServer3D::set_active(bool p_active) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	active = p_active;
}

void Box3DPhysicsServer3D::init() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
}

void Box3DPhysicsServer3D::step(real_t p_step) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	ERR_FAIL_COND_MSG(fixed_tick_rate < 1 || fixed_tick_rate > 240 || Engine::get_singleton()->get_physics_ticks_per_second() != fixed_tick_rate, "Box3D requires an immutable tick rate in 1..240.");
	ERR_FAIL_COND_MSG(!std::isfinite(p_step) || std::abs(p_step - real_t(1.0) / fixed_tick_rate) > real_t(1e-7), "Box3D scene step must equal the configured fixed timestep; time scaling needs a qualified policy.");
	ERR_FAIL_COND_MSG(std::fegetround() != FE_TONEAREST || b3GetLengthUnitsPerMeter() != 1.0f, "Box3D requires round-to-nearest and meter units.");

	if (!active) {
		return;
	}
	for (Box3DSpace3D *space : box3d_sorted(active_spaces)) {
		space->step(1.0f / fixed_tick_rate);
	}
}

void Box3DPhysicsServer3D::sync() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
}

void Box3DPhysicsServer3D::flush_queries() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (!active) {
		return;
	}
	LocalVector<RID> spaces;
	for (Box3DSpace3D *space : box3d_sorted(active_spaces)) {
		spaces.push_back(space->get_rid());
	}
	for (RID rid : spaces) {
		if (auto *space = space_owner.get_or_null(rid); space && space->is_active()) {
			space->flush_queries();
		}
	}
}

void Box3DPhysicsServer3D::end_sync() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
}

void Box3DPhysicsServer3D::finish() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
}

bool Box3DPhysicsServer3D::is_flushing_queries() const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	for (Box3DSpace3D *space : box3d_sorted(active_spaces)) {
		if (space->is_flushing_queries()) {
			return true;
		}
	}
	return false;
}

int32_t Box3DPhysicsServer3D::get_process_info(PS3DE::ProcessInfo p_process_info) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	int32_t total = 0;
	for (const Box3DSpace3D *space : box3d_sorted(active_spaces)) {
		const b3Counters counters = b3World_GetCounters(space->get_world_id());
		switch (p_process_info) {
			case PS3DE::INFO_ACTIVE_OBJECTS:
				total += b3World_GetAwakeBodyCount(space->get_world_id());
				break;
			case PS3DE::INFO_COLLISION_PAIRS:
				total += counters.contactCount;
				break;
			case PS3DE::INFO_ISLAND_COUNT:
				total += counters.islandCount;
				break;
			default:
				break;
		}
	}
	return total;
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_angular_target_rotation(RID rid, const Quaternion &rotation) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(rid);
	ERR_FAIL_COND(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF);
	static_cast<Box3DGenericJointImpl3D *>(base)->set_target_rotation(rotation);
}
Quaternion Box3DPhysicsServer3D::generic_6dof_joint_get_angular_target_rotation(RID rid) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(rid);
	ERR_FAIL_COND_V(!base || base->get_type() != PS3DE::JOINT_TYPE_6DOF, Quaternion());
	return static_cast<Box3DGenericJointImpl3D *>(base)->get_target_rotation();
}

void Box3DPhysicsServer3D::joint_make_configured(RID p_joint, PS3DE::JointType p_type, RID p_body_a, const Transform3D &p_frame_a, RID p_body_b, const Transform3D &p_frame_b, const Dictionary &p_configuration) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	ERR_FAIL_COND(!joint_owner.owns(p_joint));
	auto *a = body_owner.get_or_null(p_body_a);
	auto *b = body_owner.get_or_null(p_body_b);
	ERR_FAIL_COND(!a || (p_body_b.is_valid() && !b) || a == b);
	ERR_FAIL_COND(!p_frame_a.is_finite() || !p_frame_b.is_finite());
	if (!Box3DConfiguredJoint3D::validate_configuration(p_type, p_configuration)) {
		return;
	}
	auto *joint = memnew(Box3DConfiguredJoint3D(p_type, a, b, p_frame_a, p_frame_b));
	joint->set_rid(p_joint);
	joint->set_configuration(p_configuration);
	joint_clear(p_joint);
	joint_owner.replace(p_joint, joint);
	joint->rebuild();
}
void Box3DPhysicsServer3D::joint_set_configuration(RID p_joint, const Dictionary &p_configuration) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND(!base || base->get_type() < PS3DE::JOINT_TYPE_DISTANCE || base->get_type() > PS3DE::JOINT_TYPE_WHEEL);
	static_cast<Box3DConfiguredJoint3D *>(base)->set_configuration(p_configuration);
}
Dictionary Box3DPhysicsServer3D::joint_get_configuration(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *base = joint_owner.get_or_null(p_joint);
	ERR_FAIL_COND_V(!base || base->get_type() < PS3DE::JOINT_TYPE_DISTANCE || base->get_type() > PS3DE::JOINT_TYPE_WHEEL, Dictionary());
	return static_cast<Box3DConfiguredJoint3D *>(base)->get_configuration();
}

void Box3DPhysicsServer3D::space_apply_explosion(RID p_space, const Vector3 &p_position, real_t p_radius, real_t p_falloff, real_t p_impulse_density, uint32_t p_collision_mask) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	ERR_FAIL_COND(!p_position.is_finite() || !Math::is_finite(p_radius) || !Math::is_finite(p_falloff) || !Math::is_finite(p_impulse_density) || p_radius <= 0 || p_falloff < 0);
	auto def = b3DefaultExplosionDef();
	def.position = godot_to_b3(p_position);
	def.radius = p_radius;
	def.falloff = p_falloff;
	def.impulsePerArea = p_impulse_density;
	def.maskBits = p_collision_mask;
	b3World_Explode(space->get_world_id(), &def);
}
Array Box3DPhysicsServer3D::space_get_contact_hit_events(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, Array());
	return space->get_contact_hit_events().duplicate(true);
}
Array Box3DPhysicsServer3D::space_get_joint_events(RID p_space) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, Array());
	return space->get_joint_events().duplicate(true);
}
Vector3 Box3DPhysicsServer3D::joint_get_constraint_force(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	if (!b3Joint_IsValid(joint->get_joint_id())) {
		return Vector3();
	}
	return b3_to_godot(b3Joint_GetConstraintForce(joint->get_joint_id()));
}
Vector3 Box3DPhysicsServer3D::joint_get_constraint_torque(RID p_joint) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	if (!b3Joint_IsValid(joint->get_joint_id())) {
		return Vector3();
	}
	return b3_to_godot(b3Joint_GetConstraintTorque(joint->get_joint_id()));
}
