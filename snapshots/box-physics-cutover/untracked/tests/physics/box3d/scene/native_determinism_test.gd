extends SceneTree

var spaces: Array[RID] = []
var bodies: Array = []
var shapes: Array[RID] = []

func _initialize() -> void:
	call_deferred("run")

func add_body(space: RID, shape: RID, position: Vector3, dynamic: bool) -> RID:
	var body := PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(body, PhysicsServer3D.BODY_MODE_RIGID if dynamic else PhysicsServer3D.BODY_MODE_STATIC)
	PhysicsServer3D.body_add_shape(body, shape)
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis.IDENTITY, position))
	PhysicsServer3D.body_set_state(body, PhysicsServer3D.BODY_STATE_CAN_SLEEP, false)
	PhysicsServer3D.body_set_space(body, space)
	return body

func run() -> void:
	var floor_shape := PhysicsServer3D.box_shape_create()
	PhysicsServer3D.shape_set_data(floor_shape, Vector3(10, 0.5, 10))
	var box_shape := PhysicsServer3D.box_shape_create()
	PhysicsServer3D.shape_set_data(box_shape, Vector3(0.48, 0.48, 0.48))
	shapes.assign([floor_shape, box_shape])
	for world in 2:
		var space := PhysicsServer3D.space_create()
		PhysicsServer3D.space_set_active(space, true)
		spaces.append(space)
		var world_bodies: Array[RID] = [add_body(space, floor_shape, Vector3(0, -0.5, 0), false)]
		for index in 24:
			world_bodies.append(add_body(space, box_shape, Vector3((index % 4) * 1.05 - 1.5, 1.0 + (index / 4) * 1.02, 0), true))
		bodies.append(world_bodies)
	var passed := true
	for tick in 300:
		await physics_frame
		for index in bodies[0].size():
			for state in [PhysicsServer3D.BODY_STATE_TRANSFORM, PhysicsServer3D.BODY_STATE_LINEAR_VELOCITY, PhysicsServer3D.BODY_STATE_ANGULAR_VELOCITY]:
				if PhysicsServer3D.body_get_state(bodies[0][index], state) != PhysicsServer3D.body_get_state(bodies[1][index], state):
					print("RESULT: FAIL - independent worlds differ at tick ", tick, " body ", index, " state ", state)
					passed = false
		if not passed:
			break
		if tick == 30 or tick == 120:
			for world in 2:
				PhysicsServer3D.body_apply_central_impulse(bodies[world][5], Vector3(0.75, 1.5, 0.1))
		if tick == 150:
			for world in 2:
				PhysicsServer3D.free_rid(bodies[world][10])
				bodies[world][10] = add_body(spaces[world], box_shape, Vector3(0, 8, 0), true)
	# Exact agreement alone could also describe two inert worlds.
	var fallen: Transform3D = PhysicsServer3D.body_get_state(bodies[0][24], PhysicsServer3D.BODY_STATE_TRANSFORM)
	passed = passed and fallen.origin.y < 5.5 and fallen.origin.y > 0.2
	for world_bodies in bodies:
		for body in world_bodies:
			PhysicsServer3D.free_rid(body)
	for space in spaces:
		PhysicsServer3D.free_rid(space)
	for shape in shapes:
		PhysicsServer3D.free_rid(shape)
	print("RESULT: PASS - independent scene worlds match all 300 ticks including contacts and lifecycle" if passed else "RESULT: FAIL - scene trajectory mismatch or inactive simulation")
	quit(0 if passed else 1)
