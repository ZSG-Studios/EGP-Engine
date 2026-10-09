#include "../../box3d_task_system.h"
#include "../joints/box3d_joint_impl_3d.hpp"

#include <box3d/constants.h>
// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "../misc/box3d_globals.hpp"
#include "../misc/type_conversions.hpp"
#include "../objects/box3d_area_impl_3d.hpp"
#include "../objects/box3d_body_impl_3d.hpp"
#include "../objects/box3d_physics_direct_body_state_3d.hpp"
#include "../objects/box3d_shaped_object_impl_3d.hpp"
#include "../objects/box3d_soft_body_impl_3d.hpp"
#include "../servers/box3d_physics_server_3d.hpp"
#include "box3d_physics_direct_space_state_3d.hpp"
#include "box3d_space_3d.hpp"
#include "precompiled.hpp"

#include <box3d/box3d.h>

namespace {
constexpr int SUB_STEP_COUNT = 4;
bool soft_contact_filter(b3ShapeId p_a, b3ShapeId p_b, void *) {
	auto *a = box3d_object_cast<Box3DBodyImpl3D>(static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(b3Shape_GetBody(p_a))));
	auto *b = box3d_object_cast<Box3DBodyImpl3D>(static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(b3Shape_GetBody(p_b))));
	if (!a || !b) {
		return true;
	}
	// Called from Box3D workers while the server's simulation lock prevents mutations.
	// Do not acquire the server lock or call a script here.
	auto *soft_a = a->get_soft_body();
	auto *soft_b = b->get_soft_body();
	if (soft_a && soft_a == soft_b) {
		return false;
	}
	return !(soft_a && soft_a->get_exceptions().has(b->get_rid())) &&
			!(soft_b && soft_b->get_exceptions().has(a->get_rid())) &&
			!a->get_soft_exceptions().has(b->get_rid()) && !b->get_soft_exceptions().has(a->get_rid());
}
} // namespace

Box3DSpace3D::Box3DSpace3D() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	b3WorldDef def = b3DefaultWorldDef();
	// Solver tasks run on the engine WorkerThreadPool, like the Box2D backend.
	def.workerCount = box3d_worker_count();
	def.enqueueTask = egp::box3d::enqueue_pool_task;
	def.finishTask = egp::box3d::finish_pool_task;
	contact_hertz = def.contactHertz;
	contact_damping_ratio = def.contactDampingRatio;
	contact_max_push_speed = def.contactSpeed;
	world_id = b3CreateWorld(&def);
	b3World_SetCustomFilterCallback(world_id, soft_contact_filter, this);

	direct_state = memnew(Box3DPhysicsDirectSpaceState3D);
	direct_state->set_space(this);
}

Box3DSpace3D::~Box3DSpace3D() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (direct_state != nullptr) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
	if (B3_IS_NON_NULL(world_id)) {
		b3DestroyWorld(world_id);
		world_id = b3_nullWorldId;
	}
}

b3BodyId Box3DSpace3D::get_world_anchor_body() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	if (B3_IS_NULL(world_anchor_body)) {
		b3BodyDef def = b3DefaultBodyDef();
		def.type = b3_staticBody;
		def.name = "egp-scene-world-anchor";
		world_anchor_body = b3CreateBody(world_id, &def);
	}
	return world_anchor_body;
}

real_t Box3DSpace3D::get_param(PS3DE::SpaceParameter p_param) const {
	switch (p_param) {
		case PS3DE::SPACE_PARAM_CONTACT_RECYCLE_RADIUS:
			return b3World_GetContactRecycleDistance(world_id);
		case PS3DE::SPACE_PARAM_CONTACT_HERTZ:
			return contact_hertz;
		case PS3DE::SPACE_PARAM_CONTACT_DAMPING_RATIO:
			return contact_damping_ratio;
		case PS3DE::SPACE_PARAM_CONTACT_MAX_PUSH_SPEED:
			return contact_max_push_speed;
		case PS3DE::SPACE_PARAM_RESTITUTION_THRESHOLD:
			return b3World_GetRestitutionThreshold(world_id);
		case PS3DE::SPACE_PARAM_MAXIMUM_LINEAR_SPEED:
			return b3World_GetMaximumLinearSpeed(world_id);
		case PS3DE::SPACE_PARAM_SLEEP_ENABLED:
			return b3World_IsSleepingEnabled(world_id);
		case PS3DE::SPACE_PARAM_CONTINUOUS_ENABLED:
			return b3World_IsContinuousEnabled(world_id);
		case PS3DE::SPACE_PARAM_WARM_STARTING_ENABLED:
			return b3World_IsWarmStartingEnabled(world_id);
		case PS3DE::SPACE_PARAM_RESTITUTION_ITERATIONS:
			return b3World_GetRestitutionIterations(world_id);
		case PS3DE::SPACE_PARAM_RESTITUTION_PROPAGATION_ENABLED:
			return b3World_IsRestitutionPropagationEnabled(world_id);
		default:
			ERR_FAIL_V_MSG(0, "Invalid native space parameter.");
	}
}

