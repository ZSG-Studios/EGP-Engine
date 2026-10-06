// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_area_2d.h"

#include "../box2d_physics_server_2d.h"
#include "../spaces/box2d_space_2d.h"
#include "box2d_body_2d.h"

#include "modules/box2d/precompiled.h"

Box2DArea2D::Box2DArea2D() :
		Box2DCollisionObject2D(Type::AREA) {
	body_def.type = b2_kinematicBody;
	mode = PS2DE::BODY_MODE_KINEMATIC;
	shape_def.isSensor = true;

	const uint32_t default_bitmask = 1u << 0;
	set_collision_layer(default_bitmask);
	set_collision_mask(default_bitmask);
}

Box2DArea2D::~Box2DArea2D() {
	if (is_default_area()) {
		get_space()->set_default_area(nullptr);
	}
}

void Box2DArea2D::on_added_to_space() {
	update_area_step_list();
}

void Box2DArea2D::on_remove_from_space() {
	if (in_area_step_list) {
		space->remove_active_area(this);
		in_area_step_list = false;
	}

	// Areas do not emit events when they are freed - consistent with Godot Physics.
	// The overrides still have to come off, otherwise a body that was inside a gravity
	// replacement keeps a zero gravity scale forever. Areas that remain reapply theirs
	// at the top of the next step.
	for (const auto &[overlap_rid, overlap_count] : object_overlap_count) {
		auto *object = Box2DPhysicsServer2D::get_singleton()->get_object(overlap_rid);
		if (!object) {
			continue;
		}
		Box2DBody2D *body = object->as_body();
		if (body && body->in_space()) {
			body->apply_area_overrides();
		}
	}

	overlaps.clear();
	object_overlap_count.clear();
}

void Box2DArea2D::shapes_changed() {
	update_overlaps();
}

// Overlap history stores stable owner RIDs and indices, never pointers into a
// collision object's movable shape array. Compound primitives aggregate by shape.
void Box2DArea2D::add_overlap(Box2DShapeInstance *, Box2DShapeInstance *) {
	update_overlaps();
}
void Box2DArea2D::remove_overlap(Box2DShapeInstance *, Box2DShapeInstance *) {
	update_overlaps();
}
void Box2DArea2D::update_overlaps() {
	if (updating_overlaps || is_freed()) {
		return;
	}
	updating_overlaps = true;
	HashMap<ShapePair, ObjectAndOverlapCount, ShapePair> next;
	HashMap<RID, int> next_objects;
	if (in_space()) {
		for (b2ShapeId sensor : BodyShapeRange(body_id)) {
			if (!b2Shape_IsValid(sensor) || !b2Shape_IsSensor(sensor)) {
				continue;
			}
			auto *self = static_cast<Box2DShapeInstance *>(b2Shape_GetUserData(sensor));
			int capacity = b2Shape_GetSensorCapacity(sensor);
			LocalVector<b2ShapeId> hits;
			hits.resize(capacity);
			int count = b2Shape_GetSensorData(sensor, hits.ptr(), capacity);
			for (int i = 0; i < count; ++i) {
				if (!b2Shape_IsValid(hits[i])) {
					continue;
				}
				auto *other = static_cast<Box2DShapeInstance *>(b2Shape_GetUserData(hits[i]));
				auto *object = other ? other->get_collision_object() : nullptr;
				if (!self || !object || object->is_freed()) {
					continue;
				}
				if (object->is_area() && !object->as_area()->get_monitorable()) {
					continue;
				}
				ShapePair pair{ object->get_rid(), other->get_index(), self->get_index() };
				auto &entry = next[pair];
				if (entry.count++ == 0) {
					next_objects[object->get_rid()]++;
				}
				entry.instance_id = ObjectID(object->get_instance_id());
				entry.type = object->get_type();
			}
		}
	}
	struct Event {
		ShapePair pair;
		ObjectAndOverlapCount data;
		AreaBodyStatus status;
	};
	LocalVector<Event> events;
	LocalVector<ShapePair> keys;
	for (const auto &entry : overlaps) {
		keys.push_back(entry.key);
	}
	for (const auto &entry : next) {
		if (!overlaps.has(entry.key)) {
			keys.push_back(entry.key);
		}
	}
	keys.sort();
	for (const auto &pair : keys) {
		if (overlaps.has(pair) && !next.has(pair)) {
			events.push_back({ pair, overlaps[pair], AREA_BODY_REMOVED });
		}
		if (!overlaps.has(pair) && next.has(pair)) {
			events.push_back({ pair, next[pair], AREA_BODY_ADDED });
		}
	}
	for (const auto &entry : object_overlap_count) {
		if (!next_objects.has(entry.key)) {
			auto *object = Box2DPhysicsServer2D::get_singleton()->get_object(entry.key);
			if (object && object->is_body() && object->in_space()) {
				object->as_body()->apply_area_overrides();
			}
		}
	}
	overlaps = std::move(next);
	object_overlap_count = std::move(next_objects);
	for (const auto &event : events) {
		if (is_freed()) {
			break;
		}
		report_event(event.data.type, event.status, event.pair.other_rid, event.data.instance_id, event.pair.other_index, event.pair.self_index);
	}
	updating_overlaps = false;
}

