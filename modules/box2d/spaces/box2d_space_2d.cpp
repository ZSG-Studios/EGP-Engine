#include "../joints/box2d_joint_2d.h"
// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "../box2d_physics_server_2d.h"
#include "../box2d_project_settings.h"
#include "box2d_space_2d.h"

#include "modules/box2d/precompiled.h"

void task_function(void *p_userdata) {
	TracyZoneScoped("Box2D Task");
	CRASH_COND_MSG(std::fegetround() != FE_TONEAREST, "Box2D workers require round-to-nearest floating point.");

	Box2DTaskData *data = static_cast<Box2DTaskData *>(p_userdata);
	data->task(data->task_context);
}

/// Box2D packs the worker index into the task context, so one enqueue is one run of one callback.
void *enqueue_task_callback(b2TaskCallback *task, void *taskContext, void *userContext) {
	Box2DTaskData *task_data = new Box2DTaskData{ 0, task, taskContext };

	static const String task_name("Box2D Task");

	task_data->task_id = WorkerThreadPool::get_singleton()->add_native_task(task_function, task_data, true, task_name);

	return task_data;
}

void finish_task_callback(void *taskPtr, void *userContext) {
	if (taskPtr) {
		Box2DTaskData *task_data = static_cast<Box2DTaskData *>(taskPtr);
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task_data->task_id);
		delete task_data;
	}
}

bool box2d_godot_presolve(b2ShapeId shapeIdA, b2ShapeId shapeIdB, b2Pos point, b2Vec2 normal, void *context) {
	const Box2DShapeInstance *shape_a = static_cast<Box2DShapeInstance *>(b2Shape_GetUserData(shapeIdA));
	const Box2DShapeInstance *shape_b = static_cast<Box2DShapeInstance *>(b2Shape_GetUserData(shapeIdB));

	if (!shape_a->has_one_way_collision() && !shape_b->has_one_way_collision()) {
		return true;
	}

	Vector2 godot_normal = to_godot_normalized(normal);

	// The normal points from A to B, so B sees it reversed.
	return !shape_a->should_filter_one_way_collision(godot_normal) &&
			!shape_b->should_filter_one_way_collision(-godot_normal);
}

real_t godot_friction_callback(real_t frictionA, uint64_t materialA, real_t frictionB, uint64_t materialB) {
	return Math::abs(MIN(frictionA, frictionB));
}

real_t godot_restitution_callback(real_t restitutionA, uint64_t materialA, real_t restitutionB, uint64_t materialB) {
	return CLAMP(restitutionA + restitutionB, 0.0f, 1.0f);
}

Box2DSpace2D::Box2DSpace2D() {
	substeps = Box2DProjectSettings::get_substeps();

	// Gravity is changed by the default area immediately - the value set here doesn't matter.
	default_gravity = Vector2(0.0, 980.0);

	// The profile is independent of the machine's reported processor count.
	max_tasks = Box2DProjectSettings::get_max_threads();

	world_def.gravity = to_box2d(default_gravity);
	world_def.contactHertz = Box2DProjectSettings::get_contact_hertz();
	world_def.contactDampingRatio = Box2DProjectSettings::get_contact_damping_ratio();

	if (Box2DProjectSettings::get_friction_mixing_rule() == Box2DMixingRule::MIXING_RULE_GODOT) {
		world_def.frictionCallback = godot_friction_callback;
	}

	if (Box2DProjectSettings::get_restitution_mixing_rule() == Box2DMixingRule::MIXING_RULE_GODOT) {
		world_def.restitutionCallback = godot_restitution_callback;
	}

	world_def.workerCount = max_tasks;
	world_def.userTaskContext = this;
	world_def.enqueueTask = enqueue_task_callback;
	world_def.finishTask = finish_task_callback;

	contact_hertz = world_def.contactHertz;
	contact_damping_ratio = world_def.contactDampingRatio;
	contact_max_push_speed = world_def.contactSpeed;
	world_id = b2CreateWorld(&world_def);

	if (Box2DProjectSettings::get_presolve_enabled()) {
		b2World_SetPreSolveCallback(world_id, box2d_godot_presolve, this);
	}
}

Box2DSpace2D::~Box2DSpace2D() {
	if (direct_state) {
		memdelete(direct_state);
	}

	if (b2World_IsValid(world_id)) {
		b2DestroyWorld(world_id);
	}

	world_id = b2_nullWorldId;
}

