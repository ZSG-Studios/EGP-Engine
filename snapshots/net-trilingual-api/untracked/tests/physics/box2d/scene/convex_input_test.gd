extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("run")

func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func run() -> void:
	compare_polygon(PackedVector2Array([Vector2(-20, -10), Vector2(25, -10), Vector2(0, 20)]))
	compare_polygon(PackedVector2Array([Vector2(-20, -10), Vector2(25, -10), Vector2(30, 15), Vector2(-15, 20)]))
	var octagon := PackedVector2Array()
	for index in 8:
		octagon.append(Vector2(25, 0).rotated(index * TAU / 8.0))
	compare_polygon(octagon)
	print("RESULT: PASS - packed and vector convex input geometry" if failures == 0 else "RESULT: FAIL - " + str(failures))
	quit(0 if failures == 0 else 1)

func compare_polygon(points: PackedVector2Array) -> void:
	var space := PhysicsServer2D.space_create()
	var shape := PhysicsServer2D.convex_polygon_shape_create()
	var packed := PackedFloat32Array()
	for index in points.size():
		var normal := (points[(index + 1) % points.size()] - points[index]).orthogonal().normalized()
		packed.append_array(PackedFloat32Array([points[index].x, points[index].y, normal.x, normal.y]))
	var body := PhysicsServer2D.body_create()
	PhysicsServer2D.shape_set_data(shape, points)
	PhysicsServer2D.body_add_shape(body, shape)
	PhysicsServer2D.body_set_state(body, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0.3, Vector2(200, 30)))
	PhysicsServer2D.body_set_space(body, space)
	var state := PhysicsServer2D.space_get_direct_state(space)
	var rays: Array[PhysicsRayQueryParameters2D] = []
	var reference: Array[Dictionary] = []
	for offset in [-5, 0, 5]:
		var ray := PhysicsRayQueryParameters2D.create(Vector2(-100, 30 + offset), Vector2(400, 30 + offset))
		rays.append(ray)
		var hit := state.intersect_ray(ray)
		require(not hit.is_empty() and hit.rid == body, "vector polygon has a ray intersection")
		reference.append(hit)
	PhysicsServer2D.shape_set_data(shape, packed)
	require(PhysicsServer2D.shape_get_data(shape) == packed, "packed point/normal tuples are retained")
	for index in rays.size():
		var hit := state.intersect_ray(rays[index])
		require(not hit.is_empty() and hit.rid == body, "packed polygon has the same ray intersection")
		if not hit.is_empty() and not reference[index].is_empty():
			require(hit.position.distance_to(reference[index].position) < 0.01, "every packed vertex preserves transformed collision geometry")
			require(hit.normal.distance_to(reference[index].normal) < 0.01, "native hull normals agree between input formats")
	var query := PhysicsShapeQueryParameters2D.new()
	query.shape_rid = shape
	query.motion = Vector2(300, 30)
	query.margin = 0.0
	var packed_cast: Dictionary = state.cast_shape(query)
	require(not packed_cast.is_empty() and packed_cast.rid == body, "packed polygon supports shape casts")
	PhysicsServer2D.shape_set_data(shape, points)
	var vector_cast: Dictionary = state.cast_shape(query)
	require(not vector_cast.is_empty() and vector_cast.rid == body, "vector polygon supports shape casts")
	if not packed_cast.is_empty() and not vector_cast.is_empty():
		require(packed_cast.destination.distance_to(vector_cast.destination) < 0.01, "packed/vector casts have the same travel fraction")
	PhysicsServer2D.free_rid(body)
	PhysicsServer2D.free_rid(shape)
	PhysicsServer2D.free_rid(space)
