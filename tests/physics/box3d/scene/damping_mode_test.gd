extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func _run() -> void:
	var space := PhysicsServer3D.space_create()
	PhysicsServer3D.space_set_active(space, true)
	PhysicsServer3D.area_set_param(space, PhysicsServer3D.AREA_PARAM_GRAVITY, 0.0)
	PhysicsServer3D.area_set_param(space, PhysicsServer3D.AREA_PARAM_LINEAR_DAMP, 2.0)
	var shape := PhysicsServer3D.sphere_shape_create()
	PhysicsServer3D.shape_set_data(shape, 0.25)
	var combine := PhysicsServer3D.body_create()
	var replace := PhysicsServer3D.body_create()
	for body in [combine, replace]:
		PhysicsServer3D.body_add_shape(body, shape)
		PhysicsServer3D.body_set_param(body, PhysicsServer3D.BODY_PARAM_LINEAR_DAMP, 1.0)
		PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, Vector3(100, 0, 0))
		PhysicsServer3D.body_set_space(body, space)
	PhysicsServer3D.body_set_state(replace, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis(), Vector3(0, 10, 0)))
	PhysicsServer3D.body_set_param(replace, PhysicsServer3D.BODY_PARAM_LINEAR_DAMP_MODE, PhysicsServer3D.BODY_DAMP_MODE_REPLACE)
	_check(PhysicsServer3D.body_get_param(replace, PhysicsServer3D.BODY_PARAM_LINEAR_DAMP_MODE) == PhysicsServer3D.BODY_DAMP_MODE_REPLACE, "damping mode is retained")
	for frame in 30:
		await physics_frame
	var combined: Vector3 = PhysicsServer3D.body_get_state(combine, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
	var replaced: Vector3 = PhysicsServer3D.body_get_state(replace, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY)
	_check(combined.x < 30 and replaced.x > 50 and replaced.x > combined.x * 2.0, "COMBINE includes default-area damping and REPLACE overrides it")
	PhysicsServer3D.body_set_contacts_reported_depth_threshold(combine, 0.2)
	_check(is_equal_approx(PhysicsServer3D.body_get_contacts_reported_depth_threshold(combine), 0.2), "contact depth threshold round-trips")
	for body in [combine, replace]:
		PhysicsServer3D.free_rid(body)
	PhysicsServer3D.free_rid(shape)
	PhysicsServer3D.free_rid(space)
	print("RESULT: PASS - damping modes and contact thresholds" if failures == 0 else "RESULT: FAIL - damping modes")
	quit(0 if failures == 0 else 1)
