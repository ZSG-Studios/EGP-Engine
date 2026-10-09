// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#pragma once

#include "box2d_collision_object_2d.h"

#include "modules/box2d/precompiled.h"

using namespace PhysicsServer2DEnums;

class Box2DBody2D;

template <typename T>
bool integrate(T &p_value, const T &p_override_value, PS2DE::AreaSpaceOverrideMode p_mode) {
	switch (p_mode) {
		case PS2DE::AREA_SPACE_OVERRIDE_DISABLED: {
			return false;
		}
		case PS2DE::AREA_SPACE_OVERRIDE_COMBINE: {
			p_value += p_override_value;
			return false;
		}
		case PS2DE::AREA_SPACE_OVERRIDE_COMBINE_REPLACE: {
			p_value += p_override_value;
			return true;
		}
		case PS2DE::AREA_SPACE_OVERRIDE_REPLACE: {
			p_value = p_override_value;
			return true;
		}
		case PS2DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE: {
			p_value = p_override_value;
			return false;
		}
		default:
			ERR_FAIL_V(false);
	}
}

class Box2DArea2D final : public Box2DCollisionObject2D {
public:
	struct ShapePair {
		RID other_rid;
		int other_index = 0;
		int self_index = 0;
		static uint32_t hash(const ShapePair &pair) {
			uint32_t h = hash_murmur3_one_64(pair.other_rid.get_id());
			h = hash_murmur3_one_32(pair.other_index, h);
			return hash_fmix32(hash_murmur3_one_32(pair.self_index, h));
		}
		bool operator==(const ShapePair &b) const { return other_rid == b.other_rid && other_index == b.other_index && self_index == b.self_index; }
		bool operator<(const ShapePair &b) const {
			if (other_rid != b.other_rid) {
				return other_rid.get_id() < b.other_rid.get_id();
			}
			if (other_index != b.other_index) {
				return other_index < b.other_index;
			}
			return self_index < b.self_index;
		}
	};
	struct ObjectAndOverlapCount {
		ObjectID instance_id;
		Type type = RIGIDBODY;
		int count = 0;
	};

	Box2DArea2D();
	~Box2DArea2D();

	void apply_overrides();

	void update_area_step_list();

	void add_overlap(Box2DShapeInstance *p_other_shape, Box2DShapeInstance *p_self_shape);

	void remove_overlap(Box2DShapeInstance *p_other_shape, Box2DShapeInstance *p_self_shape);

	void update_overlaps();

	void report_event(
			Type p_type,
			PS2DE::AreaBodyStatus p_status,
			RID p_other_rid, ObjectID p_other_id,
			int32_t p_other_shape_index,
			int32_t p_self_shape_index);

	bool is_default_area() const;

	void set_gravity_override_mode(PS2DE::AreaSpaceOverrideMode p_mode);
	PS2DE::AreaSpaceOverrideMode get_gravity_override_mode() const { return override_gravity_mode; }

	void set_linear_damp_override_mode(PS2DE::AreaSpaceOverrideMode p_mode);
	PS2DE::AreaSpaceOverrideMode get_linear_damp_override_mode() const { return override_linear_damp_mode; }

	void set_angular_damp_override_mode(PS2DE::AreaSpaceOverrideMode p_mode);
	PS2DE::AreaSpaceOverrideMode get_angular_damp_override_mode() const { return override_angular_damp_mode; }

	void set_linear_damp(real_t p_damp);
	real_t get_linear_damp() const { return linear_damp; }

	void set_angular_damp(real_t p_damp);
	real_t get_angular_damp() const { return angular_damp; }

	void set_gravity_strength(real_t p_strength);
	real_t get_gravity_strength() const { return gravity_strength; }

	void set_gravity_direction(Vector2 p_direction);
	Vector2 get_gravity_direction() const { return gravity_direction; }

	void set_gravity_point_enabled(bool p_enabled);
	bool get_gravity_point_enabled() const { return gravity_point_enabled; }

	void set_gravity_point_unit_distance(real_t p_distance) { gravity_point_unit_distance = p_distance; }
	real_t get_gravity_point_unit_distance() const { return gravity_point_unit_distance; }

	Vector2 compute_gravity(Vector2 p_center_of_mass) const;

	void gravity_changed();

	void set_priority(real_t p_priority);
	int get_priority() const { return priority; }

	void set_monitorable(bool p_monitorable);
	bool get_monitorable() const { return monitorable; }

	void set_body_monitor_callback(const Callable &p_callback) { body_monitor_callback = p_callback; }
	void set_area_monitor_callback(const Callable &p_callback) { area_monitor_callback = p_callback; }

	void shapes_changed() override;

protected:
	void on_added_to_space() override;
	void on_remove_from_space() override;

	// `overlaps` is a HashMap to account for redundant sensor events from compound shapes.
	// value = number of simultaneous overlap events between two shape instances
	HashMap<ShapePair, ObjectAndOverlapCount, ShapePair> overlaps;
	HashMap<RID, int> object_overlap_count;
	bool updating_overlaps = false;

	uint64_t modify_mask_bits(uint32_t p_mask) override;

	int priority = 0;
	bool monitorable = false;
	Callable body_monitor_callback;
	Callable area_monitor_callback;

	/// Pixels per second squared and screen down, matching the space default. Godot Physics keeps
	/// these in meters because its project settings always overwrite them, but an area built
	/// straight through the server API never gets that far.
	real_t gravity_strength = 980.0;
	Vector2 gravity_direction = Vector2(0, 1);
	real_t linear_damp = 0.1;
	real_t angular_damp = 0.1;

	bool gravity_point_enabled = false;
	real_t gravity_point_unit_distance = 0.0;

	bool in_area_step_list = false;

	PS2DE::AreaSpaceOverrideMode override_gravity_mode = PS2DE::AREA_SPACE_OVERRIDE_DISABLED;
	PS2DE::AreaSpaceOverrideMode override_linear_damp_mode = PS2DE::AREA_SPACE_OVERRIDE_DISABLED;
	PS2DE::AreaSpaceOverrideMode override_angular_damp_mode = PS2DE::AREA_SPACE_OVERRIDE_DISABLED;
};