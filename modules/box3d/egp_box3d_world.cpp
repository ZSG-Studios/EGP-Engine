/**************************************************************************/
/*  egp_box3d_world.cpp                                                   */
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

#include "egp_box3d_world.h"
#include "box3d_task_system.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"

#include <cstdio>
#include <cstring>

namespace {
Error to_error(egp::box3d::Result r) {
	using egp::box3d::Result;
	switch (r) {
		case Result::OK:
			return OK;
		case Result::NOT_CONFIGURED:
			return ERR_UNCONFIGURED;
		case Result::PENDING_COMMANDS:
			return ERR_BUSY;
		case Result::LIMIT_REACHED:
			return ERR_OUT_OF_MEMORY;
		case Result::INVALID_SNAPSHOT:
			return ERR_FILE_CORRUPT;
		case Result::DUPLICATE_COMMAND:
			return ERR_ALREADY_EXISTS;
		case Result::INVALID_BATCH:
			return ERR_INVALID_DATA;
		default:
			return ERR_INVALID_PARAMETER;
	}
}
b3Vec3 to_b3(const Vector3 &v) {
	return { float(v.x), float(v.y), float(v.z) };
}
bool valid_ids(int64_t entity, int64_t sequence) {
	return entity > 0 && sequence >= 0 && uint64_t(sequence) <= UINT32_MAX;
}
} // namespace

void EGPBox3DWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "tick_rate", "substeps", "worker_count", "gravity"), &EGPBox3DWorld::configure, DEFVAL(60), DEFVAL(4), DEFVAL(1), DEFVAL(Vector3(0, -9.8, 0)));
	ClassDB::bind_method(D_METHOD("queue_create_box", "entity_id", "sequence", "position", "half_extents", "body_type", "density"), &EGPBox3DWorld::queue_create_box, DEFVAL(2), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("queue_create_sphere", "entity_id", "sequence", "position", "radius", "body_type", "density"), &EGPBox3DWorld::queue_create_sphere, DEFVAL(2), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("queue_create_capsule", "entity_id", "sequence", "position", "radius", "half_height", "body_type", "density"), &EGPBox3DWorld::queue_create_capsule, DEFVAL(2), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("queue_destroy_body", "entity_id", "sequence"), &EGPBox3DWorld::queue_destroy_body);
	ClassDB::bind_method(D_METHOD("queue_impulse", "entity_id", "sequence", "impulse"), &EGPBox3DWorld::queue_impulse);
	ClassDB::bind_method(D_METHOD("queue_linear_velocity", "entity_id", "sequence", "velocity"), &EGPBox3DWorld::queue_linear_velocity);
	ClassDB::bind_method(D_METHOD("queue_body_state", "entity_id", "sequence", "position", "rotation", "linear_velocity", "angular_velocity"), &EGPBox3DWorld::queue_body_state);
	ClassDB::bind_method(D_METHOD("queue_create_joint", "joint_id", "sequence", "kind", "body_a", "body_b", "anchor_a", "anchor_b", "options"), &EGPBox3DWorld::queue_create_joint, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_joint_count"), &EGPBox3DWorld::get_joint_count);
	ClassDB::bind_method(D_METHOD("queue_destroy_joint", "joint_id", "sequence"), &EGPBox3DWorld::queue_destroy_joint);
	ClassDB::bind_method(D_METHOD("get_body_states", "entity_ids"), &EGPBox3DWorld::get_body_states);
	ClassDB::bind_method(D_METHOD("cast_rays", "origins", "translations", "mask"), &EGPBox3DWorld::cast_rays, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("queue_create_body", "entity_id", "sequence", "body", "shapes"), &EGPBox3DWorld::queue_create_body);
	ClassDB::bind_method(D_METHOD("queue_set_body", "entity_id", "sequence", "fields"), &EGPBox3DWorld::queue_set_body);
	ClassDB::bind_method(D_METHOD("queue_add_shape", "entity_id", "sequence", "shape_index", "shape"), &EGPBox3DWorld::queue_add_shape);
	ClassDB::bind_method(D_METHOD("queue_set_shape", "entity_id", "sequence", "shape_index", "fields", "material_index"), &EGPBox3DWorld::queue_set_shape, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("queue_destroy_shape", "entity_id", "sequence", "shape_index"), &EGPBox3DWorld::queue_destroy_shape);
	ClassDB::bind_method(D_METHOD("queue_joint", "joint_id", "sequence", "type", "body_a", "body_b", "fields"), &EGPBox3DWorld::queue_joint, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("queue_set_joint", "joint_id", "sequence", "fields"), &EGPBox3DWorld::queue_set_joint);
	ClassDB::bind_method(D_METHOD("queue_apply", "entity_id", "sequence", "kind", "value", "point"), &EGPBox3DWorld::queue_apply, DEFVAL(Vector3()));
	ClassDB::bind_method(D_METHOD("queue_set_world", "sequence", "fields"), &EGPBox3DWorld::queue_set_world);
	ClassDB::bind_method(D_METHOD("queue_explode", "sequence", "position", "radius", "falloff", "impulse_per_area", "mask"), &EGPBox3DWorld::queue_explode, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("get_world"), &EGPBox3DWorld::get_world);
	ClassDB::bind_method(D_METHOD("get_body", "entity_id"), &EGPBox3DWorld::get_body);
	ClassDB::bind_method(D_METHOD("get_shape", "entity_id", "shape_index"), &EGPBox3DWorld::get_shape);
	ClassDB::bind_method(D_METHOD("get_joint", "joint_id"), &EGPBox3DWorld::get_joint);
	ClassDB::bind_method(D_METHOD("get_entities"), &EGPBox3DWorld::get_entities);
	ClassDB::bind_method(D_METHOD("get_joint_ids"), &EGPBox3DWorld::get_joint_ids);
	ClassDB::bind_method(D_METHOD("get_events"), &EGPBox3DWorld::get_events);
	ClassDB::bind_method(D_METHOD("overlap_aabb", "lower", "upper", "mask"), &EGPBox3DWorld::overlap_aabb, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("overlap_shape", "shape", "position", "rotation", "mask"), &EGPBox3DWorld::overlap_shape, DEFVAL(Quaternion()), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("cast_shape", "shape", "position", "rotation", "translation", "mask", "ignore_entity"), &EGPBox3DWorld::cast_shape, DEFVAL(-1), DEFVAL(0));
	ClassDB::bind_static_method("EGPBox3DWorld", D_METHOD("get_field_names", "set"), &EGPBox3DWorld::get_field_names);
	BIND_ENUM_CONSTANT(APPLY_FORCE);
	BIND_ENUM_CONSTANT(APPLY_FORCE_AT_POINT);
	BIND_ENUM_CONSTANT(APPLY_TORQUE);
	BIND_ENUM_CONSTANT(APPLY_IMPULSE);
	BIND_ENUM_CONSTANT(APPLY_IMPULSE_AT_POINT);
	BIND_ENUM_CONSTANT(APPLY_ANGULAR_IMPULSE);
	ClassDB::bind_method(D_METHOD("move_capsule", "position", "half_height", "radius", "translation", "ignore_entity"), &EGPBox3DWorld::move_capsule, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("configure_fluid", "lower", "upper", "density", "linear_drag", "angular_drag"), &EGPBox3DWorld::configure_fluid, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("set_body_buoyant", "entity_id", "enabled"), &EGPBox3DWorld::set_body_buoyant, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("queue_body_states", "entity_ids", "sequence", "records"), &EGPBox3DWorld::queue_body_states);
	ClassDB::bind_method(D_METHOD("queue_impulses", "entity_ids", "sequence", "impulses"), &EGPBox3DWorld::queue_impulses);
	BIND_ENUM_CONSTANT(JOINT_DISTANCE);
	BIND_ENUM_CONSTANT(JOINT_SPHERICAL);
	BIND_ENUM_CONSTANT(JOINT_PRISMATIC);
	ClassDB::bind_method(D_METHOD("apply_queued_commands"), &EGPBox3DWorld::apply_queued_commands);
	ClassDB::bind_method(D_METHOD("clear_pending_commands"), &EGPBox3DWorld::clear_pending_commands);
	ClassDB::bind_method(D_METHOD("step_tick", "expected_tick"), &EGPBox3DWorld::step_tick);
	ClassDB::bind_method(D_METHOD("get_tick"), &EGPBox3DWorld::get_tick);
	ClassDB::bind_method(D_METHOD("get_body_count"), &EGPBox3DWorld::get_body_count);
	ClassDB::bind_method(D_METHOD("get_body_state", "entity_id"), &EGPBox3DWorld::get_body_state);
	ClassDB::bind_method(D_METHOD("capture_snapshot"), &EGPBox3DWorld::capture_snapshot);
	ClassDB::bind_method(D_METHOD("restore_snapshot", "bytes"), &EGPBox3DWorld::restore_snapshot);
	ClassDB::bind_method(D_METHOD("get_state_hash"), &EGPBox3DWorld::get_state_hash);
	ClassDB::bind_method(D_METHOD("get_simulation_fingerprint"), &EGPBox3DWorld::get_simulation_fingerprint);
}

