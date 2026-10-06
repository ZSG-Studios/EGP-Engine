extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("run")

func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func run() -> void:
	var space := PhysicsServer2D.space_create()
	PhysicsServer2D.space_set_active(space, true)
	PhysicsServer2D.area_set_param(space, PhysicsServer2D.AREA_PARAM_GRAVITY, 0.0)
	var shape := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(shape, 0.5)
	var anchor := PhysicsServer2D.body_create()
	PhysicsServer2D.body_add_shape(anchor, shape)
	PhysicsServer2D.body_set_space(anchor, space)
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_CAN_SLEEP, false)
	PhysicsServer2D.body_set_space(body, space)
	var joint := PhysicsServer2D.joint_create()
	for kind in [PhysicsServer2D.JOINT_TYPE_DISTANCE, PhysicsServer2D.JOINT_TYPE_FILTER, PhysicsServer2D.JOINT_TYPE_MOTOR, PhysicsServer2D.JOINT_TYPE_PRISMATIC, PhysicsServer2D.JOINT_TYPE_REVOLUTE, PhysicsServer2D.JOINT_TYPE_WELD, PhysicsServer2D.JOINT_TYPE_WHEEL]:
		PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D.IDENTITY)
		PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, Vector2.ZERO)
		PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_ANGULAR_VELOCITY, 0.0)
		PhysicsServer2D.joint_make_configured(joint, kind, anchor, Transform2D.IDENTITY, body, Transform2D.IDENTITY, {})
		require(PhysicsServer2D.joint_get_type(joint) == kind, "native configured joint type " + str(kind))
		var configuration := PhysicsServer2D.joint_get_configuration(joint)
		PhysicsServer2D.joint_set_configuration(joint, {"constraint_hertz": 60.0})
		configuration["constraint_hertz"] = 10.0
		require(PhysicsServer2D.joint_get_configuration(joint).constraint_hertz == 60.0, "joint configuration is an independent copy")
		match kind:
			PhysicsServer2D.JOINT_TYPE_DISTANCE:
				PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(0, 2)))
				PhysicsServer2D.joint_set_configuration(joint, {"length": 1.0})
			PhysicsServer2D.JOINT_TYPE_MOTOR:
				PhysicsServer2D.joint_set_configuration(joint, {"linear_velocity": Vector2(2, 0), "max_velocity_force": 100.0, "max_spring_force": 0.0})
			PhysicsServer2D.JOINT_TYPE_PRISMATIC:
				PhysicsServer2D.joint_set_configuration(joint, {"enable_motor": true, "motor_speed": 2.0, "max_motor_force": 100.0})
			PhysicsServer2D.JOINT_TYPE_REVOLUTE, PhysicsServer2D.JOINT_TYPE_WHEEL:
				PhysicsServer2D.joint_set_configuration(joint, {"enable_motor": true, "motor_speed": 2.0, "max_motor_torque": 20.0})
			PhysicsServer2D.JOINT_TYPE_WELD:
				PhysicsServer2D.body_apply_central_impulse(body, Vector2(3, 0))
		for frame in 90:
			await physics_frame
		var pose: Transform2D = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM)
		var velocity: Vector2 = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY)
		var angular: float = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_ANGULAR_VELOCITY)
		require(pose.is_finite() and velocity.is_finite() and is_finite(angular), "joint solver remains finite")
		require(PhysicsServer2D.joint_get_constraint_force(joint).is_finite() and is_finite(PhysicsServer2D.joint_get_constraint_torque(joint)), "native reaction force and torque are exposed")
		match kind:
			PhysicsServer2D.JOINT_TYPE_DISTANCE:
				require(absf(pose.origin.length() - 1.0) < 0.05, "distance joint reaches configured length")
			PhysicsServer2D.JOINT_TYPE_FILTER:
				require(pose.origin.length() < 0.05, "filter joint suppresses overlapping connected-body collision")
			PhysicsServer2D.JOINT_TYPE_MOTOR, PhysicsServer2D.JOINT_TYPE_PRISMATIC:
				require(absf(velocity.x - 2.0) < 0.05, "linear motor reaches configured velocity")
			PhysicsServer2D.JOINT_TYPE_REVOLUTE, PhysicsServer2D.JOINT_TYPE_WHEEL:
				require(absf(angular - 2.0) < 0.05, "angular motor reaches configured velocity")
			PhysicsServer2D.JOINT_TYPE_WELD:
				require(pose.origin.length() < 0.05, "weld restrains applied translation")
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D.IDENTITY)
	PhysicsServer2D.joint_make_configured(joint, PhysicsServer2D.JOINT_TYPE_WELD, anchor, Transform2D.IDENTITY, body, Transform2D.IDENTITY, {"force_threshold": 0.01, "torque_threshold": 0.01})
	var threshold_seen := false
	for frame in 12:
		PhysicsServer2D.body_apply_central_force(body, Vector2(10, 0))
		await physics_frame
		var events := PhysicsServer2D.space_get_joint_events(space)
		for event in events:
			if event.rid == joint:
				threshold_seen = true
				require(event.force is Vector2 and event.force.is_finite() and is_finite(event.torque), "joint threshold event carries finite force/torque")
				event["force"] = Vector2(INF, INF)
				for fresh in PhysicsServer2D.space_get_joint_events(space):
					require(fresh.force.is_finite(), "joint event dictionaries are independent copies")
		require(PhysicsServer2D.joint_get_type(joint) == PhysicsServer2D.JOINT_TYPE_WELD, "threshold events do not break the joint")
	PhysicsServer2D.body_set_constant_force(body, Vector2.ZERO)
	require(threshold_seen, "native joint threshold events reach the API")
	PhysicsServer2D.free_rid(joint)
	PhysicsServer2D.free_rid(anchor)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(1.5, 0)))
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, Vector2.ZERO)
	PhysicsServer2D.space_apply_explosion(space, Vector2.ZERO, 2.0, 1.0, 10.0, 2)
	require(PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY) == Vector2.ZERO, "explosion mask excludes the body")
	PhysicsServer2D.space_apply_explosion(space, Vector2.ZERO, 2.0, 1.0, 10.0, 1)
	var exploded: Vector2 = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY)
	require(exploded.x > 0, "explosion produces an outward impulse")
	var floor_shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(floor_shape, Vector2(100, 10))
	var floor_body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_add_shape(floor_body, floor_shape)
	PhysicsServer2D.body_set_state(floor_body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(0, 100)))
	PhysicsServer2D.body_set_space(floor_body, space)
	PhysicsServer2D.area_set_param(space, PhysicsServer2D.AREA_PARAM_GRAVITY, 980.0)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D.IDENTITY)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, Vector2.ZERO)
	PhysicsServer2D.body_set_param(body, PhysicsServer2D.BODY_PARAM_HIT_EVENTS_ENABLED, true)
	var hit_seen := false
	for frame in 180:
		await physics_frame
		for event in PhysicsServer2D.space_get_contact_hit_events(space):
			if event.rid_a == body or event.rid_b == body:
				hit_seen = true
				require(event.point.is_finite() and event.normal.is_finite() and event.approach_speed > 0, "hit event includes native contact geometry and approach speed")
				event["point"] = Vector2(INF, INF)
				for fresh in PhysicsServer2D.space_get_contact_hit_events(space):
					require(fresh.point.is_finite(), "contact hit event dictionaries are independent copies")
	require(hit_seen, "native contact hit events reach the API")
	for rid in [body, floor_body, floor_shape, shape, space]:
		PhysicsServer2D.free_rid(rid)
	print("RESULT: PASS - native configured joints, threshold/hit events and explosions" if failures == 0 else "RESULT: FAIL - " + str(failures))
	quit(0 if failures == 0 else 1)