void Box2DSpace2D::step(real_t p_step) {
	locked = true;

	if (linear_damp_changed || angular_damp_changed) {
		for (auto *object : box2d_sorted(objects)) {
			if (auto *body = object->as_body()) {
				if (linear_damp_changed) {
					body->update_linear_damping();
				}
				if (angular_damp_changed) {
					body->update_angular_damping();
				}
			}
		}
		linear_damp_changed = false;
		angular_damp_changed = false;
	}

	for (Box2DBody2D *body : constant_force_list) {
		body->apply_constant_forces();
	}

	for (Box2DArea2D *area : areas_to_step) {
		area->apply_overrides();
	}

	for (auto *object : box2d_sorted(objects)) {
		if (auto *body = object->as_body()) {
			body->apply_area_overrides();
		}
	}
	bodies_with_overrides.clear();

	b2World_Step(world_id, p_step, substeps);
	_cache_native_events();

	locked = false;
	last_step = p_step;
}

void Box2DSpace2D::rebuild_exception_joints(const Box2DPhysicsServer2D *p_server) {
	if (!exceptions_dirty) {
		return;
	}
	exceptions_dirty = false;

	LocalVector<Box2DBody2D *> emptied;

	for (Box2DBody2D *body : box2d_sorted(bodies_with_exceptions)) {
		body->rebuild_exception_joints(p_server);
		if (!body->has_collision_exceptions()) {
			emptied.push_back(body);
		}
	}

	// Dropped only after the rebuild, which is what tears their last joint down.
	for (Box2DBody2D *body : emptied) {
		bodies_with_exceptions.erase(body);
	}
}

void Box2DSpace2D::sync_state() {
	TracyZoneScoped("Box2DSpace2D::sync_state");

	struct BodySync {
		RID rid;
		b2Transform transform;
		bool asleep;
		bool operator<(const BodySync &other) const { return rid.get_id() < other.rid.get_id(); }
	};
	LocalVector<BodySync> moves;
	b2BodyEvents body_events = b2World_GetBodyEvents(world_id);
	for (int i = 0; i < body_events.moveCount; ++i) {
		const auto &event = body_events.moveEvents[i];
		auto *object = static_cast<Box2DCollisionObject2D *>(event.userData);
		if (object && !object->is_freed() && object->is_body()) {
			moves.push_back({ object->get_rid(), event.transform, event.fellAsleep });
		}
	}
	moves.sort();
	auto *server = Box2DPhysicsServer2D::get_singleton();
	for (const auto &move : moves) {
		auto *body = server->get_body(move.rid);
		if (body && body->get_space() == this) {
			body->sync_state(move.transform, move.asleep);
		}
	}
	LocalVector<RID> integrations;
	for (auto *body : force_integration_list) {
		integrations.push_back(body->get_rid());
	}
	integrations.sort();
	for (RID body_rid : integrations) {
		auto *body = server->get_body(body_rid);
		if (body && body->get_space() == this) {
			body->call_force_integration_callback();
		}
	}
	for (auto *object : box2d_sorted(objects)) {
		if (!object->is_freed() && object->get_space() == this && object->is_area()) {
			object->as_area()->update_overlaps();
		}
	}
}

Box2DDirectSpaceState2D *Box2DSpace2D::get_direct_state() {
	if (!direct_state) {
		direct_state = memnew(Box2DDirectSpaceState2D(this));
	}
	return direct_state;
}

void Box2DSpace2D::set_default_gravity(Vector2 p_gravity) {
	default_gravity = p_gravity;
	b2World_SetGravity(world_id, to_box2d(default_gravity));
}

b2BodyId Box2DSpace2D::get_world_anchor_body() {
	if (!b2Body_IsValid(world_anchor_body)) {
		b2BodyDef def = b2DefaultBodyDef();
		def.type = b2_staticBody;
		world_anchor_body = b2CreateBody(world_id, &def);
	}
	return world_anchor_body;
}