Error EGPBox3DWorld::configure(int64_t rate, int64_t steps, int64_t workers, const Vector3 &g) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(rate < 1 || rate > 240 || steps < 1 || steps > 16 || workers < 1 || workers > B3_MAX_WORKERS, ERR_INVALID_PARAMETER);
	// Multi-worker solving runs on the engine WorkerThreadPool.
	const Error error = to_error(simulation.configure(uint32_t(rate), uint32_t(steps), uint32_t(workers), to_b3(g), egp::box3d::enqueue_pool_task, egp::box3d::finish_pool_task));
	if (error != OK) {
		return error;
	}
	audit_world.reset();
	audit_steps = audit_boundaries = 0;
	audit_failed = false;
	set_meta("box3d_audit_verified_steps", int64_t(0));
	set_meta("box3d_audit_verified_boundaries", int64_t(0));
	set_meta("box3d_audit_hash_mismatches", int64_t(0));
	if (bool(GLOBAL_GET("physics/box3d/audit_determinism"))) {
		audit_world = std::make_unique<egp::box3d::DeterministicWorld>();
		return to_error(audit_world->configure(uint32_t(rate), uint32_t(steps), uint32_t(workers), to_b3(g), egp::box3d::enqueue_pool_task, egp::box3d::finish_pool_task));
	}
	return OK;
}
Error EGPBox3DWorld::queue_command(const egp::box3d::Command &command) {
	if (audit_failed) {
		return ERR_INVALID_DATA;
	}
	const auto result = simulation.queue(command);
	if (audit_world && audit_world->queue(command) != result) {
		audit_failed = true;
		set_meta("box3d_audit_hash_mismatches", int64_t(1));
		ERR_PRINT("Box3D determinism audit command result differs.");
		return ERR_INVALID_DATA;
	}
	return to_error(result);
}
Error EGPBox3DWorld::verify_audit() {
	if (!audit_world) {
		return OK;
	}
	if (audit_failed || simulation.get_state_hash() != audit_world->get_state_hash()) {
		audit_failed = true;
		set_meta("box3d_audit_hash_mismatches", int64_t(1));
		ERR_PRINT("Box3D determinism audit state differs at tick " + itos(simulation.get_tick()));
		return ERR_INVALID_DATA;
	}
	set_meta("box3d_audit_verified_boundaries", ++audit_boundaries);
	return OK;
}
Error EGPBox3DWorld::queue_shape(int64_t entity, int64_t sequence, const Vector3 &position, const Vector3 &size, int64_t type, double density, egp::box3d::Operation operation) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || type < 0 || type > 2, ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = operation;
	c.value = to_b3(position);
	c.size = to_b3(size);
	c.body_type = b3BodyType(type);
	c.density = float(density);
	return queue_command(c);
}
Error EGPBox3DWorld::queue_create_box(int64_t entity, int64_t sequence, const Vector3 &position, const Vector3 &size, int64_t type, double density) {
	return queue_shape(entity, sequence, position, size, type, density, egp::box3d::Operation::CREATE_BOX);
}
Error EGPBox3DWorld::queue_create_sphere(int64_t entity, int64_t sequence, const Vector3 &position, double radius, int64_t type, double density) {
	return queue_shape(entity, sequence, position, Vector3(radius, radius, radius), type, density, egp::box3d::Operation::CREATE_SPHERE);
}
Error EGPBox3DWorld::queue_create_capsule(int64_t entity, int64_t sequence, const Vector3 &position, double radius, double height, int64_t type, double density) {
	return queue_shape(entity, sequence, position, Vector3(radius, height, radius), type, density, egp::box3d::Operation::CREATE_CAPSULE);
}
Error EGPBox3DWorld::queue_vector(int64_t entity, int64_t sequence, const Vector3 &value, egp::box3d::Operation operation) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.value = to_b3(value);
	c.operation = operation;
	return queue_command(c);
}
Error EGPBox3DWorld::queue_destroy_body(int64_t entity, int64_t sequence) {
	return queue_vector(entity, sequence, Vector3(), egp::box3d::Operation::DESTROY);
}
Error EGPBox3DWorld::queue_impulse(int64_t entity, int64_t sequence, const Vector3 &value) {
	return queue_vector(entity, sequence, value, egp::box3d::Operation::IMPULSE);
}
Error EGPBox3DWorld::queue_linear_velocity(int64_t entity, int64_t sequence, const Vector3 &value) {
	return queue_vector(entity, sequence, value, egp::box3d::Operation::VELOCITY);
}
Error EGPBox3DWorld::queue_body_state(int64_t entity, int64_t sequence, const Vector3 &position, const Quaternion &rotation, const Vector3 &linear, const Vector3 &angular) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::BODY_STATE;
	c.value = to_b3(position);
	c.rotation = { { float(rotation.x), float(rotation.y), float(rotation.z) }, float(rotation.w) };
	c.linear_velocity = to_b3(linear);
	c.angular_velocity = to_b3(angular);
	return queue_command(c);
}
Error EGPBox3DWorld::queue_create_joint(int64_t joint, int64_t sequence, int64_t kind, int64_t body_a, int64_t body_b, const Vector3 &anchor_a, const Vector3 &anchor_b, const Dictionary &options) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence) || kind < JOINT_DISTANCE || kind > JOINT_PRISMATIC || body_a <= 0 || body_b <= 0, ERR_INVALID_PARAMETER);
	for (const Variant &key : options.keys()) {
		const String name = key;
		ERR_FAIL_COND_V_MSG(name != "length" && name != "hertz" && name != "damping_ratio" && name != "lower" && name != "upper" && name != "enable_spring" && name != "enable_limit" && name != "collide_connected" && name != "axis", ERR_INVALID_PARAMETER, "Unknown EGPBox3DWorld joint option: " + name);
	}
	egp::box3d::Command c;
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::CREATE_JOINT;
	c.joint_kind = egp::box3d::JointKind(kind);
	c.body_a = uint64_t(body_a);
	c.body_b = uint64_t(body_b);
	c.anchor_a = to_b3(anchor_a);
	c.anchor_b = to_b3(anchor_b);
	c.axis = to_b3(Vector3(options.get("axis", Vector3(1, 0, 0))));
	c.length = float(double(options.get("length", 1.0)));
	c.hertz = float(double(options.get("hertz", 0.0)));
	c.damping_ratio = float(double(options.get("damping_ratio", 0.0)));
	c.lower = float(double(options.get("lower", 0.0)));
	c.upper = float(double(options.get("upper", 0.0)));
	c.enable_spring = bool(options.get("enable_spring", false));
	c.enable_limit = bool(options.get("enable_limit", false));
	c.collide_connected = bool(options.get("collide_connected", false));
	return queue_command(c);
}
PackedFloat32Array EGPBox3DWorld::get_body_states(const PackedInt64Array &entities) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedFloat32Array());
	PackedFloat32Array result;
	ERR_FAIL_COND_V(result.resize(int64_t(entities.size()) * BODY_RECORD) != OK, PackedFloat32Array());
	float *out = result.ptrw();
	for (int64_t i = 0; i < entities.size(); ++i) {
		b3WorldTransform t;
		b3Vec3 linear, angular;
		float *record = out + i * BODY_RECORD;
		if (entities[i] <= 0 || !simulation.get_body_state(uint64_t(entities[i]), t, linear, angular)) {
			// Unknown bodies read as NaN so callers can detect them without a second call.
			for (int f = 0; f < BODY_RECORD; ++f) {
				record[f] = NAN;
			}
			continue;
		}
		const float values[BODY_RECORD] = { t.p.x, t.p.y, t.p.z, t.q.v.x, t.q.v.y, t.q.v.z, t.q.s, linear.x, linear.y, linear.z, angular.x, angular.y, angular.z };
		std::memcpy(record, values, sizeof(values));
	}
	return result;
}
Error EGPBox3DWorld::queue_body_states(const PackedInt64Array &entities, int64_t sequence, const PackedFloat32Array &records) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(records.size() != entities.size() * BODY_RECORD, ERR_INVALID_PARAMETER);
	const float *in = records.ptr();
	for (int64_t i = 0; i < entities.size(); ++i) {
		const float *r = in + i * BODY_RECORD;
		const Error error = queue_body_state(entities[i], sequence, Vector3(r[0], r[1], r[2]), Quaternion(r[3], r[4], r[5], r[6]), Vector3(r[7], r[8], r[9]), Vector3(r[10], r[11], r[12]));
		if (error != OK) {
			return error;
		}
	}
	return OK;
}
Error EGPBox3DWorld::queue_impulses(const PackedInt64Array &entities, int64_t sequence, const PackedFloat32Array &impulses) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(impulses.size() != entities.size() * 3, ERR_INVALID_PARAMETER);
	const float *in = impulses.ptr();
	for (int64_t i = 0; i < entities.size(); ++i) {
		const Error error = queue_impulse(entities[i], sequence, Vector3(in[i * 3], in[i * 3 + 1], in[i * 3 + 2]));
		if (error != OK) {
			return error;
		}
	}
	return OK;
}
Error EGPBox3DWorld::queue_destroy_joint(int64_t joint, int64_t sequence) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::DESTROY_JOINT;
	return queue_command(c);
}
int64_t EGPBox3DWorld::get_joint_count() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_joint_count());
}
Error EGPBox3DWorld::apply_queued_commands() {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	if (audit_failed) {
		return ERR_INVALID_DATA;
	}
	const auto result = simulation.apply_queued_commands();
	if (audit_world && audit_world->apply_queued_commands() != result) {
		audit_failed = true;
		return verify_audit();
	}
	return result == egp::box3d::Result::OK ? verify_audit() : to_error(result);
}
void EGPBox3DWorld::clear_pending_commands() {
	ERR_FAIL_COND(Thread::get_caller_id() != owner_thread);
	simulation.clear_pending_commands();
	if (audit_world) {
		audit_world->clear_pending_commands();
	}
}
Dictionary EGPBox3DWorld::cast_rays(const PackedVector3Array &origins, const PackedVector3Array &translations, int64_t mask) const {
	Dictionary result;
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, result);
	ERR_FAIL_COND_V(origins.size() != translations.size(), result);
	const int64_t count = origins.size();
	std::vector<b3Vec3> from(static_cast<size_t>(count)), along(static_cast<size_t>(count));
	for (int64_t i = 0; i < count; ++i) {
		from[size_t(i)] = to_b3(origins[i]);
		along[size_t(i)] = to_b3(translations[i]);
	}
	std::vector<egp::box3d::DeterministicWorld::RayHit> hits(static_cast<size_t>(count));
	simulation.cast_rays(from.data(), along.data(), size_t(count), hits.data(), uint64_t(mask));
	PackedByteArray hit;
	PackedFloat32Array fraction;
	PackedVector3Array point, normal;
	PackedInt64Array entity, shape;
	hit.resize(count);
	shape.resize(count);
	fraction.resize(count);
	point.resize(count);
	normal.resize(count);
	entity.resize(count);
	for (int64_t i = 0; i < count; ++i) {
		const auto &h = hits[size_t(i)];
		hit.set(i, h.hit ? 1 : 0);
		fraction.set(i, h.fraction);
		point.set(i, Vector3(h.point.x, h.point.y, h.point.z));
		normal.set(i, Vector3(h.normal.x, h.normal.y, h.normal.z));
		entity.set(i, int64_t(h.entity));
		shape.set(i, int64_t(h.shape));
	}
	result["hit"] = hit;
	result["fraction"] = fraction;
	result["point"] = point;
	result["normal"] = normal;
	result["entity"] = entity;
	result["shape"] = shape;
	return result;
}
Dictionary EGPBox3DWorld::move_capsule(const Vector3 &position, double half_height, double radius, const Vector3 &translation, int64_t ignore) const {
	Dictionary result;
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, result);
	ERR_FAIL_COND_V(!(half_height >= 0.0) || !(radius > 0.0) || ignore < 0, result);
	b3Vec3 clipped;
	const b3Vec3 moved = simulation.move_capsule(to_b3(position), float(half_height), float(radius), to_b3(translation), uint64_t(ignore), &clipped);
	result["position"] = Vector3(moved.x, moved.y, moved.z);
	result["clipped"] = Vector3(clipped.x, clipped.y, clipped.z);
	return result;
}
Error EGPBox3DWorld::configure_fluid(const Vector3 &lower, const Vector3 &upper, double density, double linear_drag, double angular_drag) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	const auto result = simulation.configure_fluid(to_b3(lower), to_b3(upper), float(density), float(linear_drag), float(angular_drag));
	if (audit_world) {
		audit_world->configure_fluid(to_b3(lower), to_b3(upper), float(density), float(linear_drag), float(angular_drag));
	}
	return to_error(result);
}
Error EGPBox3DWorld::set_body_buoyant(int64_t entity, bool enabled) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(entity <= 0, ERR_INVALID_PARAMETER);
	if (audit_world) {
		audit_world->set_buoyant(uint64_t(entity), enabled);
	}
	return to_error(simulation.set_buoyant(uint64_t(entity), enabled));
}
Error EGPBox3DWorld::step_tick(int64_t expected) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(expected < 1, ERR_INVALID_PARAMETER);
	if (audit_failed) {
		return ERR_INVALID_DATA;
	}
	const auto result = simulation.step_tick(uint64_t(expected));
	if (audit_world && audit_world->step_tick(uint64_t(expected)) != result) {
		audit_failed = true;
		return verify_audit();
	}
	if (result != egp::box3d::Result::OK) {
		return to_error(result);
	}
	const Error error = verify_audit();
	if (error == OK && audit_world) {
		set_meta("box3d_audit_verified_steps", ++audit_steps);
	}
	return error;
}
int64_t EGPBox3DWorld::get_tick() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_tick());
}
int64_t EGPBox3DWorld::get_body_count() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, 0);
	return int64_t(simulation.get_body_count());
}
Dictionary EGPBox3DWorld::get_body_state(int64_t entity) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	Dictionary result;
	b3WorldTransform t;
	b3Vec3 linear, angular;
	if (entity <= 0 || !simulation.get_body_state(uint64_t(entity), t, linear, angular)) {
		return result;
	}
	result["position"] = Vector3(t.p.x, t.p.y, t.p.z);
	result["rotation"] = Quaternion(t.q.v.x, t.q.v.y, t.q.v.z, t.q.s);
	result["linear_velocity"] = Vector3(linear.x, linear.y, linear.z);
	result["angular_velocity"] = Vector3(angular.x, angular.y, angular.z);
	return result;
}
PackedByteArray EGPBox3DWorld::capture_snapshot() {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedByteArray());
	std::vector<uint8_t> bytes;
	ERR_FAIL_COND_V(simulation.capture_snapshot(bytes) != egp::box3d::Result::OK, PackedByteArray());
	PackedByteArray result;
	result.resize(bytes.size());
	std::memcpy(result.ptrw(), bytes.data(), bytes.size());
	return result;
}
Error EGPBox3DWorld::restore_snapshot(const PackedByteArray &bytes) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(bytes.size() < 89 || bytes.size() > egp::box3d::DeterministicWorld::MAX_SNAPSHOT_BYTES, ERR_FILE_CORRUPT);
	std::vector<uint8_t> copy(bytes.ptr(), bytes.ptr() + bytes.size());
	if (audit_failed) {
		return ERR_INVALID_DATA;
	}
	const auto result = simulation.restore_snapshot(copy);
	if (audit_world && audit_world->restore_snapshot(copy) != result) {
		audit_failed = true;
		return verify_audit();
	}
	return result == egp::box3d::Result::OK ? verify_audit() : to_error(result);
}
String EGPBox3DWorld::get_state_hash() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, String());
	char text[17];
	std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(simulation.get_state_hash()));
	return String(text);
}
String EGPBox3DWorld::get_simulation_fingerprint() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, String());
	return String(simulation.get_simulation_fingerprint().c_str());
}

