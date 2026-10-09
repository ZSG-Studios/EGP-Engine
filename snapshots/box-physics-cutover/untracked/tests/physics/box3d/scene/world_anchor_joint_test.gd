extends SceneTree

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var body := RigidBody3D.new()
	body.position = Vector3(0, 3, 0)
	body.can_sleep = false
	var collider := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = 0.25
	collider.shape = sphere
	body.add_child(collider)
	root.add_child(body)
	var joint := PhysicsServer3D.joint_create()
	PhysicsServer3D.joint_make_pin(joint, body.get_rid(), Vector3(0, 1, 0), RID(), Vector3(0, 4, 0))
	for frame in 120:
		await physics_frame
	var passed := absf(body.position.y - 3.0) < 0.2 and body.linear_velocity.length() < 0.1
	PhysicsServer3D.free_rid(joint)
	body.queue_free()
	await process_frame
	print("RESULT: PASS - body remains pinned to world anchor" if passed else "RESULT: FAIL - world anchor did not constrain body")
	quit(0 if passed else 1)
