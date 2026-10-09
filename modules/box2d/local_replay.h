/**************************************************************************/
/*  local_replay.h                                                        */
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

#include "core/error/error_list.h"
#include "core/templates/rid.h"

#include "modules/box2d/bodies/box2d_body_2d.h"
#include <mutex>
extern "C" {
#include "thirdparty/box2d/src/local_replay/reservation.h"
}

class Box2DPhysicsServer2D;
class Box2DSpace2D;

// Private engine participant under construction. These are in-process records,
// not portable serialization or a published networking capability. Callers
// provide valid aligned buffers outside all live engine/native world storage.
class Box2DLocalReplay {
public:
	struct Requirements {
		uint32_t bodies = 0;
		uint32_t shape_instances = 0;
		uint32_t native_shapes = 0;
		uint32_t shape_records = 0;
		uint32_t contacts = 0;
		size_t wrapper_bytes = 0;
	};
	struct BodyRecord {
		RID rid;
		uint64_t native_body = 0;
		uint16_t wrapper_generation = 0;
		Transform2D transform;
		b2BodyDef definition;
		b2ShapeDef shape_definition;
		b2MassData mass_data;
		Box2DBody2D::AreaOverrideAccumulator area_overrides;
		Vector2 constant_force;
		real_t constant_torque = 0;
		Vector2 total_gravity;
		real_t total_linear_damp = 0;
		real_t total_angular_damp = 0;
		Vector2 initial_linear_velocity;
		real_t initial_angular_velocity = 0;
		Vector2 static_linear_velocity;
		real_t static_angular_velocity = 0;
		bool sleeping = false;
		bool queried_contacts = false;
		uint32_t force_queue_index = UINT32_MAX;
		uint32_t contact_offset = 0, contact_count = 0;
		real_t mass = 0, inertia = 0, linear_damping = 0, angular_damping = 0;
		Vector2 center_of_mass;
		real_t contact_depth_threshold = 0, character_collision_priority = 0;
		PS2DE::BodyMode mode = PS2DE::BODY_MODE_STATIC;
		PS2DE::BodyDampMode linear_damp_mode = PS2DE::BODY_DAMP_MODE_COMBINE;
		PS2DE::BodyDampMode angular_damp_mode = PS2DE::BODY_DAMP_MODE_COMBINE;
		bool omit_force_integration = false, use_static_velocities = false;
		bool override_center_of_mass = false, override_inertia = false;
		bool contact_ignore_speculative = true;
	};
	struct ContactRecord {
		RID body;
		uint16_t generation = 0;
		real_t normal_impulse = 0;
		Vector2 local_position, local_normal;
		real_t depth = 0;
		int local_shape = 0;
		Vector2 collider_position;
		int collider_shape = 0;
		ObjectID collider_instance_id;
		RID collider;
		Vector2 collider_velocity, impulse;
	};
	struct ShapeRecord {
		RID body;
		RID resource;
		uint32_t instance_index = 0;
		uint32_t native_index = 0;
		uint64_t native_shape = 0;
		Transform2D transform;
		bool disabled = false;
		bool one_way = false;
		Vector2 one_way_direction;
		real_t one_way_margin = 0;
	};
	struct SpaceRecord {
		RID rid;
		uint32_t native_world = 0;
		uint64_t native_anchor = 0;
		real_t last_step = 0;
		Vector2 gravity;
		bool linear_damp_changed = false;
		bool angular_damp_changed = false;
		uint64_t image_hash = 0;
		size_t image_bytes = 0;
		uint32_t force_queue_count = 0;
		real_t default_linear_damp = 0, default_angular_damp = 0, default_gravity_strength = 0;
		Vector2 default_gravity_direction;
		bool default_gravity_point = false;
		real_t default_gravity_distance = 0;
		float contact_hertz = 0, contact_damping_ratio = 0, contact_max_push_speed = 0;
		int substeps = 0;
	};