void Box3DSpace3D::set_param(PS3DE::SpaceParameter p_param, real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	ERR_FAIL_COND(p_value < 0);
	switch (p_param) {
		case PS3DE::SPACE_PARAM_CONTACT_RECYCLE_RADIUS:
			b3World_SetContactRecycleDistance(world_id, (float)p_value);
			break;
		case PS3DE::SPACE_PARAM_CONTACT_HERTZ:
			contact_hertz = p_value;
			b3World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS3DE::SPACE_PARAM_CONTACT_DAMPING_RATIO:
			contact_damping_ratio = p_value;
			b3World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS3DE::SPACE_PARAM_CONTACT_MAX_PUSH_SPEED:
			contact_max_push_speed = p_value;
			b3World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS3DE::SPACE_PARAM_RESTITUTION_THRESHOLD:
			b3World_SetRestitutionThreshold(world_id, (float)p_value);
			break;
		case PS3DE::SPACE_PARAM_MAXIMUM_LINEAR_SPEED:
			ERR_FAIL_COND(p_value <= 0);
			b3World_SetMaximumLinearSpeed(world_id, (float)p_value);
			break;
		case PS3DE::SPACE_PARAM_SLEEP_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b3World_EnableSleeping(world_id, p_value != 0);
			break;
		case PS3DE::SPACE_PARAM_CONTINUOUS_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b3World_EnableContinuous(world_id, p_value != 0);
			break;
		case PS3DE::SPACE_PARAM_WARM_STARTING_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b3World_EnableWarmStarting(world_id, p_value != 0);
			break;
		case PS3DE::SPACE_PARAM_RESTITUTION_ITERATIONS:
			ERR_FAIL_COND(p_value != Math::floor(p_value) || p_value < 1 || p_value > B3_MAX_RESTITUTION_ITERATIONS);
			b3World_SetRestitutionIterations(world_id, (int)p_value);
			break;
		case PS3DE::SPACE_PARAM_RESTITUTION_PROPAGATION_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b3World_EnableRestitutionPropagation(world_id, p_value != 0);
			break;
		default:
			ERR_FAIL_MSG("Invalid native space parameter.");
	}
}

void Box3DSpace3D::set_default_area(Box3DAreaImpl3D *p_area) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	default_area = p_area;
}

void Box3DSpace3D::step(float p_step) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	last_step = p_step;

	if (default_area != nullptr) {
		b3World_SetGravity(world_id, godot_to_b3(default_area->compute_gravity(Vector3())));
	}

	_apply_area_overrides();
	for (Box3DSoftBodyImpl3D *body : box3d_sorted(soft_bodies)) {
		body->pre_step(p_step);
	}

	for (Box3DBodyImpl3D *body : box3d_sorted(bodies)) {
		body->pre_step();
	}

	b3World_Step(world_id, p_step, SUB_STEP_COUNT);
	_cache_native_events();
	_refresh_debug_contacts();

	_pull_body_events();
	_pull_sensor_events();

	// Manifold pointers are only valid until the next step, so cache contacts now.
	for (Box3DBodyImpl3D *body : box3d_sorted(bodies)) {
		body->refresh_contacts();
	}

	// Drained only so Box3D's per-step event bookkeeping stays consistent; joint events are unused.
	b3World_GetContactEvents(world_id);
	b3World_GetJointEvents(world_id);
}

