extends Node
const Prediction = preload("res://addons/egp_net/egp_net_prediction.gd")
var checks := 0
var finished := false
func require(condition: bool, message: String) -> bool:
	if not condition:
		finish(false, message)
		return false
	checks += 1
	return true

func simulate(world: RefCounted, tick: int, input: PackedByteArray, _replay: bool) -> Error:
	var error: Error = world.queue_linear_velocity(1, tick + 1, Vector3(input[0], 0, 0))
	return error if error != OK else world.step_tick(tick)

func _ready() -> void:
	var authoritative: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	var predicted: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	for world in [authoritative, predicted]:
		if not require(world.configure(60, 4, 1, Vector3.ZERO) == OK, "world configuration"):
			return
		world.queue_create_sphere(1, 1, Vector3.ZERO, 0.5)
		world.apply_queued_commands()
	var history := Prediction.new()
	if not require(history.configure(predicted.capture_snapshot, predicted.restore_snapshot, func(tick: int, input: PackedByteArray, replay: bool): return simulate(predicted, tick, input, replay), 0, 10) == OK, "bounded prediction configuration"):
		return
	for tick in range(1, 11):
		if not require(history.predict(tick, PackedByteArray([1])) == OK, "predicted fixed tick"):
			return
	if not require(history.predict(11, PackedByteArray([1])) == ERR_BUSY, "unacknowledged history pressure"):
		return
	var authoritative_snapshot: PackedByteArray
	for tick in range(1, 11):
		if not require(simulate(authoritative, tick, PackedByteArray([2 if tick == 3 else 1]), false) == OK, "authoritative fixed tick"):
			return
		if tick == 5:
			authoritative_snapshot = authoritative.capture_snapshot()
	if not require(history.reconcile(5, authoritative_snapshot) == OK and history.get_pending_ticks() == 5, "authoritative correction and replay"):
		return
	if not require(predicted.get_state_hash() == authoritative.get_state_hash() and predicted.get_tick() == 10, "native replay reaches authoritative world hash"):
		return
	if not require(history.reconcile(5, authoritative_snapshot) == OK, "stale acknowledgment ignored"):
		return
	if not require(history.reconcile(12, authoritative_snapshot) == ERR_INVALID_PARAMETER, "future acknowledgment rejected"):
		return
	if not require(history.reconcile(10, authoritative.capture_snapshot()) == OK and history.get_pending_ticks() == 0 and history.get_history_bytes() == 0, "acknowledged history released"):
		return
	finish(true, "bounded prediction, authoritative correction and native Box3D input replay")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	print("EGP_NETWORK_PREDICTION " + JSON.stringify({"passed": passed, "message": message, "checks": checks}))
	get_tree().quit(0 if passed else 1)
