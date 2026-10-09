/**************************************************************************/
/*  local_replay.cpp                                                      */
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

#include "local_replay.h"

#include "core/os/thread.h"

#include "modules/box2d/box2d_physics_server_2d.h"
#include "modules/box2d/simulation_guard.h"

#include <cstring>
#include <limits>
extern "C" {
#include "binding.h"
#include "reservation.h"
#include "solver_image.h"
}
#include "participant.h"

namespace {
struct SpaceLease {
	Box2DSpace2D *space = nullptr;
	spB2ReservedCandidate candidate = {};
};
SpaceLease leases[2];
uint64_t image_hash(const void *data, size_t size) {
	uint64_t h = 14695981039346656037ULL;
	auto *p = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}
bool same_id(uint64_t a, uint64_t b) {
	auto x = b2LoadBodyId(a), y = b2LoadBodyId(b);
	return x.index1 == y.index1 && x.generation == y.generation;
}
bool add_count(uint32_t &value, uint32_t count) {
	if (count > UINT32_MAX - value) {
		return false;
	}
	value += count;
	return true;
}
} //namespace

Error Box2DLocalReplay::measure(Box2DPhysicsServer2D *server, RID rid, Requirements &out) {
	if (!Thread::is_main_thread()) {
		return ERR_BUSY;
	}
	if (!server || server != Box2DPhysicsServer2D::get_singleton()) {
		return ERR_UNCONFIGURED;
	}
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = server->space_owner.get_or_null(rid);
	if (!space) {
		return ERR_INVALID_PARAMETER;
	}
	if (space->replay_pending_events || server->flushing_queries || space->locked || !server->bodies_to_delete.is_empty() ||
			!server->areas_to_delete.is_empty()) {
		return ERR_BUSY;
	}
	if (space->world_def.workerCount != 1 || !space->contact_hit_events.is_empty() ||
			!space->joint_events.is_empty() || !space->areas_to_step.is_empty() ||
			!space->force_integration_list.is_empty() || !space->bodies_with_exceptions.is_empty() ||
			!space->bodies_with_overrides.is_empty() || space->debug_contact_count != 0) {
		return ERR_UNAVAILABLE;
	}
	if (!spB2ParticipantQuiescent(space->world_id)) {
		return ERR_UNCONFIGURED;
	}
	Requirements next;
	uint32_t native_bodies = B2_IS_NON_NULL(space->world_anchor_body) ? 1 : 0;
	for (Box2DCollisionObject2D *object : space->objects) {
		if (!object || object->space != space || object->_is_freed) {
			return ERR_INVALID_DATA;
		}
		if (object == space->default_area) {
			if (server->area_owner.get_or_null(object->rid) != object ||
					!object->shapes.is_empty()) {
				return ERR_UNAVAILABLE;
			}
			if (B2_IS_NON_NULL(object->body_id) && !add_count(native_bodies, 1)) {
				return ERR_OUT_OF_MEMORY;
			}
			continue;
		}
		Box2DBody2D *body = object->as_body();
		if (!body || server->body_owner.get_or_null(object->rid) != body) {
			return ERR_UNAVAILABLE;
		}
		if (!body->body_state_callback.is_null() || !body->force_integration_callback.is_null() ||
				body->user_data.get_type() != Variant::NIL || body->force_integration_user_data.get_type() != Variant::NIL || !body->exceptions.is_empty() ||
				!body->exception_joints.is_empty() || body->max_contact_count != 0) {
			return ERR_UNAVAILABLE;
		}
		for (const auto &contact : body->contacts) {
			auto *other = server->body_owner.get_or_null(contact.collider);
			if (!other || other != contact.body || other->space != space || other->_is_freed ||
					contact.collider_instance_id != other->get_instance_id() || contact.local_shape < 0 ||
					uint32_t(contact.local_shape) >= body->shapes.size() || contact.collider_shape < 0 ||
					uint32_t(contact.collider_shape) >= other->shapes.size()) {
				return ERR_INVALID_DATA;
			}
		}
		if (!add_count(next.contacts, body->contacts.size())) {
			return ERR_OUT_OF_MEMORY;
		}
		if (!b2Body_IsValid(body->body_id) || b2Body_GetUserData(body->body_id) != body) {
			return ERR_INVALID_DATA;
		}
		if (!add_count(next.bodies, 1) || !add_count(native_bodies, 1) ||
				!add_count(next.shape_instances, body->shapes.size())) {
			return ERR_OUT_OF_MEMORY;
		}
		for (const Box2DShapeInstance &shape : body->shapes) {
			if (shape.object != body || !shape.shape || server->shape_owner.get_or_null(shape.shape->get_rid()) != shape.shape) {
				return ERR_INVALID_DATA;
			}
			if (!add_count(next.native_shapes, shape.shape_ids.size())) {
				return ERR_OUT_OF_MEMORY;
			}
			if (!add_count(next.shape_records, MAX(1U, shape.shape_ids.size()))) {
				return ERR_OUT_OF_MEMORY;
			}
			for (b2ShapeId id : shape.shape_ids) {
				if (!b2Shape_IsValid(id) || b2Shape_GetUserData(id) != &shape) {
					return ERR_INVALID_DATA;
				}
			}
		}
	}
	const b2Counters counters = b2World_GetCounters(space->world_id);
	for (uint32_t i = 0; i < space->constant_force_list.size(); ++i) {
		auto *b = space->constant_force_list[i];
		if (!b || !space->objects.has(b) || !b->in_constant_forces_list || !(b->has_constant_forces() || b->has_static_velocity())) {
			return ERR_INVALID_DATA;
		}
		for (uint32_t j = 0; j < i; ++j) {
			if (space->constant_force_list[j] == b) {
				return ERR_INVALID_DATA;
			}
		}
	}
	if (counters.jointCount != 0) {
		return ERR_UNAVAILABLE;
	}
	if (counters.bodyCount != native_bodies || counters.shapeCount != next.native_shapes) {
		return ERR_INVALID_DATA;
	}
	if (next.bodies > (SIZE_MAX - sizeof(SpaceRecord)) / sizeof(BodyRecord)) {
		return ERR_OUT_OF_MEMORY;
	}
	next.wrapper_bytes = sizeof(SpaceRecord) + next.bodies * sizeof(BodyRecord);
	if (next.shape_records > (SIZE_MAX - next.wrapper_bytes) / sizeof(ShapeRecord)) {
		return ERR_OUT_OF_MEMORY;
	}
	next.wrapper_bytes += next.shape_records * sizeof(ShapeRecord);
	if (next.contacts > (SIZE_MAX - next.wrapper_bytes) / sizeof(ContactRecord)) {
		return ERR_OUT_OF_MEMORY;
	}
	next.wrapper_bytes += next.contacts * sizeof(ContactRecord);
	out = next;
	return OK;
}

