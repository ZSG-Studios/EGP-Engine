extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func _frames(count: int) -> void:
	for frame in count:
		await physics_frame

func _run() -> void:
	var hinge := HingeJoint3D.new()
	hinge.set("spring/enabled", true)
	_check(hinge.is_spring_enabled() and hinge.get_param(HingeJoint3D.PARAM_SPRING_ENABLED) == 1.0, "hinge boolean spring maps to native parameter")
	hinge.free()
	var slider := SliderJoint3D.new()
	for property in ["spring/enabled", "motor/enabled", "linear_limit/enabled"]:
		slider.set(property, true)
		_check(slider.get(property) is bool and slider.get(property), "slider boolean property: " + property)
		slider.set(property, false)
		_check(not slider.get(property), "slider boolean reset: " + property)
	slider.free()
	_check(not ClassDB.class_exists("SeparationRayShape3D"), "unsupported 3D separation-ray resource is removed")
	for method in ["shape_set_margin", "shape_get_margin", "custom_shape_create", "separation_ray_shape_create", "body_set_collision_priority", "joint_set_solver_priority", "shape_set_custom_solver_bias"]:
		_check(not PhysicsServer3D.has_method(method), "removed server method: " + method)
	var resource := BoxShape3D.new()
	for info in resource.get_property_list():
		_check(info.name not in ["margin", "custom_solver_bias"], "removed shape properties are absent")
	var constants := ClassDB.class_get_integer_constant_list("PhysicsServer3D")
	for name in ["PIN_JOINT_IMPULSE_CLAMP", "PIN_JOINT_BIAS", "HINGE_JOINT_BIAS", "HINGE_JOINT_MOTOR_MAX_IMPULSE", "SLIDER_JOINT_ANGULAR_LIMIT_UPPER", "SPACE_PARAM_SOLVER_ITERATIONS", "SHAPE_CUSTOM", "SHAPE_SEPARATION_RAY"]:
		_check(name not in constants, "removed constant: " + name)
	var space := PhysicsServer3D.space_create()
	PhysicsServer3D.space_set_active(space, true)
	PhysicsServer3D.area_set_param(space, PhysicsServer3D.AREA_PARAM_GRAVITY, 0.0)
	PhysicsServer3D.space_set_param(space, PhysicsServer3D.SPACE_PARAM_CONTACT_HERTZ, 25.0)
	PhysicsServer3D.space_set_param(space, PhysicsServer3D.SPACE_PARAM_CONTACT_DAMPING_RATIO, 1.0)
	PhysicsServer3D.space_set_param(space, PhysicsServer3D.SPACE_PARAM_RESTITUTION_ITERATIONS, 3)
	_check(PhysicsServer3D.space_get_param(space, PhysicsServer3D.SPACE_PARAM_CONTACT_HERTZ) == 25, "contact tuning is exposed")
	_check(PhysicsServer3D.space_get_param(space, PhysicsServer3D.SPACE_PARAM_RESTITUTION_ITERATIONS) == 3, "native restitution iterations are exposed")
	var shape := PhysicsServer3D.sphere_shape_create()
	PhysicsServer3D.shape_set_data(shape, 0.5)
	var anchor := PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(anchor, PhysicsServer3D.BODY_MODE_STATIC)
	PhysicsServer3D.body_add_shape(anchor, shape)
	PhysicsServer3D.body_set_space(anchor, space)
	var body := PhysicsServer3D.body_create()
	PhysicsServer3D.body_add_shape(body, shape)
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_CAN_SLEEP, false)
	PhysicsServer3D.body_set_param(body, PhysicsServer3D.BODY_PARAM_SLEEP_THRESHOLD, 0.1)
	PhysicsServer3D.body_set_param(body, PhysicsServer3D.BODY_PARAM_CCD_SAFETY_FACTOR, 0.1)
	PhysicsServer3D.body_set_space(body, space)
	_check(is_equal_approx(PhysicsServer3D.body_get_param(body, PhysicsServer3D.BODY_PARAM_CCD_SAFETY_FACTOR), 0.1), "native CCD safety persists through attachment")
	var joint := PhysicsServer3D.joint_create()
	for kind in [PhysicsServer3D.JOINT_TYPE_DISTANCE, PhysicsServer3D.JOINT_TYPE_FILTER, PhysicsServer3D.JOINT_TYPE_MOTOR, PhysicsServer3D.JOINT_TYPE_PARALLEL, PhysicsServer3D.JOINT_TYPE_PRISMATIC, PhysicsServer3D.JOINT_TYPE_REVOLUTE, PhysicsServer3D.JOINT_TYPE_SPHERICAL, PhysicsServer3D.JOINT_TYPE_WELD, PhysicsServer3D.JOINT_TYPE_WHEEL]:
		PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D.IDENTITY)
		PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, Vector3.ZERO)
		PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_ANGULAR_VELOCITY, Vector3.ZERO)
		PhysicsServer3D.joint_make_configured(joint, kind, anchor, Transform3D.IDENTITY, body, Transform3D.IDENTITY, {})
		_check(PhysicsServer3D.joint_get_type(joint) == kind, "native joint type is constructed: " + str(kind))
		var settings := PhysicsServer3D.joint_get_configuration(joint)
		_check(settings.has("constraint_hertz"), "native joint configuration includes constraint tuning")
		PhysicsServer3D.joint_set_configuration(joint, {"constraint_hertz": 60.0})
		settings["constraint_hertz"] = 10.0
		_check(PhysicsServer3D.joint_get_configuration(joint).constraint_hertz == 60, "configuration getter returns an independent copy")
		match kind:
			PhysicsServer3D.JOINT_TYPE_DISTANCE:
				PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis.IDENTITY, Vector3(0, 2, 0)))
				PhysicsServer3D.joint_set_configuration(joint, {"length": 1.0})
			PhysicsServer3D.JOINT_TYPE_MOTOR:
				PhysicsServer3D.joint_set_configuration(joint, {"linear_velocity": Vector3(2, 0, 0), "max_velocity_force": 100.0, "max_spring_force": 0.0})
			PhysicsServer3D.JOINT_TYPE_PRISMATIC:
				PhysicsServer3D.joint_set_configuration(joint, {"enable_motor": true, "motor_speed": 2.0, "max_motor_force": 100.0})
			PhysicsServer3D.JOINT_TYPE_REVOLUTE:
				PhysicsServer3D.joint_set_configuration(joint, {"enable_motor": true, "motor_speed": 2.0, "max_motor_torque": 20.0})
			PhysicsServer3D.JOINT_TYPE_SPHERICAL:
				PhysicsServer3D.joint_set_configuration(joint, {"enable_motor": true, "motor_velocity": Vector3(0, 0, 2), "max_motor_torque": 20.0})
			PhysicsServer3D.JOINT_TYPE_WHEEL:
				PhysicsServer3D.joint_set_configuration(joint, {"enable_spin_motor": true, "spin_speed": 2.0, "max_spin_torque": 20.0})
			PhysicsServer3D.JOINT_TYPE_WELD:
				PhysicsServer3D.body_apply_central_impulse(body, Vector3(3, 0, 0))
		await _frames(90)
		var pose: Transform3D = PhysicsServer3D.body_get_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM)
		var velocity: Vector3 = PhysicsServer3D.body_get_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
		var angular: Vector3 = PhysicsServer3D.body_get_state(body, PhysicsServer3D.BODY_STATE_ANGULAR_VELOCITY)
		_check(pose.is_finite(), "native joint remains finite")
		_check(PhysicsServer3D.joint_get_constraint_force(joint).is_finite(), "joint reaction force is exposed")
		if kind == PhysicsServer3D.JOINT_TYPE_DISTANCE:
			_check(absf(pose.origin.length() - 1.0) < 0.05, "native distance constraint changes the trajectory")
		elif kind in [PhysicsServer3D.JOINT_TYPE_MOTOR, PhysicsServer3D.JOINT_TYPE_PRISMATIC]:
			_check(absf(velocity.x - 2.0) < 0.05, "native linear motor reaches its target")
		elif kind in [PhysicsServer3D.JOINT_TYPE_REVOLUTE, PhysicsServer3D.JOINT_TYPE_SPHERICAL, PhysicsServer3D.JOINT_TYPE_WHEEL]:
			_check(absf(angular.z - 2.0) < 0.05, "native angular motor reaches its target")
		elif kind == PhysicsServer3D.JOINT_TYPE_WELD:
			_check(pose.origin.length() < 0.05, "native weld restrains translation")
	PhysicsServer3D.free_rid(joint)
	PhysicsServer3D.free_rid(anchor)
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis.IDENTITY, Vector3(1.5, 0, 0)))
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, Vector3.ZERO)
	PhysicsServer3D.space_apply_explosion(space, Vector3.ZERO, 2.0, 1.0, 10.0, 2)
	_check(PhysicsServer3D.body_get_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY) == Vector3.ZERO, "explosion collision mask excludes body")
	PhysicsServer3D.space_apply_explosion(space, Vector3.ZERO, 2.0, 1.0, 10.0, 1)
	var exploded: Vector3 = PhysicsServer3D.body_get_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
	_check(exploded.x > 0, "native explosion applies an outward impulse")
	var floor_shape := PhysicsServer3D.box_shape_create()
	PhysicsServer3D.shape_set_data(floor_shape, Vector3(10, 0.5, 10))
	var floor_body := PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(floor_body, PhysicsServer3D.BODY_MODE_STATIC)
	PhysicsServer3D.body_add_shape(floor_body, floor_shape)
	PhysicsServer3D.body_set_space(floor_body, space)
	PhysicsServer3D.area_set_param(space, PhysicsServer3D.AREA_PARAM_GRAVITY, 9.8)
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis.IDENTITY, Vector3(0, 3, 0)))
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, Vector3.ZERO)
	PhysicsServer3D.body_set_param(body, PhysicsServer3D.BODY_PARAM_HIT_EVENTS_ENABLED, true)
	PhysicsServer3D.space_set_debug_contacts(space, 4)
	var hit_seen := false
	for frame in 180:
		await physics_frame
		for event in PhysicsServer3D.space_get_contact_hit_events(space):
			hit_seen = hit_seen or (event.rid_a == body or event.rid_b == body)
	_check(hit_seen, "native contact hit events reach the server")
	_check(PhysicsServer3D.space_get_contact_count(space) > 0 and PhysicsServer3D.space_get_contact_count(space) <= 4, "debug contact data comes from native manifolds")
	_check(PhysicsServer3D.space_get_contacts(space).size() == PhysicsServer3D.space_get_contact_count(space), "debug contacts count matches returned points")
	PhysicsServer3D.free_rid(body)
	PhysicsServer3D.free_rid(floor_body)
	PhysicsServer3D.free_rid(floor_shape)
	PhysicsServer3D.free_rid(shape)
	PhysicsServer3D.free_rid(space)
	print("RESULT: PASS - native capabilities and unsupported API removal" if failures == 0 else "RESULT: FAIL - " + str(failures))
	quit(0 if failures == 0 else 1)
