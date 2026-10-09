extends Node
const Replay = preload("res://addons/egp_net/egp_net_deterministic_replay.gd")
var checks := 0

func require(condition: bool, message: String) -> bool:
	if not condition:
		push_error(message)
		get_tree().quit(1)
		return false
	checks += 1
	return true

func make_world() -> RefCounted:
	var world: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	world.configure(60, 4, 1, Vector3.ZERO)
	world.queue_create_sphere(1, 1, Vector3.ZERO, 0.5)
	world.queue_create_box(2, 1, Vector3(1.1, 0, 0), Vector3.ONE * 0.5)
	world.apply_queued_commands()
	return world

func simulate(world: RefCounted, tick: int, input: PackedByteArray, _replay: bool) -> Error:
	if input.size() != 1: return ERR_INVALID_DATA
	var error: Error = world.queue_impulse(1, tick + 1, Vector3(input[0] * 0.1, 0, 0))
	return error if error != OK else world.step_tick(tick)

func history(world: RefCounted, max_ticks: int = 128) -> RefCounted:
	var replay := Replay.new()
	replay.configure(world.capture_snapshot, world.restore_snapshot, func(tick: int, input: PackedByteArray, again: bool): return simulate(world, tick, input, again), world.get_state_hash, 0, max_ticks, 65536, 1048576)
	return replay

func _ready() -> void:
	ProjectSettings.set_setting("physics/box3d/audit_determinism", true)
	var authority := make_world()
	var predicted := make_world()
	var replay := history(predicted)
	var frames: Array = []
	for tick in range(1, 9):
		if not require(replay.predict(tick, PackedByteArray([1])) == OK, "predict native physics"): return
		var input := PackedByteArray([2 if tick == 3 else 1])
		if not require(simulate(authority, tick, input, false) == OK, "canonical native physics"): return
		frames.append({"tick": tick, "input": input, "hash": authority.get_state_hash()})
	var before: int = replay.get_history_bytes()
	if not require(replay.accept([frames[1]]) == ERR_INVALID_DATA and replay.get_acknowledged_tick() == 0 and replay.get_history_bytes() == before, "gap rejects entire batch without mutation"): return
	if not require(replay.accept([frames[0], {"tick": 2, "input": 4, "hash": "0000000000000000"}]) == ERR_INVALID_DATA and replay.get_acknowledged_tick() == 0, "malformed later row rejects entire batch"): return
	if not require(replay.accept(frames.slice(0, 5)) == OK and replay.get_pending_ticks() == 3, "canonical inputs trigger local solver rollback"): return
	if not require(predicted.get_state_hash() == authority.get_state_hash(), "complete collision world matches after replay"): return
	if not require(replay.accept(frames.slice(0, 5)) == OK and replay.get_pending_ticks() == 3, "duplicate authoritative window ignored"): return
	if not require(replay.accept(frames.slice(5)) == OK and replay.get_pending_ticks() == 0 and replay.get_acknowledged_tick() == 8, "acknowledged history released"): return
	if not require(replay.get_acknowledged_hash() == authority.get_state_hash(), "acknowledged hash refers to verified authoritative tick"): return
	if not require(authority.get_meta("box3d_audit_verified_steps", 0) == 8 and authority.get_meta("box3d_audit_hash_mismatches", -1) == 0 and predicted.get_meta("box3d_audit_verified_steps", 0) > 8, "native backend mirrors canonical and rollback steps"): return
	if not require(replay.get_history_bytes() == predicted.capture_snapshot().size(), "only current local baseline remains"): return
	var bounded_world := make_world()
	var bounded := history(bounded_world, 2)
	if not require(bounded.predict(1, PackedByteArray([1])) == OK and bounded.predict(2, PackedByteArray([1])) == OK and bounded.predict(3, PackedByteArray([1])) == ERR_BUSY and bounded_world.get_tick() == 2, "history cap refuses simulation before mutation"): return
	var corrupt_world := make_world()
	var corrupt := history(corrupt_world)
	if not require(corrupt.accept([{"tick": 1, "input": PackedByteArray([1]), "hash": "0000000000000000"}]) == ERR_INVALID_DATA and corrupt.predict(2, PackedByteArray([1])) == ERR_UNCONFIGURED, "hash mismatch fails closed"): return
	print("EGP_NETWORK_DETERMINISTIC_REPLAY " + JSON.stringify({"passed": true, "checks": checks, "verified_ticks": 8, "state_hash": authority.get_state_hash()}))
	get_tree().quit()
