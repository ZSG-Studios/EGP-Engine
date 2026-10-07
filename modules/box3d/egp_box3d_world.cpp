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
	const Error error = to_error(simulation.configure(uint32_t(rate), uint32_t(steps), uint32_t(workers), to_b3(g)));
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
		return to_error(audit_world->configure(uint32_t(rate), uint32_t(steps), uint32_t(workers), to_b3(g)));
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