real_t Box2DSpace2D::get_param(PS2DE::SpaceParameter p_param) const {
	switch (p_param) {
		case PS2DE::SPACE_PARAM_CONTACT_HERTZ:
			return contact_hertz;
		case PS2DE::SPACE_PARAM_CONTACT_DAMPING_RATIO:
			return contact_damping_ratio;
		case PS2DE::SPACE_PARAM_CONTACT_MAX_PUSH_SPEED:
			return contact_max_push_speed;
		case PS2DE::SPACE_PARAM_RESTITUTION_THRESHOLD:
			return b2World_GetRestitutionThreshold(world_id);
		case PS2DE::SPACE_PARAM_MAXIMUM_LINEAR_SPEED:
			return b2World_GetMaximumLinearSpeed(world_id);
		case PS2DE::SPACE_PARAM_SLEEP_ENABLED:
			return b2World_IsSleepingEnabled(world_id);
		case PS2DE::SPACE_PARAM_CONTINUOUS_ENABLED:
			return b2World_IsContinuousEnabled(world_id);
		case PS2DE::SPACE_PARAM_WARM_STARTING_ENABLED:
			return b2World_IsWarmStartingEnabled(world_id);
		default:
			ERR_FAIL_V_MSG(0, "Invalid native space parameter.");
	}
}

void Box2DSpace2D::set_param(PS2DE::SpaceParameter p_param, real_t p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	ERR_FAIL_COND(p_value < 0);
	switch (p_param) {
		case PS2DE::SPACE_PARAM_CONTACT_HERTZ:
			contact_hertz = p_value;
			b2World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS2DE::SPACE_PARAM_CONTACT_DAMPING_RATIO:
			contact_damping_ratio = p_value;
			b2World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS2DE::SPACE_PARAM_CONTACT_MAX_PUSH_SPEED:
			contact_max_push_speed = p_value;
			b2World_SetContactTuning(world_id, contact_hertz, contact_damping_ratio, contact_max_push_speed);
			break;
		case PS2DE::SPACE_PARAM_RESTITUTION_THRESHOLD:
			b2World_SetRestitutionThreshold(world_id, (float)p_value);
			break;
		case PS2DE::SPACE_PARAM_MAXIMUM_LINEAR_SPEED:
			ERR_FAIL_COND(p_value <= 0);
			b2World_SetMaximumLinearSpeed(world_id, (float)p_value);
			break;
		case PS2DE::SPACE_PARAM_SLEEP_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b2World_EnableSleeping(world_id, p_value != 0);
			break;
		case PS2DE::SPACE_PARAM_CONTINUOUS_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b2World_EnableContinuous(world_id, p_value != 0);
			break;
		case PS2DE::SPACE_PARAM_WARM_STARTING_ENABLED:
			ERR_FAIL_COND(p_value != 0 && p_value != 1);
			b2World_EnableWarmStarting(world_id, p_value != 0);
			break;
		default:
			ERR_FAIL_MSG("Invalid native space parameter.");
	}
}

void Box2DSpace2D::_cache_native_events() {
	contact_hit_events.clear();
	joint_events.clear();
	const auto contacts = b2World_GetContactEvents(world_id);
	for (int i = 0; i < contacts.hitCount; i++) {
		const auto &event = contacts.hitEvents[i];
		if (!b2Shape_IsValid(event.shapeIdA) || !b2Shape_IsValid(event.shapeIdB)) {
			continue;
		}
		auto *a = static_cast<Box2DCollisionObject2D *>(b2Body_GetUserData(b2Shape_GetBody(event.shapeIdA)));
		auto *b = static_cast<Box2DCollisionObject2D *>(b2Body_GetUserData(b2Shape_GetBody(event.shapeIdB)));
		if (!a || !b) {
			continue;
		}
		Dictionary item;
		item["rid_a"] = a->get_rid();
		item["rid_b"] = b->get_rid();
		item["point"] = to_godot(event.point);
		item["normal"] = to_godot(event.normal);
		item["approach_speed"] = event.approachSpeed;
		contact_hit_events.push_back(item);
	}
	const auto joints = b2World_GetJointEvents(world_id);
	for (int i = 0; i < joints.count; i++) {
		const auto &event = joints.jointEvents[i];
		if (!event.userData || !b2Joint_IsValid(event.jointId)) {
			continue;
		}
		auto *joint = static_cast<Box2DJoint2D *>(event.userData);
		Dictionary item;
		item["rid"] = joint->get_rid();
		item["force"] = to_godot(b2Joint_GetConstraintForce(event.jointId));
		item["torque"] = b2Joint_GetConstraintTorque(event.jointId);
		joint_events.push_back(item);
	}
}
