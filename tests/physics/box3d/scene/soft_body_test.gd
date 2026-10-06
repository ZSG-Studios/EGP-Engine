extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func _frames(count: int) -> void:
	for frame in count:
		await physics_frame

func _run() -> void:
	var space := PhysicsServer3D.space_create()
	PhysicsServer3D.space_set_active(space, true)
	var floor_shape := PhysicsServer3D.box_shape_create()
	PhysicsServer3D.shape_set_data(floor_shape, Vector3(20, 0.5, 20))
	var floor_body := PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(floor_body, PhysicsServer3D.BODY_MODE_STATIC)
	PhysicsServer3D.body_add_shape(floor_body, floor_shape)
	PhysicsServer3D.body_set_state(floor_body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis(), Vector3(0, -0.5, 0)))
	PhysicsServer3D.body_set_space(floor_body, space)
	var mesh := ArrayMesh.new()
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(0, 2, 0), Vector3(1, 2, 0), Vector3(0, 2, 1), Vector3(1, 2, 1)])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 1, 3, 2])
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var body := PhysicsServer3D.soft_body_create()
	_check(body.is_valid(), "soft body has a real server RID")
	if not body.is_valid():
		quit(1)
		return
	PhysicsServer3D.body_attach_object_instance_id(body, get_instance_id())
	_check(PhysicsServer3D.body_get_object_instance_id(body) == get_instance_id(), "soft body instance identity is routed through body API")
	PhysicsServer3D.soft_body_pin_point(body, 0, true)
	PhysicsServer3D.soft_body_set_mesh(body, mesh.get_rid())
	PhysicsServer3D.soft_body_set_total_mass(body, 2.0)
	PhysicsServer3D.soft_body_set_simulation_precision(body, 8)
	PhysicsServer3D.soft_body_set_space(body, space)
	_check(PhysicsServer3D.soft_body_get_space(body) == space, "soft body attaches to its Box3D world")
	_check(PhysicsServer3D.soft_body_get_total_mass(body) == 2.0, "soft body mass round-trips")
	_check(PhysicsServer3D.soft_body_get_simulation_precision(body) == 8, "soft body precision round-trips")
	var query := PhysicsRayQueryParameters3D.create(Vector3(0.25, 3, 0.25), Vector3(0.25, 1, 0.25))
	var ray := PhysicsServer3D.space_get_direct_state(space).intersect_ray(query)
	_check(not ray.is_empty() and ray.get("rid") == body, "ray hits the triangle interior away from simulation nodes")
	PhysicsServer3D.soft_body_add_collision_exception(body, floor_body)
	_check(PhysicsServer3D.soft_body_get_collision_exceptions(body).has(floor_body), "soft collision exception is recorded")
	var exclusions := PhysicsServer3D.soft_body_get_collision_exceptions(body)
	exclusions.clear()
	_check(PhysicsServer3D.soft_body_get_collision_exceptions(body).has(floor_body), "soft exclusion query returns an owned array")
	PhysicsServer3D.soft_body_remove_collision_exception(body, floor_body)
	await _frames(120)
	_check(PhysicsServer3D.soft_body_get_point_global_position(body, 0).is_equal_approx(Vector3(0, 2, 0)), "pin requested before mesh creation stays fixed")
	_check(PhysicsServer3D.soft_body_get_point_global_position(body, 3).y < 1.9, "unpinned nodes deform under gravity")
	PhysicsServer3D.soft_body_remove_all_pinned_points(body)
	await _frames(240)
	for index in 4:
		var point := PhysicsServer3D.soft_body_get_point_global_position(body, index)
		_check(point.is_finite() and point.y > -0.04, "soft particle collides with the Box3D floor")
	PhysicsServer3D.soft_body_set_space(body, RID())
	var detached := PhysicsServer3D.soft_body_get_point_global_position(body, 3)
	PhysicsServer3D.soft_body_set_space(body, space)
	_check(PhysicsServer3D.soft_body_get_point_global_position(body, 3).is_equal_approx(detached), "space migration preserves deformed positions")
	PhysicsServer3D.soft_body_set_pressure_coefficient(body, 0.0)
	PhysicsServer3D.soft_body_set_drag_coefficient(body, 0.2)
	PhysicsServer3D.soft_body_set_damping_coefficient(body, 0.1)
	PhysicsServer3D.soft_body_apply_point_impulse(body, 3, Vector3(0, 0.2, 0))
	await _frames(60)
	_check(PhysicsServer3D.soft_body_get_bounds(body).size.is_finite(), "deformation bounds remain finite after forces")
	# Freeing a space must detach its soft bodies before destroying the native world.
	PhysicsServer3D.free_rid(space)
	_check(not PhysicsServer3D.soft_body_get_space(body).is_valid(), "space teardown detaches the soft body")
	PhysicsServer3D.free_rid(body)
	PhysicsServer3D.free_rid(floor_body)
	PhysicsServer3D.free_rid(floor_shape)
	print("RESULT: PASS - soft body mesh, pins, contacts, queries and lifetime" if failures == 0 else "RESULT: FAIL - soft bodies")
	quit(0 if failures == 0 else 1)