// Godot walks areas highest priority first, so a REPLACE there wins; ties stay stable.
struct AreaPriorityComparator {
	bool operator()(Box3DAreaImpl3D *p_a, Box3DAreaImpl3D *p_b) const {
		if (p_a->get_priority() != p_b->get_priority()) {
			return p_a->get_priority() > p_b->get_priority();
		}
		return p_a->get_rid().get_id() > p_b->get_rid().get_id();
	}
};

Box3DSpace3D::AreaOverrides Box3DSpace3D::compute_area_overrides(Box3DBodyImpl3D *p_body) const {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	AreaOverrides result;

	LocalVector<Box3DAreaImpl3D *> overlapping;
	for (Box3DAreaImpl3D *area : box3d_sorted(areas)) {
		if (area->get_gravity_mode() == PS3DE::AREA_SPACE_OVERRIDE_DISABLED &&
				area->get_linear_damp_mode() == PS3DE::AREA_SPACE_OVERRIDE_DISABLED &&
				area->get_angular_damp_mode() == PS3DE::AREA_SPACE_OVERRIDE_DISABLED) {
			continue;
		}
		if (area->get_overlaps().has(p_body)) {
			overlapping.push_back(area);
		}
	}
	overlapping.sort_custom<AreaPriorityComparator>();

	bool gravity_done = false;
	bool linear_done = false;
	bool angular_done = false;

	for (Box3DAreaImpl3D *area : overlapping) {
		const PS3DE::AreaSpaceOverrideMode gravity_mode = area->get_gravity_mode();
		if (!gravity_done && gravity_mode != PS3DE::AREA_SPACE_OVERRIDE_DISABLED) {
			const Vector3 gravity = area->compute_gravity(p_body->get_transform().origin);
			result.affects_gravity = true;
			switch (gravity_mode) {
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE:
					result.gravity += gravity;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE_REPLACE:
					result.gravity += gravity;
					result.replaces_world_gravity = true;
					gravity_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE:
					result.gravity = gravity;
					result.replaces_world_gravity = true;
					gravity_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE:
					result.gravity = gravity;
					result.replaces_world_gravity = true;
					break;
				default:
					break;
			}
		}

		const PS3DE::AreaSpaceOverrideMode linear_mode = area->get_linear_damp_mode();
		if (!linear_done && linear_mode != PS3DE::AREA_SPACE_OVERRIDE_DISABLED) {
			const real_t damp = area->get_linear_damp();
			switch (linear_mode) {
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE:
					result.linear_damp += damp;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE_REPLACE:
					result.linear_damp += damp;
					linear_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE:
					result.linear_damp = damp;
					linear_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE:
					result.linear_damp = damp;
					break;
				default:
					break;
			}
		}

		const PS3DE::AreaSpaceOverrideMode angular_mode = area->get_angular_damp_mode();
		if (!angular_done && angular_mode != PS3DE::AREA_SPACE_OVERRIDE_DISABLED) {
			const real_t damp = area->get_angular_damp();
			switch (angular_mode) {
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE:
					result.angular_damp += damp;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_COMBINE_REPLACE:
					result.angular_damp += damp;
					angular_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE:
					result.angular_damp = damp;
					angular_done = true;
					break;
				case PS3DE::AREA_SPACE_OVERRIDE_REPLACE_COMBINE:
					result.angular_damp = damp;
					break;
				default:
					break;
			}
		}
	}

	// The default area is the final contribution unless an area stopped that chain.
	// Body REPLACE damping then overrides the area result; COMBINE adds to it.
	if (default_area) {
		if (!linear_done) {
			result.linear_damp += default_area->get_linear_damp();
		}
		if (!angular_done) {
			result.angular_damp += default_area->get_angular_damp();
		}
		if (!gravity_done && default_area->is_point_gravity()) {
			result.gravity += default_area->compute_gravity(p_body->get_transform().origin);
			result.affects_gravity = true;
			gravity_done = true;
		}
	}
	result.replaces_world_gravity = gravity_done;
	if (p_body->get_linear_damp_mode() == PS3DE::BODY_DAMP_MODE_REPLACE) {
		result.linear_damp = p_body->get_linear_damping();
	} else {
		result.linear_damp += p_body->get_linear_damping();
	}
	if (p_body->get_angular_damp_mode() == PS3DE::BODY_DAMP_MODE_REPLACE) {
		result.angular_damp = p_body->get_angular_damping();
	} else {
		result.angular_damp += p_body->get_angular_damping();
	}
	return result;
}

