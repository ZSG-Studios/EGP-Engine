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
	var shape := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(shape, 20.0)
	var first := PhysicsServer2D.body_create()
	var second := PhysicsServer2D.body_create()
	for body in [first, second]:
		PhysicsServer2D.body_add_shape(body, shape)
		PhysicsServer2D.body_set_space(body, space)
	PhysicsServer2D.body_set_state(second, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(100, 0)))
	var state := PhysicsServer2D.space_get_direct_state(space)
	var ray := PhysicsRayQueryParameters2D.create(Vector2(100, -100), Vector2(100, 100))
	var hit := state.intersect_ray(ray)
	require(not hit.is_empty() and absf(hit.get("position", Vector2.ZERO).y + 20) < 0.1, "initial shared shape")
	PhysicsServer2D.free_rid(first)
	PhysicsServer2D.shape_set_data(shape, 40.0)
	hit = state.intersect_ray(ray)
	require(not hit.is_empty() and absf(hit.get("position", Vector2.ZERO).y + 40) < 0.1, "shared shape update after freeing owner")
	var compound := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_state(compound, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(0, 1000)))
	PhysicsServer2D.body_set_space(compound, space)
	for index in 12:
		PhysicsServer2D.body_add_shape(compound, shape, Transform2D(0, Vector2(index * 100, 0)))
	var compound_ray := PhysicsRayQueryParameters2D.create(Vector2(1100, 900), Vector2(1100, 1100))
	hit = state.intersect_ray(compound_ray)
	require(not hit.is_empty() and hit.shape == 11, "compound growth must update solver user data")
	PhysicsServer2D.body_remove_shape(compound, 0)
	hit = state.intersect_ray(compound_ray)
	require(not hit.is_empty() and hit.shape == 10, "compound removal must update solver user data and indices")
	var replacement := PhysicsServer2D.circle_shape_create()
	PhysicsServer2D.shape_set_data(replacement, 10.0)
	PhysicsServer2D.body_set_shape(compound, 10, replacement)
	PhysicsServer2D.free_rid(replacement)
	require(PhysicsServer2D.body_get_shape_count(compound) == 10, "replaced shape ownership must be tracked")
	var dynamic_body := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(dynamic_body, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_add_shape(dynamic_body, shape)
	PhysicsServer2D.body_set_space(dynamic_body, space)
	var joint := PhysicsServer2D.joint_create()
	PhysicsServer2D.joint_make_pin(joint, Vector2(50, 0), second, dynamic_body)
	PhysicsServer2D.free_rid(second)
	PhysicsServer2D.free_rid(space)
	require(not PhysicsServer2D.body_get_space(dynamic_body).is_valid(), "space deletion detaches survivors")
	PhysicsServer2D.free_rid(shape)
	require(PhysicsServer2D.body_get_shape_count(dynamic_body) == 0, "shape deletion detaches survivors")
	PhysicsServer2D.free_rid(joint)
	PhysicsServer2D.free_rid(dynamic_body)
	PhysicsServer2D.free_rid(compound)
	print("RESULT: PASS - native shared shape, joint and space lifetimes" if failures == 0 else "RESULT: FAIL - native lifetimes")
	quit(0 if failures == 0 else 1)
