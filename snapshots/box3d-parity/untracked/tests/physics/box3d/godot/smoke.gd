extends SceneTree

func require(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		quit(1)
		assert(condition, message)

func _initialize() -> void:
	require(ClassDB.class_exists("EGPBox3DWorld"), "Box3D module is not registered")
	var world = ClassDB.instantiate("EGPBox3DWorld")
	require(world.configure(60, 4, 1) == OK, "configuration failed")
	require(world.queue_create_box(1, 0, Vector3(0, -0.5, 0), Vector3(20, 0.5, 20), 0, 1.0) == OK, "ground command failed")
	require(world.queue_create_box(2, 0, Vector3(0, 6, 0), Vector3(0.5, 0.5, 0.5), 2, 1.0) == OK, "body command failed")
	require(world.apply_queued_commands() == OK, "baseline failed")
	require(world.get_tick() == 0, "baseline advanced the tick")
	var hashes: Array[String] = []
	var snapshot: PackedByteArray
	for tick in range(1, 181):
		require(world.step_tick(tick) == OK, "step failed")
		hashes.append(world.get_state_hash())
		if tick == 60:
			snapshot = world.capture_snapshot()
	require(not snapshot.is_empty(), "capture failed")
	require(world.restore_snapshot(snapshot) == OK, "restore failed")
	require(world.get_tick() == 60, "restore lost the tick")
	for tick in range(61, 181):
		require(world.step_tick(tick) == OK, "replay step failed")
		require(world.get_state_hash() == hashes[tick - 1], "replay diverged")
	var state: Dictionary = world.get_body_state(2)
	require(state.position.y > 0.3 and state.position.y < 0.7, "body did not settle on ground")
	require(world.step_tick(182) == ERR_INVALID_PARAMETER, "wrong tick was accepted")
	print("BOX3D_GODOT_PASS tick=", world.get_tick(), " hash=", world.get_state_hash(), " profile=", world.get_simulation_fingerprint())
	quit(0)
