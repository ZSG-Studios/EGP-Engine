extends SceneTree

func _initialize() -> void:
	var shape := WorldBoundaryShape2D.new()
	shape.normal = Vector2(0, -1)
	shape.distance = 24.0
	var rid := shape.get_rid()
	var passed := rid.is_valid() and PhysicsServer2D.shape_get_type(rid) == PhysicsServer2D.SHAPE_WORLD_BOUNDARY
	var data: Array = PhysicsServer2D.shape_get_data(rid)
	passed = passed and data.size() == 2 and data[0] == Vector2(0, -1) and is_equal_approx(data[1], 24.0)
	var path := "user://world-boundary-resource.tres"
	passed = passed and ResourceSaver.save(shape, path) == OK
	var restored := ResourceLoader.load(path, "WorldBoundaryShape2D", ResourceLoader.CACHE_MODE_IGNORE) as WorldBoundaryShape2D
	passed = passed and restored != null and restored.normal == shape.normal and is_equal_approx(restored.distance, shape.distance)
	shape = null
	restored = null
	DirAccess.remove_absolute(path)
	print("RESULT: PASS - world boundary resource lifecycle without collision support" if passed else "RESULT: FAIL - world boundary resource lifecycle")
	quit(0 if passed else 1)