void Box3DSpace3D::set_max_debug_contacts(int p_count) {
	ERR_FAIL_COND(p_count < 0);
	max_debug_contacts = p_count;
	debug_contacts.clear();
}

void Box3DSpace3D::_refresh_debug_contacts() {
	debug_contacts.clear();
	if (max_debug_contacts == 0) {
		return;
	}
	LocalVector<b3ContactData> pairs;
	for (Box3DBodyImpl3D *body : box3d_sorted(bodies)) {
		if (!body->has_body_id()) {
			continue;
		}
		b3BodyId id = body->get_body_id();
		int capacity = b3Body_GetContactCapacity(id);
		if (capacity == 0) {
			continue;
		}
		pairs.resize(capacity);
		int count = b3Body_GetContactData(id, pairs.ptr(), capacity);
		for (int i = 0; i < count; i++) {
			const b3ContactData &pair = pairs[i];
			if (!b3Shape_IsValid(pair.shapeIdA) || !b3Shape_IsValid(pair.shapeIdB) || !B3_ID_EQUALS(b3Shape_GetBody(pair.shapeIdA), id)) {
				continue;
			}
			b3Vec3 center = b3Body_GetWorldCenter(id);
			for (int m = 0; m < pair.manifoldCount; m++) {
				const b3Manifold &manifold = pair.manifolds[m];
				for (int p = 0; p < manifold.pointCount; p++) {
					if (debug_contacts.size() >= max_debug_contacts) {
						return;
					}
					debug_contacts.push_back(b3_to_godot(b3Add(center, manifold.points[p].anchorA)));
				}
			}
		}
	}
}

void Box3DSpace3D::_apply_area_overrides() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	for (Box3DBodyImpl3D *body : box3d_sorted(bodies)) {
		if (!body->has_body_id() || body->is_omitting_force_integration()) {
			continue;
		}

		const AreaOverrides overrides = compute_area_overrides(body);

		b3Body_SetLinearDamping(body->get_body_id(), (float)overrides.linear_damp);
		b3Body_SetAngularDamping(body->get_body_id(), (float)overrides.angular_damp);

		if (!overrides.affects_gravity) {
			b3Body_SetGravityScale(body->get_body_id(), (float)body->get_gravity_scale());
			continue;
		}

		// Box3D has no per-body gravity vector, so contribute the area gravity as an
		// equivalent velocity delta. A force would divide by mass and damp differently.
		const float world_scale = overrides.replaces_world_gravity ? 0.0f : (float)body->get_gravity_scale();
		b3Body_SetGravityScale(body->get_body_id(), world_scale);
		const Vector3 delta = overrides.gravity * body->get_gravity_scale() * last_step;
		const b3Vec3 velocity = b3Body_GetLinearVelocity(body->get_body_id());
		b3Body_SetLinearVelocity(body->get_body_id(), b3Add(velocity, godot_to_b3(delta)));
	}
}

void Box3DSpace3D::_pull_body_events() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	// event.userData is set to the raw C++ object pointer at body creation time (see
	// Box3DBodyImpl3D::_create_body_id / Box3DAreaImpl3D::_create_body_id), but both
	// regular bodies AND area-backing kinematic bodies generate move events (areas move
	// too). The two are sibling classes under Box3DShapedObjectImpl3D, not related to
	// each other, so a blind static_cast<Box3DBodyImpl3D*> on an area's userData produces
	// a garbage pointer and crashes. dynamic_cast safely yields nullptr for area bodies.
	const b3BodyEvents events = b3World_GetBodyEvents(world_id);
	for (int i = 0; i < events.moveCount; i++) {
		const b3BodyMoveEvent &event = events.moveEvents[i];
		auto *body = box3d_object_cast<Box3DBodyImpl3D>(static_cast<Box3DShapedObjectImpl3D *>(event.userData));
		if (body == nullptr || !body->get_state_sync_callback().is_valid()) {
			continue;
		}
		body->set_needs_state_sync(true);
	}
}