	// Private same-owner transaction. Inputs, topology and wrapper owners remain
	// immutable until cleanup; no engine iteration/gameplay callback may intervene.
	// The held server mutex excludes other threads, not same-thread reentry.
	struct PreparedRestore {
		PreparedRestore() noexcept = default;
		~PreparedRestore();
		PreparedRestore(const PreparedRestore &) = delete;
		PreparedRestore &operator=(const PreparedRestore &) = delete;
		bool active() const noexcept { return owner_lock.owns_lock(); }
	private:
		friend class Box2DLocalReplay;
		std::unique_lock<std::recursive_mutex> owner_lock;
		Box2DPhysicsServer2D *server = nullptr;
		Box2DSpace2D *space = nullptr;
		void *lease = nullptr;
		SpaceRecord checkpoint;
		const BodyRecord *bodies = nullptr;
		const ShapeRecord *shapes = nullptr;
		const ContactRecord *contacts = nullptr;
		uint32_t body_count = 0, shape_count = 0;
		uint64_t mapped[4096] = {};
		spB2ReservedCandidate candidate = {}, retired_candidate = {};
		b2WorldId retired_world = {};
		bool staged = false;
	};

	// Private local profile: actual rigid-body wrappers, a default
	// area, fixed topology, serial solver; no area callbacks, joints, pending
	// delete queue, undrained hit/joint events or exceptions. Cached rigid-body
	// contacts use explicit RID/generation records and prechecked retained storage.
	// Unsupported states are rejected before any output is written.
	static Error measure(Box2DPhysicsServer2D *server, RID space, Requirements &out);
	static Error capture(Box2DPhysicsServer2D *server, RID space,
			BodyRecord *bodies, uint32_t body_capacity,
			ShapeRecord *shapes, uint32_t shape_capacity,
			SpaceRecord &space_out, Requirements &written,
			ContactRecord *contacts = nullptr, uint32_t contact_capacity = 0);

	// Trusted, same-process checkpoint. Capture and native image share the same
	// server lock and drained event boundary. All buffers are caller-owned.
	static Error checkpoint(Box2DPhysicsServer2D *server, RID space,
			BodyRecord *bodies, uint32_t body_capacity,
			ShapeRecord *shapes, uint32_t shape_capacity,
			void *image_staging, size_t staging_capacity, void *image, size_t image_capacity,
			SpaceRecord &space_out, Requirements &written,
			ContactRecord *contacts = nullptr, uint32_t contact_capacity = 0);
	// Admission currently requires unchanged topology/configuration and empty
	// delete/integration/area/effect queues. Constant-force ordering is restored.
	// No public recovery capability is registered by this private participant.
	static Error restore(Box2DPhysicsServer2D *server, const SpaceRecord &checkpoint,
			const BodyRecord *bodies, uint32_t body_count,
			const ShapeRecord *shapes, uint32_t shape_count,
			const void *image, size_t image_bytes, size_t native_budget,
			const ContactRecord *contacts = nullptr, uint32_t contact_count = 0);
	static Error prepare_restore(Box2DPhysicsServer2D *server, const SpaceRecord &checkpoint,
			const BodyRecord *bodies, uint32_t body_count, const ShapeRecord *shapes, uint32_t shape_count,
			const void *image, size_t image_bytes, size_t native_budget, PreparedRestore &prepared,
			spB2CandidateAllocator allocator = {}, const ContactRecord *contacts = nullptr, uint32_t contact_count = 0);
	// Allocation/callback-free publication; caller supplies the qualified owner
	// phase barrier. Old-world teardown occurs only in cleanup after publication.
	static void commit_restore(PreparedRestore &prepared) noexcept;
	// Abort before publication, or retire the superseded world after publication.
	static void abort_restore(PreparedRestore &prepared) noexcept;
	static void before_step(Box2DSpace2D *space);
	static void after_flush(Box2DSpace2D *space);
	static void space_destroyed(Box2DSpace2D *space);
};
