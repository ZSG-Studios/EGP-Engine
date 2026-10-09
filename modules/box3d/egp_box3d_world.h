/**************************************************************************/
/*  egp_box3d_world.h                                                     */
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

#pragma once
#include "deterministic_world.h"

#include "core/object/ref_counted.h"
#include "core/variant/binder_common.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include <memory>

class EGPBox3DWorld : public RefCounted {
	GDCLASS(EGPBox3DWorld, RefCounted);
	egp::box3d::DeterministicWorld simulation;
	std::unique_ptr<egp::box3d::DeterministicWorld> audit_world;
	int64_t audit_steps = 0;
	int64_t audit_boundaries = 0;
	bool audit_failed = false;
	Error queue_command(const egp::box3d::Command &p_command);
	Error verify_audit();
	Thread::ID owner_thread = Thread::get_caller_id();
	Error queue_shape(int64_t p_entity, int64_t p_sequence, const Vector3 &p_position, const Vector3 &p_size, int64_t p_type, double p_density, egp::box3d::Operation p_operation);
	Error queue_vector(int64_t p_entity, int64_t p_sequence, const Vector3 &p_value, egp::box3d::Operation p_operation);
	Error queue_world_command(int64_t p_sequence, egp::box3d::Command &p_command);

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
	enum JointKind {
		JOINT_DISTANCE,
		JOINT_SPHERICAL,
		JOINT_PRISMATIC,
	};
	Error queue_create_joint(int64_t p_joint, int64_t p_sequence, int64_t p_kind, int64_t p_body_a, int64_t p_body_b, const Vector3 &p_anchor_a, const Vector3 &p_anchor_b, const Dictionary &p_options = Dictionary());
	int64_t get_joint_count() const;
	Error queue_destroy_joint(int64_t p_joint, int64_t p_sequence);
	// Batched access: one native call per tick instead of one per body. Each record is
	// 13 floats: position xyz, rotation xyzw, linear velocity xyz, angular velocity xyz.
	static constexpr int BODY_RECORD = 13;
	PackedFloat32Array get_body_states(const PackedInt64Array &p_entities) const;
	Error queue_body_states(const PackedInt64Array &p_entities, int64_t p_sequence, const PackedFloat32Array &p_records);
	Error queue_impulses(const PackedInt64Array &p_entities, int64_t p_sequence, const PackedFloat32Array &p_impulses);
	// Box3D queries (deterministic, read-only) and fluid volumes.
	Dictionary cast_rays(const PackedVector3Array &p_origins, const PackedVector3Array &p_translations, int64_t p_mask = -1) const;
	Dictionary move_capsule(const Vector3 &p_position, double p_half_height, double p_radius, const Vector3 &p_translation, int64_t p_ignore_entity) const;
	Error configure_fluid(const Vector3 &p_lower, const Vector3 &p_upper, double p_density, double p_linear_drag, double p_angular_drag);
	Error set_body_buoyant(int64_t p_entity, bool p_enabled);

	// Full Box3D exposure. Fields are Box3D's definition and runtime fields by snake_case
	// name (get_field_names); shapes are Dictionaries with a "type" and geometry keys.
	enum ApplyKind {
		APPLY_FORCE,
		APPLY_FORCE_AT_POINT,
		APPLY_TORQUE,
		APPLY_IMPULSE,
		APPLY_IMPULSE_AT_POINT,
		APPLY_ANGULAR_IMPULSE,
	};
	Error queue_create_body(int64_t p_entity, int64_t p_sequence, const Dictionary &p_body, const TypedArray<Dictionary> &p_shapes);
	Error queue_set_body(int64_t p_entity, int64_t p_sequence, const Dictionary &p_fields);
	Error queue_add_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index, const Dictionary &p_shape);
	Error queue_set_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index, const Dictionary &p_fields);
	Error queue_destroy_shape(int64_t p_entity, int64_t p_sequence, int64_t p_shape_index);
	Error queue_joint(int64_t p_joint, int64_t p_sequence, const String &p_type, int64_t p_body_a, int64_t p_body_b, const Dictionary &p_fields);
	Error queue_set_joint(int64_t p_joint, int64_t p_sequence, const Dictionary &p_fields);
	Error queue_apply(int64_t p_entity, int64_t p_sequence, ApplyKind p_kind, const Vector3 &p_value, const Vector3 &p_point = Vector3());
	Error queue_set_world(int64_t p_sequence, const Dictionary &p_fields);
	Error queue_explode(int64_t p_sequence, const Vector3 &p_position, double p_radius, double p_falloff, double p_impulse_per_area, int64_t p_mask = -1);
	Dictionary get_world() const;
	Dictionary get_body(int64_t p_entity) const;
	Dictionary get_shape(int64_t p_entity, int64_t p_shape_index) const;
	Dictionary get_joint(int64_t p_joint) const;
	PackedInt64Array get_entities() const;
	PackedInt64Array get_joint_ids() const;
	Dictionary get_events() const;
	PackedInt64Array overlap_aabb(const Vector3 &p_lower, const Vector3 &p_upper, int64_t p_mask = -1) const;
	PackedInt64Array overlap_shape(const Dictionary &p_shape, const Vector3 &p_position, const Quaternion &p_rotation = Quaternion(), int64_t p_mask = -1) const;
	Dictionary cast_shape(const Dictionary &p_shape, const Vector3 &p_position, const Quaternion &p_rotation, const Vector3 &p_translation, int64_t p_mask = -1, int64_t p_ignore_entity = 0) const;
	static Dictionary get_field_names(const String &p_set);

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

VARIANT_ENUM_CAST(EGPBox3DWorld::JointKind);
VARIANT_ENUM_CAST(EGPBox3DWorld::ApplyKind);
