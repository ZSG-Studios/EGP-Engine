extends SceneTree

var failures := 0

func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func box_node(offset: Vector3) -> CollisionShape3D:
	var node := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = Vector3.ONE
	node.shape = shape
	node.position = offset
	return node

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var compound := StaticBody3D.new()
	compound.add_child(box_node(Vector3.ZERO))
	compound.add_child(box_node(Vector3(3, 0, 0)))
	root.add_child(compound)
	var mover := CharacterBody3D.new()
	mover.position = Vector3(3, 3, 0)
	mover.add_child(box_node(Vector3.ZERO))
	root.add_child(mover)
	await physics_frame
	await physics_frame
	var state := compound.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(Vector3(3, 2, 0), Vector3(3, -2, 0))
	var hit := state.intersect_ray(query)
	require(not hit.is_empty() and hit.get("rid") == compound.get_rid() and hit.get("shape") == 1, "ray must identify compound shape 1")
	query.exclude = [compound.get_rid()]
	require(state.intersect_ray(query).is_empty(), "ray exclusion must reach native callback")
	var point := PhysicsPointQueryParameters3D.new()
	point.position = Vector3(3, 0, 0)
	var hits := state.intersect_point(point)
	require(hits.size() == 1 and hits[0].get("shape") == 1, "point query must identify compound shape 1")
	point.exclude = [compound.get_rid()]
	require(state.intersect_point(point).is_empty(), "point exclusion must reach native callback")
	var motion := PhysicsTestMotionParameters3D.new()
	motion.from = mover.global_transform
	motion.motion = Vector3(0, -5, 0)
	var result := PhysicsTestMotionResult3D.new()
	require(PhysicsServer3D.body_test_motion(mover.get_rid(), motion, result), "motion must hit compound")
	if result.get_collision_count() > 0:
		require(result.get_collider_shape() == 1, "motion must identify collider shape 1")
	motion.exclude_bodies = [compound.get_rid()]
	require(not PhysicsServer3D.body_test_motion(mover.get_rid(), motion, result), "motion body exclusion must reach native callback")
	motion.exclude_bodies = []
	motion.exclude_objects = [compound.get_instance_id()]
	require(not PhysicsServer3D.body_test_motion(mover.get_rid(), motion, result), "motion object exclusion must reach native callback")
	mover.queue_free()
	compound.queue_free()
	await process_frame
	print("RESULT: PASS - native shape indices and exclusions" if failures == 0 else "RESULT: FAIL - native indices or exclusions")
	quit(0 if failures == 0 else 1)