namespace {
namespace bx = egp::box3d;

Variant::Type variant_type(bx::FieldKind kind) {
	switch (kind) {
		case bx::FieldKind::FLOAT:
			return Variant::FLOAT;
		case bx::FieldKind::BOOL:
			return Variant::BOOL;
		case bx::FieldKind::INT:
		case bx::FieldKind::U64:
			return Variant::INT;
		case bx::FieldKind::VEC3:
			return Variant::VECTOR3;
		case bx::FieldKind::QUAT:
			return Variant::QUATERNION;
	}
	return Variant::NIL;
}

bool field_set(const String &name, bx::FieldSet &set) {
	if (name == "world") {
		set = bx::FieldSet::WORLD;
	} else if (name == "body") {
		set = bx::FieldSet::BODY;
	} else if (name == "shape") {
		set = bx::FieldSet::SHAPE;
	} else if (name == "joint") {
		set = bx::FieldSet::JOINT;
	} else {
		return false;
	}
	return true;
}

bool numeric(const Variant &value) {
	return value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT || value.get_type() == Variant::BOOL;
}

bool to_prop(bx::FieldSet set, const bx::FieldInfo &info, const Variant &value, bx::Prop &prop) {
	prop = bx::Prop();
	prop.id = info.id;
	switch (info.kind) {
		case bx::FieldKind::FLOAT:
		case bx::FieldKind::BOOL:
			if (!numeric(value)) {
				return false;
			}
			prop.v[0] = info.kind == bx::FieldKind::BOOL ? (bool(value) ? 1.0 : 0.0) : double(value);
			return true;
		case bx::FieldKind::INT:
			if (set == bx::FieldSet::BODY && value.get_type() == Variant::STRING) {
				// Body type by name.
				const String name = value;
				prop.v[0] = name == "static" ? 0.0 : name == "kinematic" ? 1.0 :
						name == "dynamic"                                ? 2.0 :
																		   -1.0;
				return prop.v[0] >= 0.0;
			}
			if (value.get_type() != Variant::INT) {
				return false;
			}
			prop.v[0] = double(int64_t(value));
			return true;
		case bx::FieldKind::U64:
			if (value.get_type() != Variant::INT) {
				return false;
			}
			prop.bits = uint64_t(int64_t(value));
			return true;
		case bx::FieldKind::VEC3: {
			if (value.get_type() != Variant::VECTOR3 && value.get_type() != Variant::VECTOR3I) {
				return false;
			}
			const Vector3 v = value;
			prop.v[0] = v.x;
			prop.v[1] = v.y;
			prop.v[2] = v.z;
			return true;
		}
		case bx::FieldKind::QUAT: {
			Quaternion q;
			if (value.get_type() == Variant::QUATERNION) {
				q = value;
			} else if (value.get_type() == Variant::BASIS) {
				q = Basis(value).get_rotation_quaternion();
			} else {
				return false;
			}
			prop.v[0] = q.x;
			prop.v[1] = q.y;
			prop.v[2] = q.z;
			prop.v[3] = q.w;
			return true;
		}
	}
	return false;
}

const char *const GEOMETRY_KEYS[] = { "type", "radius", "center", "rotation", "half_height", "point_a", "point_b", "half_extents", "scale", "points", "indices", "heights", "holes", "count_x", "count_z", "children", "materials", "material_indices" };

bool geometry_key(const String &name) {
	for (const char *key : GEOMETRY_KEYS) {
		if (name == key) {
			return true;
		}
	}
	return false;
}

Error to_props(bx::FieldSet set, const Dictionary &fields, bx::Props &props, bool skip_geometry = false) {
	props.clear();
	for (const Variant &key : fields.keys()) {
		const String name = key;
		if (skip_geometry && geometry_key(name)) {
			continue;
		}
		const bx::FieldInfo *info = bx::find_field(set, name.utf8().get_data());
		ERR_FAIL_NULL_V_MSG(info, ERR_INVALID_PARAMETER, "Unknown EGPBox3DWorld field: " + name);
		bx::Prop prop;
		ERR_FAIL_COND_V_MSG(!to_prop(set, *info, fields[key], prop), ERR_INVALID_PARAMETER, "EGPBox3DWorld field " + name + " expects " + Variant::get_type_name(variant_type(info->kind)) + ".");
		props.push_back(prop);
	}
	ERR_FAIL_COND_V_MSG(!bx::validate_props(set, props), ERR_INVALID_PARAMETER, "EGPBox3DWorld fields out of range (finite values, unit rotations, non-negative stiffness, damping, materials and thresholds, angles within PI, positive lengths).");
	return OK;
}

b3Vec3 vec(const Vector3 &v) {
	return { float(v.x), float(v.y), float(v.z) };
}

b3Quat quat(const Variant &value) {
	const Quaternion q = value.get_type() == Variant::BASIS ? Basis(value).get_rotation_quaternion() : Quaternion(value);
	return { { float(q.x), float(q.y), float(q.z) }, float(q.w) };
}

Error to_geometry(const Dictionary &shape, bx::Geometry &g) {
	ERR_FAIL_COND_V_MSG(!shape.has("type"), ERR_INVALID_PARAMETER, "EGPBox3DWorld shape needs a type: sphere, capsule, box, hull, mesh or height_field.");
	const String type = shape["type"];
	ERR_FAIL_COND_V_MSG(!bx::shape_type_from_name(type.utf8().get_data(), g.type), ERR_INVALID_PARAMETER, "Unknown EGPBox3DWorld shape type: " + type);
	g.radius = float(double(shape.get("radius", 0.5)));
	g.center = vec(shape.get("center", Vector3()));
	g.rotation = quat(shape.get("rotation", Quaternion()));
	if (shape.has("half_height")) {
		const float half = float(double(shape["half_height"]));
		g.center1 = { 0.0f, -half, 0.0f };
		g.center2 = { 0.0f, half, 0.0f };
	}
	g.center1 = vec(shape.get("point_a", Vector3(g.center1.x, g.center1.y, g.center1.z)));
	g.center2 = vec(shape.get("point_b", Vector3(g.center2.x, g.center2.y, g.center2.z)));
	g.half_extents = vec(shape.get("half_extents", Vector3(0.5, 0.5, 0.5)));
	g.scale = vec(shape.get("scale", Vector3(1, 1, 1)));
	const PackedVector3Array points = shape.get("points", PackedVector3Array());
	g.points.resize(size_t(points.size()));
	for (int64_t i = 0; i < points.size(); ++i) {
		g.points[size_t(i)] = vec(points[i]);
	}
	const PackedInt32Array indices = shape.get("indices", PackedInt32Array());
	g.indices.assign(indices.ptr(), indices.ptr() + indices.size());
	const PackedFloat32Array heights = shape.get("heights", PackedFloat32Array());
	g.heights.assign(heights.ptr(), heights.ptr() + heights.size());
	const PackedByteArray holes = shape.get("holes", PackedByteArray());
	g.holes.assign(holes.ptr(), holes.ptr() + holes.size());
	g.count_x = int32_t(int64_t(shape.get("count_x", 0)));
	g.count_z = int32_t(int64_t(shape.get("count_z", 0)));
	// Per-triangle (mesh) or per-cell (height field) material tables: extra materials are
	// indices 1.. after the shape's own material.
	const Array materials = shape.get("materials", Array());
	for (int64_t i = 0; i < materials.size(); ++i) {
		ERR_FAIL_COND_V_MSG(materials[i].get_type() != Variant::DICTIONARY, ERR_INVALID_PARAMETER, "EGPBox3DWorld materials are Dictionaries of material fields.");
		bx::Props props;
		const Error error = to_props(bx::FieldSet::SHAPE, materials[i], props);
		if (error != OK) {
			return error;
		}
		g.extra_materials.push_back(bx::material_from(props, b3DefaultSurfaceMaterial()));
	}
	const PackedByteArray material_indices = shape.get("material_indices", PackedByteArray());
	g.material_indices.assign(material_indices.ptr(), material_indices.ptr() + material_indices.size());
	// Baked compound children: geometry plus their own material fields.
	const Array children = shape.get("children", Array());
	for (int64_t i = 0; i < children.size(); ++i) {
		ERR_FAIL_COND_V_MSG(children[i].get_type() != Variant::DICTIONARY, ERR_INVALID_PARAMETER, "EGPBox3DWorld compound children are shape Dictionaries.");
		const Dictionary child = children[i];
		bx::Geometry geometry;
		Error error = to_geometry(child, geometry);
		if (error != OK) {
			return error;
		}
		bx::Props props;
		error = to_props(bx::FieldSet::SHAPE, child, props, true);
		if (error != OK) {
			return error;
		}
		geometry.material = bx::material_from(props, b3DefaultSurfaceMaterial());
		g.children.push_back(std::move(geometry));
	}
	ERR_FAIL_COND_V_MSG(!bx::valid_geometry(g), ERR_INVALID_PARAMETER, "Invalid EGPBox3DWorld " + type + " geometry.");
	return OK;
}

Error to_shape(const Dictionary &shape, bx::ShapeSpec &spec) {
	const Error error = to_geometry(shape, spec.geometry);
	if (error != OK) {
		return error;
	}
	return to_props(bx::FieldSet::SHAPE, shape, spec.props, true);
}

Variant to_variant(const bx::Value &value) {
	switch (value.kind) {
		case bx::FieldKind::FLOAT:
			return value.v[0];
		case bx::FieldKind::BOOL:
			return value.v[0] != 0.0;
		case bx::FieldKind::INT:
			return int64_t(value.v[0]);
		case bx::FieldKind::U64:
			return int64_t(value.bits);
		case bx::FieldKind::VEC3:
			return Vector3(value.v[0], value.v[1], value.v[2]);
		case bx::FieldKind::QUAT:
			return Quaternion(value.v[0], value.v[1], value.v[2], value.v[3]);
	}
	return Variant();
}

Dictionary to_dictionary(const bx::Values &values) {
	Dictionary result;
	for (const bx::Value &value : values) {
		result[String(value.name)] = to_variant(value);
	}
	return result;
}

void append_pair(PackedInt64Array &out, const bx::ShapeRef &a, const bx::ShapeRef &b) {
	out.push_back(int64_t(a.entity));
	out.push_back(int64_t(a.shape));
	out.push_back(int64_t(b.entity));
	out.push_back(int64_t(b.shape));
}

PackedInt64Array to_pairs(const std::vector<bx::StepEvents::Pair> &pairs) {
	PackedInt64Array out;
	for (const auto &pair : pairs) {
		append_pair(out, pair.a, pair.b);
	}
	return out;
}

PackedInt64Array to_refs(const std::vector<bx::ShapeRef> &refs) {
	PackedInt64Array out;
	out.resize(int64_t(refs.size()) * 2);
	for (size_t i = 0; i < refs.size(); ++i) {
		out.set(int64_t(i) * 2, int64_t(refs[i].entity));
		out.set(int64_t(i) * 2 + 1, int64_t(refs[i].shape));
	}
	return out;
}
} // namespace