Error Box2DLocalReplay::capture(Box2DPhysicsServer2D *server, RID rid,
		BodyRecord *bodies, uint32_t body_capacity, ShapeRecord *shapes, uint32_t shape_capacity,
		SpaceRecord &space_out, Requirements &written, ContactRecord *contacts, uint32_t contact_capacity) {
	if (!Thread::is_main_thread()) {
		return ERR_BUSY;
	}
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Requirements required;
	Error error = measure(server, rid, required);
	if (error != OK) {
		return error;
	}
	if (body_capacity < required.bodies || shape_capacity < required.shape_records || contact_capacity < required.contacts) {
		return ERR_OUT_OF_MEMORY;
	}
	if ((required.bodies && !bodies) || (required.shape_records && !shapes) || (required.contacts && !contacts)) {
		return ERR_INVALID_PARAMETER;
	}
	Box2DSpace2D *space = server->space_owner.get_or_null(rid);
	uint32_t bi = 0, si = 0, ci = 0;
	for (Box2DCollisionObject2D *object : space->objects) {
		if (object == space->default_area) {
			continue;
		}
		Box2DBody2D *body = object->as_body();
		BodyRecord &record = bodies[bi++];
		record.rid = body->rid;
		record.native_body = b2StoreBodyId(body->body_id);
		record.wrapper_generation = body->generation;
		record.transform = body->current_transform;
		record.definition = body->body_def;
		record.shape_definition = body->shape_def;
		record.mass_data = body->mass_data;
		record.area_overrides = body->area_overrides;
		record.constant_force = body->constant_force;
		record.constant_torque = body->constant_torque;
		record.total_gravity = body->total_gravity;
		record.total_linear_damp = body->total_linear_damp;
		record.total_angular_damp = body->total_angular_damp;
		record.initial_linear_velocity = body->initial_linear_velocity;
		record.initial_angular_velocity = body->initial_angular_velocity;
		record.static_linear_velocity = body->static_linear_velocity;
		record.static_angular_velocity = body->static_angular_velocity;
		record.sleeping = body->sleeping;
		record.queried_contacts = body->queried_contacts;
		record.force_queue_index = UINT32_MAX;
		for (uint32_t i = 0; i < space->constant_force_list.size(); ++i) {
			if (space->constant_force_list[i] == body) {
				record.force_queue_index = i;
			}
		}
		record.contact_offset = ci;
		record.contact_count = body->contacts.size();
		for (const auto &c : body->contacts) {
			auto &v = contacts[ci++];
			v.body = c.collider;
			v.generation = c.body->generation;
			v.normal_impulse = c.normal_impulse;
			v.local_position = c.local_position;
			v.local_normal = c.local_normal;
			v.depth = c.depth;
			v.local_shape = c.local_shape;
			v.collider_position = c.collider_position;
			v.collider_shape = c.collider_shape;
			v.collider_instance_id = c.collider_instance_id;
			v.collider = c.collider;
			v.collider_velocity = c.collider_velocity;
			v.impulse = c.impulse;
		}
		record.mass = body->mass;
		record.inertia = body->inertia;
		record.center_of_mass = body->center_of_mass;
		record.linear_damping = body->linear_damping;
		record.angular_damping = body->angular_damping;
		record.mode = body->mode;
		record.linear_damp_mode = body->linear_damp_mode;
		record.angular_damp_mode = body->angular_damp_mode;
		record.omit_force_integration = body->omit_force_integration;
		record.use_static_velocities = body->use_static_velocities;
		record.override_center_of_mass = body->override_center_of_mass;
		record.override_inertia = body->override_inertia;
		record.contact_depth_threshold = body->contact_depth_threshold;
		record.contact_ignore_speculative = body->contact_ignore_speculative;
		record.character_collision_priority = body->character_collision_priority;
		for (uint32_t index = 0; index < body->shapes.size(); ++index) {
			const Box2DShapeInstance &shape = body->shapes[index];
			for (uint32_t native = 0; native < MAX(1U, shape.shape_ids.size()); ++native) {
				ShapeRecord &sr = shapes[si++];
				sr.body = body->rid;
				sr.resource = shape.shape->get_rid();
				sr.instance_index = index;
				sr.native_index = native;
				sr.native_shape = shape.shape_ids.is_empty() ? 0 : b2StoreShapeId(shape.shape_ids[native]);
				sr.transform = shape.transform;
				sr.disabled = shape.disabled;
				sr.one_way = shape.one_way_collision;
				sr.one_way_direction = shape.one_way_direction;
				sr.one_way_margin = shape.one_way_collision_margin;
			}
		}
	}
	SpaceRecord record;
	record.rid = rid;
	record.native_world = b2StoreWorldId(space->world_id);
	record.native_anchor = b2StoreBodyId(space->world_anchor_body);
	record.last_step = space->last_step;
	record.gravity = space->default_gravity;
	record.linear_damp_changed = space->linear_damp_changed;
	record.angular_damp_changed = space->angular_damp_changed;
	record.force_queue_count = space->constant_force_list.size();
	auto *area = space->default_area;
	if (area) {
		record.default_linear_damp = area->get_linear_damp();
		record.default_angular_damp = area->get_angular_damp();
		record.default_gravity_strength = area->get_gravity_strength();
		record.default_gravity_direction = area->get_gravity_direction();
		record.default_gravity_point = area->get_gravity_point_enabled();
		record.default_gravity_distance = area->get_gravity_point_unit_distance();
	}
	record.contact_hertz = space->contact_hertz;
	record.contact_damping_ratio = space->contact_damping_ratio;
	record.contact_max_push_speed = space->contact_max_push_speed;
	record.substeps = space->substeps;
	space_out = record;
	written = required;
	return OK;
}

