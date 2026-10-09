extends SceneTree

func _initialize() -> void:
	var body := PhysicsServer2D.body_create()
	var other := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_contacts_reported_depth_threshold(body, 0.2)
	var passed := is_equal_approx(PhysicsServer2D.body_get_contacts_reported_depth_threshold(body), 0.2)
	PhysicsServer2D.body_add_collision_exception(body, other)
	var exclusions := PhysicsServer2D.body_get_collision_exceptions(body)
	passed = passed and exclusions == [other]
	exclusions.clear()
	passed = passed and PhysicsServer2D.body_get_collision_exceptions(body).has(other)
	PhysicsServer2D.body_remove_collision_exception(body, other)
	passed = passed and PhysicsServer2D.body_get_collision_exceptions(body).is_empty()
	PhysicsServer2D.free_rid(other)
	PhysicsServer2D.free_rid(body)
	print("RESULT: PASS - contact threshold and owned exclusion queries" if passed else "RESULT: FAIL - server queries")
	quit(0 if passed else 1)
