extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer2D.space_create()
	var state := PhysicsServer2D.space_get_direct_state(space)
	var shape := PhysicsServer2D.convex_polygon_shape_create()
	var valid := PackedVector2Array([Vector2(-10, -10), Vector2(10, -10), Vector2(10, 10), Vector2(-10, 10)])
	var oversized := PackedVector2Array()
	for index in 9:
		oversized.append(Vector2(25, 0).rotated(index * TAU / 9.0))
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape_rid = shape
	query.motion = Vector2(150, 0)
	var inputs: Array = [
		{},
		PackedFloat32Array([0, 0, 1, 0, 10, 0, 1, 0, 0, 10, 1]),
		PackedVector2Array(),
		PackedVector2Array([Vector2(-10, -10), Vector2(10, 10)]),
		oversized,
		PackedVector2Array([Vector2(-20, 0), Vector2.ZERO, Vector2(20, 0)]),
		PackedVector2Array([Vector2(NAN, 0), Vector2(20, 0), Vector2(0, 20)]),
		PackedVector2Array([Vector2(1e20, 0), Vector2(20, 0), Vector2(0, 20)]),
		PackedFloat32Array([-10, -10, INF, 0, 10, -10, 0, 1, 0, 10, 1, 0]),
		PackedVector2Array([Vector2(0.1, 0), Vector2(0, 0.1), Vector2.ZERO]),
	]
	for input in inputs:
		PhysicsServer2D.shape_set_data(shape, input)
		if not state.cast_shape(query).is_empty():
			failures += 1
	PhysicsServer2D.shape_set_data(shape, valid)
	query.transform = Transform2D(0, Vector2(INF, 0))
	if not state.cast_shape(query).is_empty():
		failures += 1
	query.transform = Transform2D(Vector2.ZERO, Vector2.ZERO, Vector2.ZERO)
	if not state.cast_shape(query).is_empty():
		failures += 1
	query.transform = Transform2D.IDENTITY
	var target_shape := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(target_shape, Vector2(20, 20))
	var target := PhysicsServer2D.body_create()
	PhysicsServer2D.body_add_shape(target, target_shape)
	PhysicsServer2D.body_set_state(target, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(100, 0)))
	PhysicsServer2D.body_set_space(target, space)
	var repaired: Dictionary = state.cast_shape(query)
	if repaired.is_empty() or repaired.rid != target:
		failures += 1
	for rid in [target, target_shape, shape, space]:
		PhysicsServer2D.free_rid(rid)
	print("RESULT: PASS - malformed convex input rejection and recovery" if failures == 0 else "RESULT: FAIL - " + str(failures))
	quit(0 if failures == 0 else 1)
