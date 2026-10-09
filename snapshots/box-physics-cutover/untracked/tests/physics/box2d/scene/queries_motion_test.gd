extends SceneTree
var failures := 0

func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer2D.space_create()
	var shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(shape, Vector2(10, 10))
	var bodies: Array[RID] = []
	for x in [0, 100]:
		var body := PhysicsServer2D.body_create()
		PhysicsServer2D.body_add_shape(body, shape)
		PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(x, 0)))
		PhysicsServer2D.body_set_space(body, space)
		bodies.append(body)
	var state := PhysicsServer2D.space_get_direct_state(space)
	var ray := PhysicsRayQueryParameters2D.create(Vector2(-50, 0), Vector2(150, 0))
	ray.exclude = [bodies[0]]
	var hit := state.intersect_ray(ray)
	require(not hit.is_empty() and hit.rid == bodies[1], "ray exclusions must survive copied filters")
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape_rid = shape
	query.transform = Transform2D(0, Vector2(100, 0))
	query.exclude = [bodies[1]]
	require(state.intersect_shape(query).is_empty(), "overlap exclusions must survive copied filters")
	PhysicsServer2D.body_set_mode(bodies[0], PhysicsServer2D.BODY_MODE_KINEMATIC)
	var motion := PhysicsTestMotionParameters2D.new()
	motion.from = Transform2D(0, Vector2.ZERO)
	motion.motion = Vector2(120, 0)
	require(PhysicsServer2D.body_test_motion(bodies[0], motion), "motion must detect obstacle")
	motion.exclude_bodies = [bodies[1]]
	require(not PhysicsServer2D.body_test_motion(bodies[0], motion), "motion exclusions must be honoured")
	for body in bodies:
		PhysicsServer2D.free_rid(body)
	PhysicsServer2D.free_rid(space)
	PhysicsServer2D.free_rid(shape)
	print("RESULT: PASS - query and motion exclusions" if failures == 0 else "RESULT: FAIL - query or motion")
	quit(0 if failures == 0 else 1)
