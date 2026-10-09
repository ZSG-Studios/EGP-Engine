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
	var shape := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(shape, 10.0)
	var canvas := CanvasLayer.new()
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_attach_canvas_instance_id(body, canvas.get_instance_id())
	PhysicsServer2D.body_set_space(body, space)
	var area := PhysicsServer2D.area_create()
	PhysicsServer2D.area_add_shape(area, shape)
	PhysicsServer2D.area_attach_canvas_instance_id(area, canvas.get_instance_id())
	PhysicsServer2D.area_set_space(area, space)
	var state := PhysicsServer2D.space_get_direct_state(space)
	var point := PhysicsPointQueryParameters2D.new()
	point.collide_with_areas = true
	require(state.intersect_point(point).is_empty(), "default canvas excludes custom-canvas body and area")
	point.canvas_instance_id = canvas.get_instance_id()
	var hits := state.intersect_point(point)
	require(hits.size() == 2, "matching canvas returns both body and area")
	point.exclude = [body]
	hits = state.intersect_point(point)
	require(hits.size() == 1 and hits[0].rid == area, "canvas query combines RID exclusions")
	point.collide_with_areas = false
	require(state.intersect_point(point).is_empty(), "canvas query combines area/body filters")
	PhysicsServer2D.body_attach_canvas_instance_id(body, 0)
	point.canvas_instance_id = 0
	point.exclude = []
	hits = state.intersect_point(point)
	require(hits.size() == 1 and hits[0].rid == body, "changing body canvas updates native query filtering")
	PhysicsServer2D.free_rid(area)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(50, 0)))
	var farther := PhysicsServer2D.body_create()
	PhysicsServer2D.body_add_shape(farther, shape)
	PhysicsServer2D.body_set_state(farther, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(100, 0)))
	PhysicsServer2D.body_set_space(farther, space)
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape_rid = shape
	query.motion = Vector2(150, 0)
	query.margin = 0.0
	var nearest: Dictionary = state.cast_shape(query)
	# Box2D's default pixel scale gives shape casts a 0.5-pixel linear tolerance.
	require(not nearest.is_empty() and nearest.rid == body and absf(nearest.destination.x - 30.0) < 1.0, "native cast returns nearest collision and travel destination")
	hits = state.cast_shape_all(query, 8)
	require(hits.size() == 2 and hits[0].rid == body and hits[1].rid == farther, "native all-hit cast sorts collisions by distance")
	query.exclude = [body]
	nearest = state.cast_shape(query)
	require(not nearest.is_empty() and nearest.rid == farther, "native casts apply copied RID exclusions")
	require(state.cast_shape_all(query, 0).is_empty(), "zero-result cast returns an empty array")
	query.motion = Vector2(-150, 0)
	require(state.cast_shape(query).is_empty() and state.cast_shape_all(query, 8).is_empty(), "native casts return empty results when moving away")
	for rid in [body, farther, shape, space]:
		PhysicsServer2D.free_rid(rid)
	canvas.free()
	print("RESULT: PASS - native canvas filters and shape casts" if failures == 0 else "RESULT: FAIL - " + str(failures))
	quit(0 if failures == 0 else 1)