Error EGPBox3DWorld::queue_create_body(int64_t entity, int64_t sequence, const Dictionary &body, const TypedArray<Dictionary> &shape_list) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || shape_list.size() > int64_t(egp::box3d::DeterministicWorld::MAX_SHAPES_PER_BODY), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::CREATE_BODY;
	Error error = to_props(bx::FieldSet::BODY, body, c.props);
	if (error != OK) {
		return error;
	}
	c.shapes.resize(size_t(shape_list.size()));
	for (int64_t i = 0; i < shape_list.size(); ++i) {
		error = to_shape(shape_list[i], c.shapes[size_t(i)]);
		if (error != OK) {
			return error;
		}
	}
	return queue_command(c);
}

Error EGPBox3DWorld::queue_set_body(int64_t entity, int64_t sequence, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::SET_BODY;
	const Error error = to_props(bx::FieldSet::BODY, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox3DWorld::queue_add_shape(int64_t entity, int64_t sequence, int64_t index, const Dictionary &shape) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index >= int64_t(egp::box3d::DeterministicWorld::MAX_SHAPES_PER_BODY), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::ADD_SHAPE;
	c.shape_index = uint32_t(index);
	c.shapes.resize(1);
	const Error error = to_shape(shape, c.shapes[0]);
	return error != OK ? error : queue_command(c);
}

Error EGPBox3DWorld::queue_set_shape(int64_t entity, int64_t sequence, int64_t index, const Dictionary &fields, int64_t material_index) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index > int64_t(UINT32_MAX), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::SET_SHAPE;
	c.shape_index = uint32_t(index);
	ERR_FAIL_COND_V(material_index < -1 || material_index > 255, ERR_INVALID_PARAMETER);
	c.material_index = int32_t(material_index);
	const Error error = to_props(bx::FieldSet::SHAPE, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox3DWorld::queue_destroy_shape(int64_t entity, int64_t sequence, int64_t index) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || index < 0 || index > int64_t(UINT32_MAX), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::DESTROY_SHAPE;
	c.shape_index = uint32_t(index);
	return queue_command(c);
}

Error EGPBox3DWorld::queue_joint(int64_t joint, int64_t sequence, const String &type, int64_t body_a, int64_t body_b, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence) || body_a <= 0 || body_b <= 0 || body_a == body_b, ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	ERR_FAIL_COND_V_MSG(!bx::joint_type_from_name(type.utf8().get_data(), c.joint_type), ERR_INVALID_PARAMETER, "Unknown EGPBox3DWorld joint type: " + type);
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::CREATE_TYPED_JOINT;
	c.body_a = uint64_t(body_a);
	c.body_b = uint64_t(body_b);
	const Error error = to_props(bx::FieldSet::JOINT, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox3DWorld::queue_set_joint(int64_t joint, int64_t sequence, const Dictionary &fields) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(joint, sequence), ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(joint);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::SET_JOINT;
	const Error error = to_props(bx::FieldSet::JOINT, fields, c.props);
	return error != OK ? error : queue_command(c);
}

Error EGPBox3DWorld::queue_apply(int64_t entity, int64_t sequence, ApplyKind kind, const Vector3 &value, const Vector3 &point) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(!valid_ids(entity, sequence) || kind < APPLY_FORCE || kind > APPLY_ANGULAR_IMPULSE, ERR_INVALID_PARAMETER);
	egp::box3d::Command c;
	c.entity = uint64_t(entity);
	c.sequence = uint32_t(sequence);
	c.operation = egp::box3d::Operation::APPLY;
	c.apply = egp::box3d::ApplyKind(kind);
	c.value = to_b3(value);
	c.point = to_b3(point);
	return queue_command(c);
}

