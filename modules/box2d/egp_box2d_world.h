// SPDX-License-Identifier: MIT
#pragma once

#include "deterministic_world_2d.h"

#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/binder_common.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

// Explicit fixed-tick Box2D world for deterministic lockstep and rollback: the 2D
// counterpart of EGPBox3DWorld with the same command, event, query and readback API.
class EGPBox2DWorld : public RefCounted {
	GDCLASS(EGPBox2DWorld, RefCounted);
	egp::box2d::DeterministicWorld2D simulation;
	Thread::ID owner_thread = Thread::get_caller_id();
	Error queue_command(const egp::box2d::Command &p_command);
	Error queue_world_command(int64_t p_sequence, egp::box2d::Command &p_command);

protected:
	static void _bind_methods();

public:
	enum ApplyKind {
		APPLY_FORCE,
		APPLY_FORCE_AT_POINT,
		APPLY_TORQUE,
		APPLY_IMPULSE,
		APPLY_IMPULSE_AT_POINT,
		APPLY_ANGULAR_IMPULSE,
	};
	// Bodies hold records of 6 floats: position xy, angle, linear velocity xy, angular velocity.
	static constexpr int BODY_RECORD = 6;

	Error configure(int64_t p_tick_rate = 60, int64_t p_substeps = 4, int64_t p_workers = 1, const Vector2 &p_gravity = Vector2(0, 980));
	Error queue_create_body(int64_t p_entity, int64_t p_sequence, const Dictionary &p_body, const TypedArray<Dictionary> &p_shapes);
	Error queue_destroy_body(int64_t p_entity, int64_t p_sequence);
	Error queue_set_body(int64_t p_entity, int64_t p_sequence, const Dictionary &p_fields);
	Error queue_add_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index, const Dictionary &p_shape);
	Error queue_set_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index, const Dictionary &p_fields, int64_t p_material_index = -1);
	Error queue_destroy_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index);
	Error queue_joint(int64_t p_joint, int64_t p_sequence, const String &p_type, int64_t p_body_a, int64_t p_body_b, const Dictionary &p_fields);
	Error queue_set_joint(int64_t p_joint, int64_t p_sequence, const Dictionary &p_fields);
	Error queue_destroy_joint(int64_t p_joint, int64_t p_sequence);
	Error queue_apply(int64_t p_entity, int64_t p_sequence, ApplyKind p_kind, const Vector2 &p_value, double p_scalar = 0.0, const Vector2 &p_point = Vector2());
	Error queue_apply_wind(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index, const Vector2 &p_wind, double p_drag, double p_lift);
	Error queue_set_world(int64_t p_sequence, const Dictionary &p_fields);
	Error queue_explode(int64_t p_sequence, const Vector2 &p_position, double p_radius, double p_falloff, double p_impulse_per_length, int64_t p_mask = -1);
	Error apply_queued_commands();
	void clear_pending_commands();
	Error step_tick(int64_t p_expected_tick);
	int64_t get_tick() const;
	int64_t get_body_count() const;
	int64_t get_joint_count() const;
	PackedFloat32Array get_body_states(const PackedInt64Array &p_entities) const;
	Dictionary get_world() const;
	Dictionary get_body(int64_t p_entity) const;
	Dictionary get_shape(int64_t p_entity, int64_t p_shape_index) const;
	Dictionary get_joint(int64_t p_joint) const;
	PackedInt64Array get_entities() const;
	PackedInt64Array get_joint_ids() const;
	Dictionary get_events() const;
	Dictionary cast_rays(const PackedVector2Array &p_origins, const PackedVector2Array &p_translations, int64_t p_mask = -1) const;
	PackedInt64Array overlap_aabb(const Vector2 &p_lower, const Vector2 &p_upper, int64_t p_mask = -1) const;
	PackedInt64Array overlap_shape(const Dictionary &p_shape, const Vector2 &p_position, double p_angle = 0.0, int64_t p_mask = -1) const;
	Dictionary cast_shape(const Dictionary &p_shape, const Vector2 &p_position, double p_angle, const Vector2 &p_translation, int64_t p_mask = -1, int64_t p_ignore_entity = 0) const;
	Dictionary move_capsule(const Vector2 &p_position, const Vector2 &p_point_a, const Vector2 &p_point_b, double p_radius, const Vector2 &p_translation, int64_t p_mask = -1) const;
	PackedByteArray capture_snapshot();
	Error restore_snapshot(const PackedByteArray &p_bytes);
	String get_state_hash() const;
	String get_simulation_fingerprint() const;
	static Dictionary get_field_names(const String &p_set);
};

VARIANT_ENUM_CAST(EGPBox2DWorld::ApplyKind);
