extends Node
const Adapter = preload("res://addons/egp_net/egp_net_box3d_prediction.gd")
var checks := 0

class FakeSession extends RefCounted:
	var state := "Connected"
	var sent: Array = []
	func get_state() -> String: return state
	func send_application(peer: int, payload: PackedByteArray) -> Error:
		sent.append({"peer":peer,"payload":payload.duplicate()})
		return OK

class WorldProvider extends Node:
	signal application_received(peer: int, payload: PackedByteArray)
	var native := FakeSession.new()
	var world: RefCounted
	func get_session() -> RefCounted: return native
	func get_simulation_fingerprint() -> String: return world.get_simulation_fingerprint()
	func get_tick_rate() -> int: return 60

func require(condition: bool, label: String) -> bool:
	checks += 1
	if not condition:
		push_error(label)
		get_tree().quit(1)
	return condition

func make_world() -> RefCounted:
	var world: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	world.configure(60, 4, 1, Vector3.ZERO)
	world.queue_create_sphere(1, 1, Vector3.ZERO, 0.5)
	world.queue_create_box(2, 1, Vector3(1.1, 0, 0), Vector3.ONE * 0.5)
	world.apply_queued_commands()
	return world

func apply_input(world: RefCounted, tick: int, input: PackedByteArray, _replay: bool) -> Error:
	return world.queue_impulse(1, tick + 1, Vector3(input.decode_s16(0) / 32767.0 * 0.1, 0, 0))

func input_for_tick(_tick: int) -> PackedByteArray:
	return Adapter.encode_player_input(1, 0, 0)

func _ready() -> void:
	var authority := make_world()
	var predicted := make_world()
	var genesis: PackedByteArray = predicted.capture_snapshot()
	var provider := WorldProvider.new()
	provider.world = predicted
	add_child(provider)
	var visual := Node3D.new()
	add_child(visual)
	var inspector := Adapter.new()
	inspector.auto_predict = false
	inspector.send_inputs = false
	add_child(inspector)
	inspector.world_provider_path = inspector.get_path_to(provider)
	inspector.session_path = inspector.get_path_to(provider)
	inspector.hook_node_path = inspector.get_path_to(self)
	inspector.presentation_path = inspector.get_path_to(visual)
	if not require(inspector._get_configuration_warnings().is_empty(), "Complete Inspector bindings have no warnings"): return
	inspector.world_property = &"missing_world"
	if not require(not inspector._get_configuration_warnings().is_empty() and inspector._bind_inspector() == ERR_INVALID_PARAMETER, "Missing world property reports feedback without invalid getter"): return
	inspector.world_property = &"world"
	inspector.input_method = &"missing_input"
	if not require(not inspector._get_configuration_warnings().is_empty() and inspector._bind_inspector() == ERR_INVALID_PARAMETER, "Missing input hook reports feedback before binding"): return
	inspector.input_method = &"input_for_tick"
	if not require(inspector._get_configuration_warnings().is_empty() and inspector._bind_inspector() == OK, "Inspector-only paths and hooks start native prediction"): return
	if not require(inspector.stop_prediction() == OK, "Inspector configured journal stops cleanly"): return
	var adapter := Adapter.new()
	adapter.auto_predict = false
	adapter.send_inputs = false
	add_child(adapter)
	if not require(adapter.start_prediction(predicted, provider, func(_tick: int): return Adapter.encode_player_input(1, 0, 0), apply_input) == OK, "Inspector adapter binds explicit synchronized Box3D genesis"): return
	var canonical: Array = []
	for tick in range(1, 9):
		if not require(adapter.predict_next() == OK, "Predict complete local world"): return
		var input: PackedByteArray = Adapter.encode_player_input(-1 if tick == 3 else 1, 0, 0)
		apply_input(authority, tick, input, false)
		authority.step_tick(tick)
		canonical.append({"tick":tick,"input":input,"hash":authority.get_state_hash()})
	if not require(adapter.accept_authority(canonical.slice(2)) == OK and adapter.get_statistics().acknowledged_tick == 0, "Delayed/reordered authority queues without premature restore"): return
	if not require(adapter.accept_authority([canonical[0]]) == OK and adapter.get_statistics().acknowledged_tick == 1, "First contiguous frame verified independently"): return
	if not require(adapter.accept_authority([canonical[1]]) == OK and adapter.get_statistics().acknowledged_tick == 8, "Missing retransmission flushes contiguous reordered journal"): return
	if not require(predicted.get_state_hash() == authority.get_state_hash(), "Corrected entire collision world matches native canonical hash"): return
	if not require(adapter.accept_authority(canonical) == OK and adapter.get_statistics().queued_authority == 0, "Duplicate replay window idempotent"): return
	if not require(adapter.get_statistics().history_bytes == predicted.capture_snapshot().size(), "Acknowledged input snapshots released"): return
	if not require(predicted.restore_snapshot(genesis) == OK and adapter.reset_local(1) == OK, "Explicit local trusted-genesis epoch reset"): return
	if not require(adapter.accept_authority(canonical, 0) == ERR_INVALID_DATA and adapter.get_statistics().tick == 0, "Late old epoch cannot corrupt reset baseline"): return
	# Native application prefix is dispatched by SuperpositionWorld, with authenticated peer0.
	var sender_provider := WorldProvider.new()
	sender_provider.world = authority
	sender_provider.native.state = "Listening"
	add_child(sender_provider)
	var sender := Adapter.new()
	sender.auto_predict = false
	sender.send_inputs = false
	add_child(sender)
	sender.start_prediction(authority, sender_provider, func(_tick: int): return Adapter.encode_player_input(1, 0, 0), apply_input)
	sender.reset_local(1)
	if not require(sender.publish_authority(canonical[0], PackedInt64Array([12])) == OK, "Authority emits bounded native reliable SPP1 namespace"): return
	var packet: PackedByteArray = sender_provider.native.sent.back().payload
	provider.application_received.emit(9, packet)
	if not require(adapter.get_statistics().acknowledged_tick == 0, "Non-authority peer cannot inject canonical inputs"): return
	provider.application_received.emit(0, packet.slice(0, 20))
	provider.application_received.emit(0, PackedByteArray([1,2,3,4]))
	if not require(adapter.get_statistics().acknowledged_tick == 0, "Truncated/foreign application namespace ignored"): return
	provider.application_received.emit(0, packet)
	if not require(adapter.get_statistics().acknowledged_tick == 1, "Native prefix reaches canonical verification without remote solver snapshot"): return
	var bad: Dictionary = canonical[1].duplicate(true)
	bad.hash = "0000000000000000"
	if not require(adapter.accept_authority([bad], 1) == ERR_INVALID_DATA and adapter.get_statistics().failed, "Corrupt authoritative hash fails closed"): return
	if not require(adapter.predict_next() == ERR_UNCONFIGURED, "Failed solver cannot continue predicting"): return
	adapter.stop_prediction()
	sender.stop_prediction()
	print("EGP_BOX3D_PREDICTION_PASS " + JSON.stringify({"checks":checks,"verified_ticks":8,"reorder":true,"trusted_epoch_reset":true,"hash_fail_closed":true}))
	get_tree().quit()