void Box3DSpace3D::_pull_sensor_events() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	const b3SensorEvents events = b3World_GetSensorEvents(world_id);

	for (int i = 0; i < events.beginCount; i++) {
		const b3SensorBeginTouchEvent &event = events.beginEvents[i];
		if (!b3Shape_IsValid(event.sensorShapeId) || !b3Shape_IsValid(event.visitorShapeId)) {
			continue;
		}
		const b3BodyId sensor_body_id = b3Shape_GetBody(event.sensorShapeId);
		const b3BodyId visitor_body_id = b3Shape_GetBody(event.visitorShapeId);
		auto *area = static_cast<Box3DAreaImpl3D *>(b3Body_GetUserData(sensor_body_id));
		auto *other = static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(visitor_body_id));
		if (area == nullptr || other == nullptr || other == area) {
			continue;
		}

		if (area->add_overlap(other)) {
			_queue_area_event(area, other, PS3DE::AREA_BODY_ADDED);
		}
	}

	for (int i = 0; i < events.endCount; i++) {
		const b3SensorEndTouchEvent &event = events.endEvents[i];
		if (!b3Shape_IsValid(event.sensorShapeId)) {
			continue;
		}
		const b3BodyId sensor_body_id = b3Shape_GetBody(event.sensorShapeId);
		auto *area = static_cast<Box3DAreaImpl3D *>(b3Body_GetUserData(sensor_body_id));
		if (area == nullptr) {
			continue;
		}

		Box3DShapedObjectImpl3D *other = nullptr;
		if (b3Shape_IsValid(event.visitorShapeId)) {
			const b3BodyId visitor_body_id = b3Shape_GetBody(event.visitorShapeId);
			other = static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(visitor_body_id));
		}
		if (other == nullptr || other == area) {
			continue;
		}

		if (area->remove_overlap(other)) {
			_queue_area_event(area, other, PS3DE::AREA_BODY_REMOVED);
		}
	}
}

void Box3DSpace3D::_queue_area_event(
		Box3DAreaImpl3D *p_area,
		Box3DShapedObjectImpl3D *p_other,
		PS3DE::AreaBodyStatus p_status) {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	auto *other_body = box3d_object_cast<Box3DBodyImpl3D>(p_other);
	auto *other_area = box3d_object_cast<Box3DAreaImpl3D>(p_other);
	if (other_body && other_body->get_soft_body()) {
		auto &counts = soft_area_overlaps[p_area->get_rid()];
		const RID other = p_other->get_rid();
		const int previous = counts.has(other) ? counts[other] : 0;
		if (p_status == PS3DE::AREA_BODY_ADDED) {
			counts[other] = previous + 1;
			if (previous > 0) {
				return;
			}
		} else {
			if (previous > 1) {
				counts[other] = previous - 1;
				return;
			}
			counts.erase(other);
			if (previous == 0) {
				return;
			}
		}
	}

	PendingAreaEvent event;
	event.status = p_status;
	event.other_rid = p_other->get_rid();
	event.other_instance_id = p_other->get_instance_id();

	if (other_body != nullptr && p_area->has_body_monitor_callback()) {
		event.callback = p_area->get_body_monitor_callback();
		pending_area_events.push_back(event);
	} else if (other_area != nullptr && p_area->has_area_monitor_callback()) {
		event.callback = p_area->get_area_monitor_callback();
		pending_area_events.push_back(event);
	}
}

// Mirrors GodotBody3D::call_queries: force integration then state sync, back to back, so a
// node that applies forces from its state-sync callback has them picked up by the next step.
void Box3DSpace3D::_call_body_queries() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	LocalVector<RID> body_rids;
	body_rids.reserve(bodies.size());
	for (Box3DBodyImpl3D *body : box3d_sorted(bodies)) {
		body_rids.push_back(body->get_rid());
	}

	// Callbacks may detach or free bodies, so re-resolve each RID instead of holding a
	// pointer across the call.
	for (const RID &body_rid : body_rids) {
		Box3DBodyImpl3D *body = Box3DPhysicsServer3D::get_singleton()->get_body(body_rid);
		if (body == nullptr || body->get_space() != this) {
			continue;
		}

		const Callable integration_callback = body->get_force_integration_callback();
		if (integration_callback.is_valid()) {
			const Variant userdata = body->get_force_integration_userdata();
			Array arguments;
			if (userdata.get_type() == Variant::NIL) {
				arguments.resize(1);
				arguments[0] = body->get_direct_state_or_null();
			} else {
				arguments.resize(2);
				arguments[0] = body->get_direct_state_or_null();
				arguments[1] = userdata;
			}
			integration_callback.callv(arguments);
		}

		body = Box3DPhysicsServer3D::get_singleton()->get_body(body_rid);
		if (body == nullptr || body->get_space() != this) {
			continue;
		}
		// Godot syncs every awake body, not just ones Box3D reported as moved: nodes like
		// VehicleBody3D drive themselves from this callback and would never start moving.
		if (body->is_sleeping() && !body->needs_state_sync()) {
			continue;
		}
		const Callable sync_callback = body->get_state_sync_callback();
		if (sync_callback.is_valid()) {
			Array arguments;
			arguments.resize(1);
			arguments[0] = body->get_direct_state_or_null();
			sync_callback.callv(arguments);
		}
		body = Box3DPhysicsServer3D::get_singleton()->get_body(body_rid);
		if (body != nullptr && body->get_space() == this) {
			body->set_needs_state_sync(false);
		}
	}
}