Error EGPBox3DWorld::queue_world_command(int64_t sequence, egp::box3d::Command &c) {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, ERR_BUSY);
	ERR_FAIL_COND_V(sequence < 0 || uint64_t(sequence) > UINT32_MAX, ERR_INVALID_PARAMETER);
	c.entity = egp::box3d::DeterministicWorld::WORLD_KEY;
	c.sequence = uint32_t(sequence);
	return queue_command(c);
}

Error EGPBox3DWorld::queue_set_world(int64_t sequence, const Dictionary &fields) {
	egp::box3d::Command c;
	c.operation = egp::box3d::Operation::SET_WORLD;
	const Error error = to_props(bx::FieldSet::WORLD, fields, c.props);
	return error != OK ? error : queue_world_command(sequence, c);
}

Error EGPBox3DWorld::queue_explode(int64_t sequence, const Vector3 &position, double radius, double falloff, double impulse_per_area, int64_t mask) {
	egp::box3d::Command c;
	c.operation = egp::box3d::Operation::EXPLODE;
	c.value = to_b3(position);
	c.radius = float(radius);
	c.falloff = float(falloff);
	c.impulse_per_area = float(impulse_per_area);
	c.mask = uint64_t(mask);
	return queue_world_command(sequence, c);
}

Dictionary EGPBox3DWorld::get_world() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (!simulation.read_world(values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	result["tick"] = int64_t(simulation.get_tick());
	return result;
}

Dictionary EGPBox3DWorld::get_body(int64_t entity) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (entity <= 0 || !simulation.read_body(uint64_t(entity), values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	PackedInt32Array indices;
	for (const uint32_t index : simulation.get_shape_indices(uint64_t(entity))) {
		indices.push_back(int32_t(index));
	}
	result["shapes"] = indices;
	return result;
}

Dictionary EGPBox3DWorld::get_shape(int64_t entity, int64_t index) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	if (entity <= 0 || index < 0 || index > int64_t(UINT32_MAX) || !simulation.read_shape(uint64_t(entity), uint32_t(index), values)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	// Box3D shape type by name.
	static const char *const TYPES[] = { "capsule", "compound", "height_field", "hull", "mesh", "sphere" };
	const int64_t type = result.get("box3d_type", -1);
	result.erase("box3d_type");
	result["type"] = type >= 0 && type < 6 ? String(TYPES[type]) : String("unknown");
	std::vector<bx::Values> materials;
	if (int64_t(result.get("material_count", 1)) > 1 && simulation.read_shape_materials(uint64_t(entity), uint32_t(index), materials)) {
		Array table;
		for (size_t i = 1; i < materials.size(); ++i) {
			table.push_back(to_dictionary(materials[i]));
		}
		result["materials"] = table;
	}
	return result;
}

Dictionary EGPBox3DWorld::get_joint(int64_t joint) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	bx::Values values;
	bx::JointType type;
	uint64_t body_a = 0, body_b = 0;
	if (joint <= 0 || !simulation.read_joint(uint64_t(joint), values, type, body_a, body_b)) {
		return Dictionary();
	}
	Dictionary result = to_dictionary(values);
	result["type"] = String(bx::joint_type_name(type));
	result["body_a"] = int64_t(body_a);
	result["body_b"] = int64_t(body_b);
	return result;
}

PackedInt64Array EGPBox3DWorld::get_entities() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	for (const uint64_t entity : simulation.get_entities()) {
		result.push_back(int64_t(entity));
	}
	return result;
}

