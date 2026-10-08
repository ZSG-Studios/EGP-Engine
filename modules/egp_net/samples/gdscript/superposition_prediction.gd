extends Node
var checks := 0

func require(value: bool, message: String) -> bool:
	checks += 1
	if not value:
		push_error(message)
		get_tree().quit(1)
	return value

func make_world() -> RefCounted:
	var world: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	world.configure(60, 4, 1, Vector3.ZERO)
	world.queue_create_sphere(1, 1, Vector3.ZERO, 0.5)
	world.queue_create_box(2, 1, Vector3(1.1, 0, 0), Vector3.ONE * 0.5)
	world.apply_queued_commands()
	return world

func simulate(world: RefCounted, tick: int, input: PackedByteArray, _replay: bool) -> Error:
	if input.size() != 1: return ERR_INVALID_DATA
	var result: Error = world.queue_impulse(1, tick + 1, Vector3(input[0] * 0.1, 0, 0))
	return result if result != OK else world.step_tick(tick)

func journal(world: RefCounted, capacity: int = 128) -> RefCounted:
	var replay: RefCounted = ClassDB.instantiate("SuperpositionPrediction")
	replay.configure(world.capture_snapshot, world.restore_snapshot, func(tick: int, input: PackedByteArray, again: bool): return simulate(world,tick,input,again), world.get_state_hash, 0, capacity, 65536, 1048576)
	return replay

