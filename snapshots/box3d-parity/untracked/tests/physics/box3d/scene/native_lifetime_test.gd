extends SceneTree

var failures := 0

func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var space := PhysicsServer3D.space_create()
	var shape := PhysicsServer3D.sphere_shape_create()
	PhysicsServer3D.shape_set_data(shape, 0.5)
	var first := PhysicsServer3D.body_create()
	var second := PhysicsServer3D.body_create()
	for body in [first, second]:
		PhysicsServer3D.body_set_mode(body, PhysicsServer3D.BODY_MODE_STATIC)
		PhysicsServer3D.body_add_shape(body, shape)
		PhysicsServer3D.body_set_space(body, space)
	PhysicsServer3D.body_set_state(second, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis(), Vector3(4, 0, 0)))
	var state := PhysicsServer3D.space_get_direct_state(space)
	var ray := PhysicsRayQueryParameters3D.create(Vector3(4, 3, 0), Vector3(4, -3, 0))
	var hit := state.intersect_ray(ray)
	require(not hit.is_empty() and absf(hit.get("position", Vector3.ZERO).y - 0.5) < 0.01, "initial shared geometry")
	PhysicsServer3D.shape_set_data(shape, 1.0)
	hit = state.intersect_ray(ray)
	require(not hit.is_empty() and absf(hit.get("position", Vector3.ZERO).y - 1.0) < 0.01, "live shared geometry must update")
	PhysicsServer3D.free_rid(first)
	PhysicsServer3D.shape_set_data(shape, 1.5)
	hit = state.intersect_ray(ray)
	require(not hit.is_empty() and absf(hit.get("position", Vector3.ZERO).y - 1.5) < 0.01, "freed owner must leave shared geometry registry")
	var dynamic_body := PhysicsServer3D.body_create()
	PhysicsServer3D.body_set_mode(dynamic_body, PhysicsServer3D.BODY_MODE_RIGID)
	PhysicsServer3D.body_add_shape(dynamic_body, shape)
	PhysicsServer3D.body_set_state(dynamic_body, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis(), Vector3(8, 0, 0)))
	PhysicsServer3D.body_set_space(dynamic_body, space)
	var joint := PhysicsServer3D.joint_create()
	PhysicsServer3D.joint_make_pin(joint, second, Vector3(2, 0, 0), dynamic_body, Vector3(-2, 0, 0))
	PhysicsServer3D.free_rid(second)
	PhysicsServer3D.pin_joint_set_local_b(joint, Vector3(-1, 0, 0))
	PhysicsServer3D.free_rid(space)
	require(not PhysicsServer3D.body_get_space(dynamic_body).is_valid(), "space deletion must detach surviving bodies")
	PhysicsServer3D.free_rid(shape)
	require(PhysicsServer3D.body_get_shape_count(dynamic_body) == 0, "shape deletion must detach surviving attachments")
	PhysicsServer3D.free_rid(joint)
	PhysicsServer3D.free_rid(dynamic_body)
	print("RESULT: PASS - shared shape, joint and space lifetimes" if failures == 0 else "RESULT: FAIL - native lifetime")
	quit(0 if failures == 0 else 1)