PackedInt64Array EGPBox3DWorld::get_joint_ids() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	PackedInt64Array result;
	for (const uint64_t joint : simulation.get_joint_ids()) {
		result.push_back(int64_t(joint));
	}
	return result;
}

Dictionary EGPBox3DWorld::get_events() const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	const bx::StepEvents &events = simulation.get_events();
	Dictionary result;
	result["contact_begin"] = to_pairs(events.contact_begin);
	result["contact_end"] = to_pairs(events.contact_end);
	result["sensor_begin"] = to_pairs(events.sensor_begin);
	result["sensor_end"] = to_pairs(events.sensor_end);
	PackedInt64Array hits;
	PackedVector3Array points, normals;
	PackedFloat32Array speeds;
	for (const auto &hit : events.contact_hit) {
		append_pair(hits, hit.a, hit.b);
		points.push_back(Vector3(hit.point.x, hit.point.y, hit.point.z));
		normals.push_back(Vector3(hit.normal.x, hit.normal.y, hit.normal.z));
		speeds.push_back(hit.approach_speed);
	}
	result["contact_hit"] = hits;
	result["contact_hit_point"] = points;
	result["contact_hit_normal"] = normals;
	result["contact_hit_speed"] = speeds;
	PackedInt64Array moved, asleep;
	PackedFloat32Array transforms;
	moved.resize(int64_t(events.moved.size()));
	transforms.resize(int64_t(events.moved.size()) * 7);
	float *out = transforms.ptrw();
	for (size_t i = 0; i < events.moved.size(); ++i) {
		const auto &move = events.moved[i];
		moved.set(int64_t(i), int64_t(move.entity));
		const float record[7] = { move.transform.p.x, move.transform.p.y, move.transform.p.z, move.transform.q.v.x, move.transform.q.v.y, move.transform.q.v.z, move.transform.q.s };
		std::memcpy(out + i * 7, record, sizeof(record));
		if (move.fell_asleep) {
			asleep.push_back(int64_t(move.entity));
		}
	}
	result["moved"] = moved;
	result["moved_transforms"] = transforms;
	result["fell_asleep"] = asleep;
	PackedInt64Array joints;
	for (const uint64_t joint : events.joints) {
		joints.push_back(int64_t(joint));
	}
	result["joint_threshold"] = joints;
	return result;
}