func _ready() -> void:
	ProjectSettings.set_setting("physics/box3d/audit_determinism", true)
	var authority := make_world()
	var predicted := make_world()
	var genesis: PackedByteArray = predicted.capture_snapshot()
	var replay := journal(predicted)
	var frames: Array = []
	for tick in range(1, 9):
		if not require(replay.predict(tick, PackedByteArray([1])) == OK, "Predict native complete collision world"): return
		var input := PackedByteArray([2 if tick == 3 else 1])
		simulate(authority,tick,input,false)
		frames.append({"tick":tick,"input":input,"hash":authority.get_state_hash()})
	var bytes: int = replay.get_history_bytes()
	if not require(replay.accept([frames[1]]) == ERR_INVALID_DATA and replay.get_history_bytes() == bytes and predicted.get_tick() == 8, "Gap preflight cannot mutate solver/journal"): return
	if not require(replay.accept([frames[0], {"tick":2,"input":4,"hash":"0000000000000000"}]) == ERR_INVALID_DATA and replay.get_acknowledged_tick() == 0, "Malformed later row preflight before any authority mutation"): return
	if not require(replay.accept(frames.slice(0,5)) == OK and replay.get_pending_ticks() == 3, "Native canonical inputs reconcile predicted history"): return
	if not require(predicted.get_state_hash() == authority.get_state_hash(), "Exact whole-world native solver hash after rollback"): return
	if not require(replay.get_statistics().corrections == 1 and replay.get_statistics().replayed_ticks == 6, "Native reconciliation metrics count actual solver replay"): return
	if not require(replay.accept(frames.slice(0,5)) == OK and replay.get_pending_ticks() == 3, "Duplicate acknowledged windows idempotent"): return
	if not require(replay.accept(frames.slice(5)) == OK and replay.get_acknowledged_tick() == 8 and replay.get_acknowledged_hash() == authority.get_state_hash(), "All hashes verified and acknowledged history released"): return
	if not require(replay.get_history_bytes() == predicted.capture_snapshot().size(), "Memory includes only trusted baseline after all acknowledgments"): return
	var bounded_world := make_world()
	var bounded := journal(bounded_world,2)
	if not require(bounded.predict(1,PackedByteArray([1])) == OK and bounded.predict(2,PackedByteArray([1])) == OK and bounded.predict(3,PackedByteArray([1])) == ERR_BUSY and bounded_world.get_tick() == 2, "Native capacity refuses tick before game callback mutation"): return
	if not require(predicted.restore_snapshot(genesis) == OK and replay.reset(1,0) == OK, "Reset captures caller-restored local trusted genesis"): return
	if not require(replay.accept(frames,0) == ERR_INVALID_PARAMETER and predicted.get_tick() == 0, "Stale epoch cannot restore/replay after reset"): return
	if not require(replay.reset(1,0) == ERR_INVALID_PARAMETER and replay.reset(0,0) == ERR_INVALID_PARAMETER, "Reset epochs strictly monotonic"): return
	if not require(replay.accept(frames,1) == OK and predicted.get_state_hash() == authority.get_state_hash(), "New epoch authority simulates from local baseline without remote snapshot bytes"): return
	var corrupt_world := make_world()
	var corrupt := journal(corrupt_world)
	if not require(corrupt.accept([{"tick":1,"input":PackedByteArray([1]),"hash":"0000000000000000"}]) == ERR_INVALID_DATA and not corrupt.get_statistics().ready and corrupt.get_statistics().hash_failures == 1, "Hash mismatch fails native journal closed"): return
	if not require(corrupt.predict(2,PackedByteArray([1])) == ERR_UNCONFIGURED and corrupt.get_history_bytes() == 0, "Failed journal refuses later simulation and frees history"): return
	var reentrant: RefCounted = ClassDB.instantiate("SuperpositionPrediction")
	var reentrant_world := make_world()
	var reentry_results: Array[int] = []
	var journal_weak: WeakRef = weakref(reentrant)
	var callback := func():
		var current: RefCounted = journal_weak.get_ref()
		reentry_results.append(current.predict(1,PackedByteArray([1])))
		return reentrant_world.capture_snapshot()
	if not require(reentrant.configure(callback, reentrant_world.restore_snapshot, func(tick: int,input: PackedByteArray,again: bool): return simulate(reentrant_world,tick,input,again),reentrant_world.get_state_hash) == OK and reentry_results[0] == ERR_UNCONFIGURED, "Capture callback cannot reenter unconfigured prediction"): return
	if not require(reentrant.predict(1,PackedByteArray([1])) == OK and reentry_results.back() == ERR_BUSY, "Capture callback cannot reenter busy prediction"): return
	# Releasing the final external reference from a callback must not free the
	# native journal while its public operation still executes.
	var lifetime_world := make_world()
	var holder: Array = [ClassDB.instantiate("SuperpositionPrediction")]
	var lifetime_weak: WeakRef = weakref(holder[0])
	var callback_checks: Array[int] = []
	var capture_drop := func():
		holder.clear()
		var current: RefCounted = lifetime_weak.get_ref()
		callback_checks.append(current.reset(1,0) if current != null else ERR_BUG)
		return lifetime_world.capture_snapshot()
	var configured: Error = holder[0].configure(capture_drop,lifetime_world.restore_snapshot,func(tick: int,input: PackedByteArray,again: bool): return simulate(lifetime_world,tick,input,again),lifetime_world.get_state_hash)
	if not require(configured == OK and callback_checks[0] == ERR_BUSY and lifetime_weak.get_ref() == null, "Configure callback dropping external finalref survives and releases after completion"): return
	var failing_holder: Array = [ClassDB.instantiate("SuperpositionPrediction")]
	var failing_weak: WeakRef = weakref(failing_holder[0])
	var failing_world := make_world()
	var failures: Array[int] = []
	failing_holder[0].configure(failing_world.capture_snapshot, failing_world.restore_snapshot, func(_tick: int,_input: PackedByteArray,_again: bool): return ERR_INVALID_DATA, failing_world.get_state_hash)
	failing_holder[0].resync_required.connect(func(_error: Error):
		failing_holder.clear()
		var current: RefCounted = failing_weak.get_ref()
		failures.append(current.reset(1,0) if current != null else ERR_BUG)
	)
	var failed: Error = failing_holder[0].predict(1,PackedByteArray([1]))
	if not require(failed == ERR_INVALID_DATA and failures[0] == ERR_BUSY and failing_weak.get_ref() == null, "Failure callback cannot reset active operation or free native instance before unwind"): return
	var invalid_hash: RefCounted = ClassDB.instantiate("SuperpositionPrediction")
	if not require(invalid_hash.configure(lifetime_world.capture_snapshot,lifetime_world.restore_snapshot,simulate,func(): return "+123456789abcdef") == ERR_INVALID_DATA, "Signed hexadecimal is not a valid16digit state digest"): return
	print("EGP_SUPERPOSITION_PREDICTION_PASS " + JSON.stringify({"checks":checks,"verified_ticks":8,"corrections":replay.get_statistics().corrections,"typed_native_api":true,"hash_fail_closed":true}))
	get_tree().quit()
