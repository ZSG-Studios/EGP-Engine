extends SceneTree

var spaces: Array[RID] = []
var bodies: Array = []

func _initialize() -> void:
	call_deferred("run")

func add_body(space: RID, shape: RID, position: Vector2, dynamic: bool) -> RID:
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body, PhysicsServer2D.BODY_MODE_RIGID if dynamic else PhysicsServer2D.BODY_MODE_STATIC)
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, position))
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_CAN_SLEEP, false)
	PhysicsServer2D.body_set_space(body, space)
	return body

func run() -> void:
	var floor_shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(floor_shape, Vector2(1000, 50))
	var box_shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(box_shape, Vector2(24, 24))
	for world in 2:
		var space := PhysicsServer2D.space_create()
		PhysicsServer2D.space_set_active(space, true)
		spaces.append(space)
		var world_bodies: Array[RID] = [add_body(space, floor_shape, Vector2(0, 500), false)]
		for index in 24:
			world_bodies.append(add_body(space, box_shape, Vector2((index % 4) * 52 - 78, 400 - (index / 4) * 52), true))
		bodies.append(world_bodies)
	var passed := true
	for tick in 300:
		await physics_frame
		for index in bodies[0].size():
			for state in [PhysicsServer2D.BODY_STATE_TRANSFORM, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, PhysicsServer2D.BODY_STATE_ANGULAR_VELOCITY]:
				if PhysicsServer2D.body_get_state(bodies[0][index], state) != PhysicsServer2D.body_get_state(bodies[1][index], state):
					print("Mismatch at tick ", tick, " body ", index, " state ", state)
					passed = false
		if not passed:
			break
		if tick == 30 or tick == 120:
			for world in 2:
				PhysicsServer2D.body_apply_central_impulse(bodies[world][5], Vector2(75, -150))
		if tick == 150:
			for world in 2:
				PhysicsServer2D.free_rid(bodies[world][10])
				bodies[world][10] = add_body(spaces[world], box_shape, Vector2(0, 0), true)
	var fallen: Transform2D = PhysicsServer2D.body_get_state(bodies[0][24], PhysicsServer2D.BODY_STATE_TRANSFORM)
	passed = passed and fallen.origin.y > 150 and fallen.origin.y < 500
	for world_bodies in bodies:
		for body in world_bodies:
			PhysicsServer2D.free_rid(body)
	for space in spaces:
		PhysicsServer2D.free_rid(space)
	for shape in [floor_shape, box_shape]:
		PhysicsServer2D.free_rid(shape)
	print("RESULT: PASS - exact 300 tick twin worlds with contacts, impulses and replacement" if passed else "RESULT: FAIL - trajectory mismatch or inactive physics")
	quit(0 if passed else 1)