Vector3 Box3DSpace3D::compute_wind(Box3DBodyImpl3D *p_body, const Vector3 &p_normal, real_t p_area) const {
	Vector3 force;
	for (auto *area : box3d_sorted(areas)) {
		if (area->get_overlaps().has(p_body)) {
			force += area->compute_wind(p_body->get_transform().origin, p_normal, p_area);
		}
	}
	return force;
}

void Box3DSpace3D::forget_object(Box3DShapedObjectImpl3D *p_object) {
	for (auto *area : box3d_sorted(areas)) {
		if (area == p_object) {
			area->clear_overlaps();
			soft_area_overlaps.erase(area->get_rid());
			continue;
		}
		if (area->forget_overlap(p_object)) {
			_queue_area_event(area, p_object, PS3DE::AREA_BODY_REMOVED);
		}
	}
}

void Box3DSpace3D::flush_queries() {
	std::lock_guard<std::recursive_mutex> guard(egp::box3d::get_simulation_mutex());
	flushing_queries = true;

	_call_body_queries();

	// A callback can queue more events; retain the original batch outside the mutable queue.
	LocalVector<PendingAreaEvent> events;
	std::swap(events, pending_area_events);
	for (const PendingAreaEvent &event : events) {
		Array arguments;
		arguments.resize(5);
		arguments[0] = event.status;
		arguments[1] = event.other_rid;
		arguments[2] = event.other_instance_id;
		arguments[3] = 0;
		arguments[4] = 0;
		event.callback.callv(arguments);
	}

	flushing_queries = false;
}

void Box3DSpace3D::_cache_native_events() {
	contact_hit_events.clear();
	joint_events.clear();
	const auto contacts = b3World_GetContactEvents(world_id);
	for (int i = 0; i < contacts.hitCount; i++) {
		const auto &event = contacts.hitEvents[i];
		if (!b3Shape_IsValid(event.shapeIdA) || !b3Shape_IsValid(event.shapeIdB)) {
			continue;
		}
		auto *a = static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(b3Shape_GetBody(event.shapeIdA)));
		auto *b = static_cast<Box3DShapedObjectImpl3D *>(b3Body_GetUserData(b3Shape_GetBody(event.shapeIdB)));
		if (!a || !b) {
			continue;
		}
		Dictionary item;
		item["rid_a"] = a->get_rid();
		item["rid_b"] = b->get_rid();
		item["point"] = b3_to_godot(event.point);
		item["normal"] = b3_to_godot(event.normal);
		item["approach_speed"] = event.approachSpeed;
		contact_hit_events.push_back(item);
	}
	const auto joints = b3World_GetJointEvents(world_id);
	for (int i = 0; i < joints.count; i++) {
		const auto &event = joints.jointEvents[i];
		if (!event.userData || !b3Joint_IsValid(event.jointId)) {
			continue;
		}
		auto *joint = static_cast<Box3DJointImpl3D *>(event.userData);
		Dictionary item;
		item["rid"] = joint->get_rid();
		item["force"] = b3_to_godot(b3Joint_GetConstraintForce(event.jointId));
		item["torque"] = b3_to_godot(b3Joint_GetConstraintTorque(event.jointId));
		joint_events.push_back(item);
	}
}
