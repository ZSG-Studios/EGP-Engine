extends SceneTree

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer2D.space_create()
	PhysicsServer2D.space_set_active(space, true)
	var shape := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(shape, 10.0)
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(100, 100)))
	PhysicsServer2D.body_set_space(body, space)
	var joint := PhysicsServer2D.joint_create()
	PhysicsServer2D.joint_make_pin(joint, Vector2(100, 100), body)
	for tick in 90:
		await physics_frame
	var pinned: Transform2D = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM)
	var passed := pinned.origin.distance_to(Vector2(100, 100)) < 2
	PhysicsServer2D.joint_clear(joint)
	PhysicsServer2D.joint_make_damped_spring(joint, Vector2(100, 100), Vector2(100, 50), body)
	PhysicsServer2D.damped_spring_joint_set_param(joint, PhysicsServer2D.DAMPED_SPRING_REST_LENGTH, 50.0)
	for tick in 60:
		await physics_frame
	var sprung: Transform2D = PhysicsServer2D.body_get_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM)
	passed = passed and sprung.origin.is_finite() and sprung.origin.y < 300
	PhysicsServer2D.free_rid(joint)
	PhysicsServer2D.free_rid(body)
	PhysicsServer2D.free_rid(space)
	PhysicsServer2D.free_rid(shape)
	print("RESULT: PASS - fixed world pin and spring anchors" if passed else "RESULT: FAIL - world anchors")
	quit(0 if passed else 1)