void Box2DLocalReplay::before_step(Box2DSpace2D *space) {
	space->replay_pending_events = true;
}
void Box2DLocalReplay::after_flush(Box2DSpace2D *space) {
	space->replay_pending_events = false;
}
void Box2DLocalReplay::space_destroyed(Box2DSpace2D *space) {
	for (auto &entry : leases) {
		if (entry.space == space) {
			CRASH_COND(!spB2ReleaseDestroyedCandidate(&entry.candidate));
			entry.space = nullptr;
		}
	}
}
Error Box2DLocalReplay::checkpoint(Box2DPhysicsServer2D *server, RID rid,
		BodyRecord *bodies, uint32_t body_capacity, ShapeRecord *shapes, uint32_t shape_capacity,
		void *staging, size_t staging_capacity, void *image, size_t capacity, SpaceRecord &out, Requirements &written, ContactRecord *contacts, uint32_t contact_capacity) {
	if (!Thread::is_main_thread()) {
		return ERR_BUSY;
	}
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Requirements required;
	Error error = measure(server, rid, required);
	if (error != OK) {
		return error;
	}
	if (body_capacity < required.bodies || shape_capacity < required.shape_records || contact_capacity < required.contacts) {
		return ERR_OUT_OF_MEMORY;
	}
	if ((required.bodies && !bodies) || (required.shape_records && !shapes) || (required.contacts && !contacts)) {
		return ERR_INVALID_PARAMETER;
	}
	auto *space = server->space_owner.get_or_null(rid);
	size_t size = 0;
	if (spB2MeasureSolverImage(space->world_id, &size) != spB2ImageOk) {
		return ERR_INVALID_DATA;
	}
	if (capacity < size || staging_capacity < size) {
		return ERR_OUT_OF_MEMORY;
	}
	if (spB2CaptureSolverImage(space->world_id, staging, staging_capacity, image, capacity, &size) != spB2ImageOk) {
		return ERR_INVALID_DATA;
	}
	error = capture(server, rid, bodies, body_capacity, shapes, shape_capacity, out, written, contacts, contact_capacity);
	CRASH_COND(error != OK); // Same owner/lock, no callback or mutation intervenes.
	out.image_bytes = size;
	out.image_hash = image_hash(image, size);
	return OK;
}
Error Box2DLocalReplay::restore(Box2DPhysicsServer2D *server, const SpaceRecord &checkpoint,
		const BodyRecord *bodies, uint32_t body_count, const ShapeRecord *shapes, uint32_t shape_count,
		const void *image, size_t bytes, size_t budget, const ContactRecord *contacts, uint32_t contact_count) {
	if (!Thread::is_main_thread()) {
		return ERR_BUSY;
	}
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Requirements required;
	Error error = measure(server, checkpoint.rid, required);
	if (error != OK) {
		return error;
	}
	if (body_count != required.bodies || shape_count != required.shape_records ||
			(body_count && !bodies) || (shape_count && !shapes) || (contact_count && !contacts) || !image || bytes != checkpoint.image_bytes ||
			image_hash(image, bytes) != checkpoint.image_hash) {
		return ERR_INVALID_DATA;
	}
	if (body_count > SP_B2_LOCAL_BINDING_LIMIT - 2 || required.native_shapes > SP_B2_LOCAL_BINDING_LIMIT - 2 - body_count) {
		return ERR_OUT_OF_MEMORY;
	}
	auto *space = server->space_owner.get_or_null(checkpoint.rid);
	if (space->default_gravity != checkpoint.gravity) {
		return ERR_INVALID_DATA;
	}
	auto *area = space->default_area;
	if (!area || area->get_linear_damp() != checkpoint.default_linear_damp || area->get_angular_damp() != checkpoint.default_angular_damp ||
			area->get_gravity_strength() != checkpoint.default_gravity_strength || area->get_gravity_direction() != checkpoint.default_gravity_direction ||
			area->get_gravity_point_enabled() != checkpoint.default_gravity_point || area->get_gravity_point_unit_distance() != checkpoint.default_gravity_distance ||
			space->contact_hertz != checkpoint.contact_hertz || space->contact_damping_ratio != checkpoint.contact_damping_ratio ||
			space->contact_max_push_speed != checkpoint.contact_max_push_speed || space->substeps != checkpoint.substeps) {
		return ERR_INVALID_DATA;
	}
	if (checkpoint.force_queue_count > space->constant_force_list.get_capacity()) {
		return ERR_OUT_OF_MEMORY;
	}
	SpaceLease *lease = nullptr;
	for (auto &entry : leases) {
		if (entry.space == space) {
			lease = &entry;
			break;
		}
	}
	if (!lease) {
		for (auto &entry : leases) {
			if (!entry.space) {
				lease = &entry;
				break;
			}
		}
	}
	if (!lease) {
		return ERR_OUT_OF_MEMORY;
	}
	// Fixed stack metadata is bounded before any candidate allocation. All
	// pointer owners remain live under the physics server mutex.
	spB2LocalBinding bindings[SP_B2_LOCAL_BINDING_LIMIT];
	uint64_t mapped[SP_B2_LOCAL_BINDING_LIMIT];
	uint32_t count = 0;
	uint32_t force_count = 0, checked_contacts = 0;
	for (uint32_t i = 0; i < body_count; ++i) {
		const auto &r = bodies[i];
		auto *b = server->body_owner.get_or_null(r.rid);
		if (b2LoadBodyId(r.native_body).world0 != b2LoadWorldId(checkpoint.native_world).index1 - 1) {
			return ERR_INVALID_DATA;
		}
		if (!b || b->space != space || b->_is_freed || b->generation != r.wrapper_generation ||
				!same_id(b2StoreBodyId(b->body_id), r.native_body)) {
			return ERR_INVALID_DATA;
		}
		for (uint32_t j = 0; j < i; ++j) {
			if (bodies[j].rid == r.rid) {
				return ERR_INVALID_DATA;
			}
		}
		if (std::memcmp(&b->body_def, &r.definition, sizeof(r.definition)) ||
				std::memcmp(&b->shape_def, &r.shape_definition, sizeof(r.shape_definition)) ||
				std::memcmp(&b->mass_data, &r.mass_data, sizeof(r.mass_data)) ||
				b->mass != r.mass || b->inertia != r.inertia || b->center_of_mass != r.center_of_mass ||
				b->linear_damping != r.linear_damping || b->angular_damping != r.angular_damping ||
				b->mode != r.mode || b->linear_damp_mode != r.linear_damp_mode || b->angular_damp_mode != r.angular_damp_mode ||
				b->omit_force_integration != r.omit_force_integration || b->use_static_velocities != r.use_static_velocities ||
				b->override_center_of_mass != r.override_center_of_mass || b->override_inertia != r.override_inertia ||
				b->contact_depth_threshold != r.contact_depth_threshold || b->contact_ignore_speculative != r.contact_ignore_speculative ||
				b->character_collision_priority != r.character_collision_priority) {
			return ERR_INVALID_DATA;
		}
		if (r.contact_offset != checked_contacts || r.contact_count > contact_count - checked_contacts) {
			return ERR_INVALID_DATA;
		}
		if (r.contact_count > b->contacts.get_capacity()) {
			return ERR_OUT_OF_MEMORY;
		}
		for (uint32_t j = 0; j < r.contact_count; ++j) {
			const auto &c = contacts[checked_contacts + j];
			auto *other = server->body_owner.get_or_null(c.body);
			if (!other || other->space != space || other->_is_freed || other->generation != c.generation ||
					c.collider != c.body || c.collider_instance_id != other->get_instance_id() || c.local_shape < 0 ||
					uint32_t(c.local_shape) >= b->shapes.size() || c.collider_shape < 0 ||
					uint32_t(c.collider_shape) >= other->shapes.size()) {
				return ERR_INVALID_DATA;
			}
		}
		checked_contacts += r.contact_count;
		if (r.force_queue_index != UINT32_MAX) {
			if (r.force_queue_index >= checkpoint.force_queue_count) {
				return ERR_INVALID_DATA;
			}
			for (uint32_t j = 0; j < i; ++j) {
				if (bodies[j].force_queue_index == r.force_queue_index) {
					return ERR_INVALID_DATA;
				}
			}
			++force_count;
		}
		if ((!r.constant_force.is_zero_approx() || !Math::is_zero_approx(r.constant_torque) || !r.static_linear_velocity.is_zero_approx() || !Math::is_zero_approx(r.static_angular_velocity)) != (r.force_queue_index != UINT32_MAX)) {
			return ERR_INVALID_DATA;
		}
		bindings[count++] = { spB2BindingBody, b2StoreBodyId(b->body_id), b };
	}
	if (checked_contacts != contact_count) {
		return ERR_INVALID_DATA;
	}
	if (force_count != checkpoint.force_queue_count) {
		return ERR_INVALID_DATA;
	}
	for (uint32_t i = 0; i < shape_count; ++i) {
		const auto &r = shapes[i];
		auto *b = server->body_owner.get_or_null(r.body);
		if (r.native_shape && b2LoadShapeId(r.native_shape).world0 != b2LoadWorldId(checkpoint.native_world).index1 - 1) {
			return ERR_INVALID_DATA;
		}
		if (!b || b->space != space || r.instance_index >= b->shapes.size()) {
			return ERR_INVALID_DATA;
		}
		const auto &s = b->shapes[r.instance_index];
		if (s.shape->get_rid() != r.resource || s.transform != r.transform || s.disabled != r.disabled ||
				s.one_way_collision != r.one_way || s.one_way_direction != r.one_way_direction || s.one_way_collision_margin != r.one_way_margin) {
			return ERR_INVALID_DATA;
		}
		for (uint32_t j = 0; j < i; ++j) {
			if (shapes[j].body == r.body && shapes[j].instance_index == r.instance_index && shapes[j].native_index == r.native_index) {
				return ERR_INVALID_DATA;
			}
		}
		if (!r.native_shape) {
			if (!s.shape_ids.is_empty() || r.native_index) {
				return ERR_INVALID_DATA;
			}
			continue;
		}
		if (r.native_index >= s.shape_ids.size() || !same_id(b2StoreShapeId(s.shape_ids[r.native_index]), r.native_shape)) {
			return ERR_INVALID_DATA;
		}
		bindings[count++] = { spB2BindingShape, b2StoreShapeId(s.shape_ids[r.native_index]), const_cast<Box2DShapeInstance *>(&s) };
	}
	if (B2_IS_NON_NULL(space->world_anchor_body)) {
		if (!same_id(b2StoreBodyId(space->world_anchor_body), checkpoint.native_anchor)) {
			return ERR_INVALID_DATA;
		}
		bindings[count++] = { spB2BindingBody, b2StoreBodyId(space->world_anchor_body), nullptr };
	} else if (checkpoint.native_anchor) {
		return ERR_INVALID_DATA;
	}
	if (space->default_area && B2_IS_NON_NULL(space->default_area->body_id)) {
		bindings[count++] = { spB2BindingBody, b2StoreBodyId(space->default_area->body_id), space->default_area };
	}
	spB2ReservedCandidate candidate = {};
	auto status = spB2CreateReservedCandidate(space->world_id, static_cast<const uint8_t *>(image), bytes, budget, &candidate);
	if (status != spB2ReserveOk) {
		return status == spB2ReserveBusy ? ERR_BUSY : status == spB2ReserveInvalid ? ERR_INVALID_DATA
																				   : ERR_OUT_OF_MEMORY;
	}
	struct Abort {
		spB2ReservedCandidate *c;
		~Abort() {
			if (c->world.index1) {
				CRASH_COND(!spB2DestroyReservedCandidate(c));
			}
		}
	} abort{ &candidate };
	if (!spB2ParticipantSameTopology(space->world_id, candidate.world)) {
		return ERR_INVALID_DATA;
	}
	size_t bound = 0;
	if (spB2PrepareLocalBindings(space->world_id, candidate.world, bindings, count, mapped, SP_B2_LOCAL_BINDING_LIMIT, &bound) != spB2BindingOk || bound != count) {
		return ERR_INVALID_DATA;
	}
	if (!spB2ParticipantCopyWiring(space->world_id, candidate.world)) {
		return ERR_INVALID_DATA;
	}
	if (spB2CommitLocalBindings(space->world_id, candidate.world, bindings, mapped, count) != spB2BindingOk) {
		return ERR_INVALID_DATA;
	}
	// Everything after here is an allocation-free, callback-free publication.
	const b2WorldId previous = space->world_id;
	auto previous_lease = lease->candidate;
	space->world_id = candidate.world;
	lease->space = space;
	lease->candidate = candidate;
	candidate = {};
	uint32_t cursor = 0;
	for (uint32_t i = 0; i < body_count; ++i) {
		const auto &r = bodies[i];
		auto *b = server->body_owner.get_or_null(r.rid);
		b->body_id = b2LoadBodyId(mapped[cursor++]);
		b->current_transform = r.transform;
		b->area_overrides = r.area_overrides;
		b->constant_force = r.constant_force;
		b->constant_torque = r.constant_torque;
		b->total_gravity = r.total_gravity;
		b->total_linear_damp = r.total_linear_damp;
		b->total_angular_damp = r.total_angular_damp;
		b->initial_linear_velocity = r.initial_linear_velocity;
		b->initial_angular_velocity = r.initial_angular_velocity;
		b->static_linear_velocity = r.static_linear_velocity;
		b->static_angular_velocity = r.static_angular_velocity;
		b->contacts.clear();
		for (uint32_t j = 0; j < r.contact_count; ++j) {
			const auto &v = contacts[r.contact_offset + j];
			Box2DBody2D::Contact c;
			c.body = server->body_owner.get_or_null(v.body);
			c.normal_impulse = v.normal_impulse;
			c.local_position = v.local_position;
			c.local_normal = v.local_normal;
			c.depth = v.depth;
			c.local_shape = v.local_shape;
			c.collider_position = v.collider_position;
			c.collider_shape = v.collider_shape;
			c.collider_instance_id = v.collider_instance_id;
			c.collider = v.collider;
			c.collider_velocity = v.collider_velocity;
			c.impulse = v.impulse;
			b->contacts.push_back(c);
		}
		b->sleeping = r.sleeping;
		b->queried_contacts = r.queried_contacts;
		b->in_constant_forces_list = r.force_queue_index != UINT32_MAX;
	}
	for (uint32_t i = 0; i < shape_count; ++i) {
		if (shapes[i].native_shape) {
			const auto &r = shapes[i];
			server->body_owner.get_or_null(r.body)->shapes[r.instance_index].shape_ids[r.native_index] = b2LoadShapeId(mapped[cursor++]);
		}
	}
	if (B2_IS_NON_NULL(space->world_anchor_body)) {
		space->world_anchor_body = b2LoadBodyId(mapped[cursor++]);
	}
	if (space->default_area && B2_IS_NON_NULL(space->default_area->body_id)) {
		space->default_area->body_id = b2LoadBodyId(mapped[cursor++]);
	}
	space->constant_force_list.clear();
	for (uint32_t q = 0; q < checkpoint.force_queue_count; ++q) {
		for (uint32_t i = 0; i < body_count; ++i) {
			if (bodies[i].force_queue_index == q) {
				space->constant_force_list.push_back(server->body_owner.get_or_null(bodies[i].rid));
			}
		}
	}
	space->last_step = checkpoint.last_step;
	space->linear_damp_changed = checkpoint.linear_damp_changed;
	space->angular_damp_changed = checkpoint.angular_damp_changed;
	b2DestroyWorld(previous);
	if (previous_lease.world.index1) {
		CRASH_COND(!spB2ReleaseDestroyedCandidate(&previous_lease));
	}
	return OK;
}