PackedInt64Array EGPBox3DWorld::overlap_aabb(const Vector3 &lower, const Vector3 &upper, int64_t mask) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	std::vector<bx::ShapeRef> found;
	simulation.overlap_aabb(to_b3(lower), to_b3(upper), uint64_t(mask), found);
	return to_refs(found);
}

PackedInt64Array EGPBox3DWorld::overlap_shape(const Dictionary &shape, const Vector3 &position, const Quaternion &rotation, int64_t mask) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, PackedInt64Array());
	bx::Geometry geometry;
	ERR_FAIL_COND_V(to_geometry(shape, geometry) != OK, PackedInt64Array());
	std::vector<bx::ShapeRef> found;
	ERR_FAIL_COND_V_MSG(!simulation.overlap_shape(geometry, to_b3(position), quat(rotation), uint64_t(mask), found), PackedInt64Array(), "overlap_shape takes sphere, capsule, box or hull geometry and a unit rotation.");
	return to_refs(found);
}

Dictionary EGPBox3DWorld::cast_shape(const Dictionary &shape, const Vector3 &position, const Quaternion &rotation, const Vector3 &translation, int64_t mask, int64_t ignore) const {
	ERR_FAIL_COND_V(Thread::get_caller_id() != owner_thread, Dictionary());
	ERR_FAIL_COND_V(ignore < 0, Dictionary());
	bx::Geometry geometry;
	ERR_FAIL_COND_V(to_geometry(shape, geometry) != OK, Dictionary());
	egp::box3d::DeterministicWorld::RayHit hit;
	ERR_FAIL_COND_V_MSG(!simulation.cast_shape(geometry, to_b3(position), quat(rotation), to_b3(translation), uint64_t(mask), uint64_t(ignore), hit), Dictionary(), "cast_shape takes sphere, capsule, box or hull geometry and a unit rotation.");
	Dictionary result;
	result["hit"] = hit.hit;
	result["fraction"] = hit.fraction;
	result["point"] = Vector3(hit.point.x, hit.point.y, hit.point.z);
	result["normal"] = Vector3(hit.normal.x, hit.normal.y, hit.normal.z);
	result["entity"] = int64_t(hit.entity);
	result["shape"] = int64_t(hit.shape);
	return result;
}

Dictionary EGPBox3DWorld::get_field_names(const String &set_name) {
	bx::FieldSet set;
	ERR_FAIL_COND_V_MSG(!field_set(set_name, set), Dictionary(), "Field sets are world, body, shape and joint.");
	Dictionary result;
	for (const bx::FieldInfo &info : bx::all_fields(set)) {
		result[String(info.name)] = int64_t(variant_type(info.kind));
	}
	return result;
}
