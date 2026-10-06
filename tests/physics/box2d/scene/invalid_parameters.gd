extends SceneTree

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer2D.space_create()
	var state := PhysicsServer2D.space_get_direct_state(space)
	# The runner verifies both diagnostics, normal exit and absence of crash/leak logs.
	if not state.cast_shape(null).is_empty() or not state.cast_shape_all(null, 8).is_empty():
		print("RESULT: FAIL - null casts returned results")
		quit(1)
		return
	var shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(shape, Vector2(10, 0.5))
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_space(body, space)
	PhysicsServer2D.body_add_shape(body, shape)
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape_rid = shape
	query.motion = Vector2(100, 0)
	if not state.intersect_shape(query).is_empty() or not state.cast_shape(query).is_empty() or not state.cast_shape_all(query, 8).is_empty():
		print("RESULT: FAIL - rejected rectangle returned collision results")
		quit(1)
		return
	for rid in [body, shape, space]:
		PhysicsServer2D.free_rid(rid)
	print("RESULT: PASS - invalid queries and degenerate geometry report errors without crashing")
	quit(0)
