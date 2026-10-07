extends SceneTree

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer2D.space_create()
	var state := PhysicsServer2D.space_get_direct_state(space)
	var shape := WorldBoundaryShape2D.new()
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_space(body, space)
	PhysicsServer2D.body_add_shape(body, shape.get_rid())
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape = shape
	query.motion = Vector2(100, 0)
	var no_collision := state.intersect_shape(query).is_empty() and state.get_rest_info(query).is_empty()
	PhysicsServer2D.free_rid(body)
	PhysicsServer2D.free_rid(space)
	shape = null
	query = null
	print("RESULT: PASS - unsupported world boundary collision and queries are explicit" if no_collision else "RESULT: FAIL - unsupported world boundary returned collision")
	quit(0 if no_collision else 1)
