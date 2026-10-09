// SPDX-License-Identifier: MIT
#pragma once
#include "deterministic_world.h"

#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

class EGPBox3DWorld : public RefCounted {
	GDCLASS(EGPBox3DWorld, RefCounted);
	egp::box3d::DeterministicWorld simulation;
	Thread::ID owner_thread = Thread::get_caller_id();
	Error queue_shape(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, const Vector3 &p_size, int64_t p_type, double p_density, egp::box3d::Operation p_operation);
	Error queue_vector(int64_t p_entity, int64_t p_sequence, const Vector3 &p_value, egp::box3d::Operation p_operation);

protected:
	static void _bind_methods();

public:
	Error configure(int64_t p_tick_rate = 60, int64_t p_substeps = 4, int64_t p_workers = 1, const Vector3 &p_gravity = Vector3(0, -9.8, 0));
	Error queue_create_box(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, const Vector3 &p_half_extents, int64_t p_type = 2, double p_density = 1);
	Error queue_create_sphere(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, double p_radius, int64_t p_type = 2, double p_density = 1);
	Error queue_create_capsule(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, double p_radius, double p_half_height, int64_t p_type = 2, double p_density = 1);
	Error queue_destroy_body(int64_t p_entity, int64_t p_sequence);
	Error queue_impulse(int64_t p_entity, int64_t p_sequence, const Vector3 &p_impulse);
	Error queue_linear_velocity(int64_t p_entity, int64_t p_sequence, const Vector3 &p_velocity);
	Error queue_body_state(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, const Quaternion &p_rotation, const Vector3 &p_linear_velocity, const Vector3 &p_angular_velocity);
	Error apply_queued_commands();
	void clear_pending_commands();
	Error step_tick(int64_t p_expected_tick);
	int64_t get_tick() const;
	int64_t get_body_count() const;
	Dictionary get_body_state(int64_t p_entity) const;
	PackedByteArray capture_snapshot();
	Error restore_snapshot(const PackedByteArray &p_bytes);
	String get_state_hash() const;
	String get_simulation_fingerprint() const;
};