void Box2DArea2D::apply_overrides() {
	for (const auto &[overlap_rid, overlap_count] : object_overlap_count) {
		auto *object = Box2DPhysicsServer2D::get_singleton()->get_object(overlap_rid);
		if (!object) {
			continue;
		}
		Box2DBody2D *body = object->as_body();
		if (!body || !object->in_space()) {
			continue;
		}

		Box2DBody2D::AreaOverrideAccumulator &overrides = body->area_overrides;

		if (!overrides.ignore_remaining_gravity) {
			overrides.ignore_remaining_gravity = integrate(
					overrides.total_gravity,
					compute_gravity(body->get_center_of_mass_global()),
					override_gravity_mode);

			if (override_gravity_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE ||
					override_gravity_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE) {
				overrides.skip_world_gravity = true;
			}
		}

		if (!overrides.ignore_remaining_linear_damp) {
			overrides.ignore_remaining_linear_damp = integrate(
					overrides.total_linear_damp,
					linear_damp,
					override_linear_damp_mode);
			if (override_linear_damp_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE ||
					override_linear_damp_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE) {
				overrides.skip_world_linear_damp = true;
			}
		}

		if (!overrides.ignore_remaining_angular_damp) {
			overrides.ignore_remaining_angular_damp = integrate(
					overrides.total_angular_damp,
					angular_damp,
					override_angular_damp_mode);
			if (override_angular_damp_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE ||
					override_angular_damp_mode == PS2DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE) {
				overrides.skip_world_angular_damp = true;
			}
		}

		ERR_FAIL_NULL(space);
		space->add_body_with_overrides(body);
	}
}

void Box2DArea2D::update_area_step_list() {
	if (!in_space() || is_default_area()) {
		return;
	}

	ERR_FAIL_NULL(space);

	bool has_gravity = override_gravity_mode != PS2DE::AREA_SPACE_OVERRIDE_DISABLED;
	bool has_linear_damp = override_linear_damp_mode != PS2DE::AREA_SPACE_OVERRIDE_DISABLED;
	bool has_angular_damp = override_angular_damp_mode != PS2DE::AREA_SPACE_OVERRIDE_DISABLED;

	bool has_overrides = has_gravity || has_linear_damp || has_angular_damp;

	if (has_overrides) {
		if (!in_area_step_list) {
			space->add_active_area(this);
			in_area_step_list = true;
		}
	} else {
		if (in_area_step_list) {
			space->remove_active_area(this);
			in_area_step_list = false;
		}
	}
}

void Box2DArea2D::report_event(
		Type p_type,
		PS2DE::AreaBodyStatus p_status,
		RID p_other_rid, ObjectID p_other_id,
		int32_t p_other_shape_index,
		int32_t p_self_shape_index) {
	static thread_local Array arguments = []() {
		Array array;
		array.resize(5);
		return array;
	}();

	arguments[0] = p_status;
	arguments[1] = p_other_rid;
	arguments[2] = uint64_t(p_other_id);
	arguments[3] = p_other_shape_index;
	arguments[4] = p_self_shape_index;

	if (p_type == Type::AREA) {
		if (!area_monitor_callback.is_valid()) {
			return;
		}
		area_monitor_callback.callv(arguments);
	} else {
		if (!body_monitor_callback.is_valid()) {
			return;
		}
		body_monitor_callback.callv(arguments);
	}
}

bool Box2DArea2D::is_default_area() const {
	return space && space->get_default_area() == this;
}

void Box2DArea2D::set_gravity_override_mode(PS2DE::AreaSpaceOverrideMode p_mode) {
	override_gravity_mode = p_mode;
	update_area_step_list();
}

void Box2DArea2D::set_linear_damp_override_mode(PS2DE::AreaSpaceOverrideMode p_mode) {
	override_linear_damp_mode = p_mode;
	update_area_step_list();
}

void Box2DArea2D::set_angular_damp_override_mode(PS2DE::AreaSpaceOverrideMode p_mode) {
	override_angular_damp_mode = p_mode;
	update_area_step_list();
}

void Box2DArea2D::set_linear_damp(real_t p_damp) {
	linear_damp = p_damp;
	update_area_step_list();
}

void Box2DArea2D::set_angular_damp(real_t p_damp) {
	angular_damp = p_damp;
	update_area_step_list();
}

void Box2DArea2D::set_gravity_strength(real_t p_strength) {
	gravity_strength = p_strength;
	update_area_step_list();
	gravity_changed();
}

void Box2DArea2D::set_gravity_direction(Vector2 p_direction) {
	gravity_direction = p_direction.normalized();
	update_area_step_list();
	gravity_changed();
}

void Box2DArea2D::set_gravity_point_enabled(bool p_enabled) {
	gravity_point_enabled = p_enabled;
	update_area_step_list();
}

Vector2 Box2DArea2D::compute_gravity(Vector2 p_position) const {
	if (gravity_point_enabled) {
		Vector2 displacement = get_transform().xform(gravity_direction) - p_position;

		if (gravity_point_unit_distance <= 0.0) {
			return displacement.normalized() * gravity_strength;
		}

		real_t distance_sq = displacement.length_squared();

		if (distance_sq <= 0.0) {
			return Vector2();
		}

		real_t adjusted_strength = gravity_strength * (gravity_point_unit_distance * gravity_point_unit_distance) / distance_sq;

		return displacement.normalized() * adjusted_strength;
	}

	return gravity_direction * gravity_strength;
}

void Box2DArea2D::gravity_changed() {
	if (is_default_area()) {
		space->set_default_gravity(gravity_direction * gravity_strength);
	}
}

void Box2DArea2D::set_priority(real_t p_priority) {
	priority = p_priority;

	if (in_area_step_list) {
		ERR_FAIL_NULL(space);
		space->remove_active_area(this);
		space->add_active_area(this);
	}
}

void Box2DArea2D::set_monitorable(bool p_monitorable) {
	monitorable = p_monitorable;
	set_collision_mask(shape_def.filter.maskBits);
}

uint64_t Box2DArea2D::modify_mask_bits(uint32_t p_mask) {
	uint64_t result = (uint64_t)p_mask | AREA_MASK_BIT;
	return result;
}
