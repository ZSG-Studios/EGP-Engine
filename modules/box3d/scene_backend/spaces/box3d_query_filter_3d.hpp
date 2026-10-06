// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "precompiled.hpp"
#pragma once

#include "../misc/type_conversions.hpp"
#include "box3d_physics_direct_space_state_3d.hpp"

#include <box3d/types.h>

// Builds a b3QueryFilter matching Godot's mask-vs-layer query semantics, plus a
// side-channel RID exclude-set since Box3D has no native per-query RID-exclude list.
// Callers check should_exclude() from inside their b3*ResultFcn/b3CastResultFcn callback
// alongside whatever the callback itself needs.
struct Box3DQueryFilter3D {
	b3QueryFilter filter = godot_to_b3_query_filter(UINT32_MAX);
	HashSet<RID> exclude;
	HashSet<ObjectID> exclude_objects;
	const Box3DPhysicsDirectSpaceState3D *direct_state = nullptr;
	bool collide_with_bodies = true;
	bool collide_with_areas = false;
	bool pick_ray = false;

	Box3DQueryFilter3D() = default;

	Box3DQueryFilter3D(uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) :
			collide_with_bodies(p_collide_with_bodies), collide_with_areas(p_collide_with_areas) {
		set_collision_mask(p_collision_mask);
	}

	void set_collision_mask(uint32_t p_collision_mask) { filter = godot_to_b3_query_filter(p_collision_mask); }

	bool should_exclude(const RID &p_rid) const {
		return exclude.has(p_rid) || (direct_state != nullptr && direct_state->is_body_excluded_from_query(p_rid));
	}
};
