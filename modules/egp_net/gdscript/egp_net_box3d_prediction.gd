@tool
class_name EGPNetBox3DPrediction
extends Node
## Inspector-configured local Box3D prediction. Received frames contain canonical
## inputs and hashes, never solver snapshots. Replay simulates the complete world.
## The game supplies input_for_tick(tick) -> PackedByteArray and
## apply_input(world, tick, bytes, replay) -> Error. apply_input queues commands;
## this adapter owns step_tick. External stepping of this world is forbidden.

signal resync_required(error: Error)
signal corrected(first_tick: int, replayed_ticks: int)
signal input_ready(tick: int, input: PackedByteArray)
signal remote_input(peer: int, tick: int, input: PackedByteArray)

@export_group("Bindings")
@export var world_provider_path: NodePath = NodePath(".."):
	set(value):
		world_provider_path = value
		if is_inside_tree(): update_configuration_warnings()
@export var world_property: StringName = &"world":
	set(value):
		world_property = value
		if is_inside_tree(): update_configuration_warnings()
@export var session_path: NodePath:
	set(value):
		session_path = value
		if is_inside_tree(): update_configuration_warnings()
@export var body_id: int = 1:
	set(value):
		body_id = value
		if is_inside_tree(): update_configuration_warnings()
@export var presentation_path: NodePath:
	set(value):
		presentation_path = value
		if is_inside_tree(): update_configuration_warnings()
@export var hook_node_path: NodePath = NodePath(".."):
	set(value):
		hook_node_path = value
		if is_inside_tree(): update_configuration_warnings()
@export var input_method: StringName = &"input_for_tick":
	set(value):
		input_method = value
		if is_inside_tree(): update_configuration_warnings()
@export var apply_method: StringName = &"apply_input":
	set(value):
		apply_method = value
		if is_inside_tree(): update_configuration_warnings()
@export_group("Input contract")
@export var input_schema: StringName = &"axes2-buttons16-v1":
	set(value):
		input_schema = value
		if is_inside_tree(): update_configuration_warnings()
@export_range(1, 1024, 1) var input_bytes: int = 6:
	set(value):
		input_bytes = value
		if is_inside_tree(): update_configuration_warnings()
## Required for custom binary schemas. validate_input(bytes) -> bool.
@export var validate_method: StringName:
	set(value):
		validate_method = value
		if is_inside_tree(): update_configuration_warnings()
@export_group("Prediction bounds")
@export var auto_predict := true:
	set(value):
		auto_predict = value
		if is_inside_tree(): update_configuration_warnings()
@export var send_inputs := true:
	set(value):
		send_inputs = value
		if is_inside_tree(): update_configuration_warnings()
@export_range(1, 512, 1) var history_ticks := 128:
	set(value):
		history_ticks = value
		if is_inside_tree(): update_configuration_warnings()
@export_range(65536, 67108864, 65536) var history_bytes := 33554432:
	set(value):
		history_bytes = value
		if is_inside_tree(): update_configuration_warnings()
@export_range(0.0, 30.0, 0.5) var presentation_speed := 12.0:
	set(value):
		presentation_speed = value
		if is_inside_tree(): update_configuration_warnings()

var _world: RefCounted
var _session: Object
var _network_provider: Node
var _bound_session: Object
var _tick_credit := 0.0
var _connected_once := false
var _input: Callable
var _apply: Callable
var _validate: Callable
var _history: RefCounted
var _pending: Dictionary = {}
var _epoch := 0
var _failed := false
var _busy := false
var _present_offset := Vector3.ZERO
var _rotation_offset := Quaternion.IDENTITY
var _last_error := ""

func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if world_provider_path.is_empty(): warnings.append("Choose the world provider node. It exposes a local EGPBox3DWorld in World Property. Session Path can target EGPNet or discover an ancestor SuperpositionWorld automatically.")
	var provider := get_node_or_null(world_provider_path) if not world_provider_path.is_empty() else null
	if provider == null: warnings.append("World Provider Path does not resolve to a node.")
	elif not _property_exists(provider, world_property): warnings.append("World Property is missing from the selected provider: %s." % world_property)
	var session := _find_network_provider()
	if session == null: warnings.append("Session Path must resolve to EGPNet or an ancestor SuperpositionWorld.")
	var hooks := get_node_or_null(hook_node_path) if not hook_node_path.is_empty() else null
	if hooks != null:
		if not hooks.has_method(input_method): warnings.append("Hook node has no input method: %s." % input_method)
		if not hooks.has_method(apply_method): warnings.append("Hook node has no deterministic apply method: %s." % apply_method)
		if not validate_method.is_empty() and not hooks.has_method(validate_method): warnings.append("Hook node has no custom validation method: %s." % validate_method)
	if hooks == null: warnings.append("Choose the hook node. Input Method returns typed bytes; Apply Method queues deterministic commands for the complete local world and returns Error. The adapter owns world stepping.")
	if presentation_path.is_empty(): warnings.append("Choose a presentation Node3D separate from authoritative physics state.")
	elif not get_node_or_null(presentation_path) is Node3D: warnings.append("Presentation Path must resolve to a Node3D.")
	if history_bytes < 1048576: warnings.append("History Bytes must be at least 1048576 to capture one bounded native world snapshot.")
	if input_schema != &"axes2-buttons16-v1" and validate_method.is_empty(): warnings.append("Custom fixed-byte input schemas require a validation callback; arbitrary network bytes cannot be simulated.")
	if input_schema == &"axes2-buttons16-v1" and input_bytes != 6: warnings.append("Axes2 Buttons16 schema is exactly 6 bytes: two normalized signed16 axes and an unsigned16 buttons bitmask.")
	return warnings

static func encode_player_input(x: float, z: float, buttons: int) -> PackedByteArray:
	if not is_finite(x) or not is_finite(z) or buttons < 0 or buttons > 65535: return PackedByteArray()
	var bytes := PackedByteArray()
	bytes.resize(6)
	bytes.encode_s16(0, roundi(clampf(x, -1, 1) * 32767))
	bytes.encode_s16(2, roundi(clampf(z, -1, 1) * 32767))
	bytes.encode_u16(4, buttons)
	return bytes

func start_prediction(world: RefCounted, session: Node, capture_input: Callable, apply_input: Callable, validate_input: Callable = Callable()) -> Error:
	if Engine.is_editor_hint(): return ERR_UNAVAILABLE
	if _busy: return ERR_BUSY
	if _history != null: return ERR_ALREADY_IN_USE
	if world == null or not world.is_class("EGPBox3DWorld") or session == null or not session.has_method("get_simulation_fingerprint") or not capture_input.is_valid() or not apply_input.is_valid(): return ERR_INVALID_PARAMETER
	if session.get_simulation_fingerprint() != world.get_simulation_fingerprint() or not session.has_method("get_tick_rate") or not world.get_simulation_fingerprint().contains(":hz%d:" % session.get_tick_rate()): return ERR_INVALID_PARAMETER
	if body_id <= 0 or world.get_body_state(body_id).is_empty(): return ERR_INVALID_PARAMETER
	if input_bytes < 1 or input_bytes > 1024 or history_ticks < 1 or history_ticks > 512 or history_bytes < 1048576 or history_bytes > 67108864: return ERR_INVALID_PARAMETER
	if input_schema != &"axes2-buttons16-v1" and not validate_input.is_valid(): return ERR_INVALID_PARAMETER
	if input_schema == &"axes2-buttons16-v1" and input_bytes != 6: return ERR_INVALID_PARAMETER
	if str(input_schema).to_utf8_buffer().is_empty() or str(input_schema).to_utf8_buffer().size() > 64: return ERR_INVALID_PARAMETER
	_world = world
	_network_provider = session
	_session = session if session.has_method("send_message") else (session.get_session() if session.has_method("get_session") else session)
	_bound_session = _session_identity(session)
	if _session == null: return ERR_UNCONFIGURED
	_tick_credit = 0
	_connected_once = false
	_input = capture_input
	_apply = apply_input
	_validate = validate_input
	_failed = false
	_pending.clear()
	if not ClassDB.class_exists("SuperpositionPrediction"): return ERR_UNAVAILABLE
	_history = ClassDB.instantiate("SuperpositionPrediction")
	var result: Error = _history.configure(world.capture_snapshot, world.restore_snapshot, _simulate, world.get_state_hash, world.get_tick(), history_ticks, 1048576, history_bytes, _epoch)
	if result != OK: _history = null; return result
	_history.resync_required.connect(_fail)
	_history.corrected.connect(func(first: int, count: int): corrected.emit(first, count))
	if session.has_signal("message_received") and not session.message_received.is_connected(_receive): session.message_received.connect(_receive)
	if session.has_signal("application_received") and not session.application_received.is_connected(_receive_application): session.application_received.connect(_receive_application)
	return OK

func _find_network_provider() -> Node:
	var session := get_node_or_null(session_path) if not session_path.is_empty() else get_parent()
	while session != null and not session.has_method("get_session") and not session.has_method("get_simulation_fingerprint"): session = session.get_parent()
	return session

func _property_exists(node: Node, name: StringName) -> bool:
	for property in node.get_property_list():
		if property.name == name: return true
	return false

func _bind_inspector() -> Error:
	var provider := get_node_or_null(world_provider_path)
	var session := _find_network_provider()
	var hooks := get_node_or_null(hook_node_path)
	if provider == null or session == null or hooks == null: return ERR_UNCONFIGURED
	if not _property_exists(provider, world_property) or not hooks.has_method(input_method) or not hooks.has_method(apply_method): return ERR_INVALID_PARAMETER
	var world: Variant = provider.get(world_property)
	if world == null: return ERR_UNCONFIGURED
	if not world is RefCounted: return ERR_INVALID_PARAMETER
	return start_prediction(world, session, Callable(hooks, input_method), Callable(hooks, apply_method), Callable(hooks, validate_method) if not validate_method.is_empty() else Callable())

func _valid_input(input: PackedByteArray) -> bool:
	if input.size() != input_bytes: return false
	if input_schema == &"axes2-buttons16-v1": return input.decode_s16(0) != -32768 and input.decode_s16(2) != -32768
	var value: Variant = _validate.call(input.duplicate())
	return value is bool and value

func _simulate(tick: int, input: PackedByteArray, replay: bool) -> Error:
	if not _valid_input(input): return ERR_INVALID_DATA
	var result: Variant = _apply.call(_world, tick, input.duplicate(), replay)
	if not result is int or result != OK: return result if result is int else ERR_INVALID_DATA
	return _world.step_tick(tick)

func predict_next() -> Error:
	if _busy: return ERR_BUSY
	_busy = true
	var result := _predict_next_unlocked()
	_busy = false
	return result

func _predict_next_unlocked() -> Error:
	if _history == null or _failed: return ERR_UNCONFIGURED
	var tick: int = _history.get_tick() + 1
	var input: Variant = _input.call(tick)
	if not input is PackedByteArray or not _valid_input(input): return ERR_INVALID_DATA
	var result: Error = _history.predict(tick, input)
	if result == OK:
		input_ready.emit(tick, input.duplicate())
		if send_inputs:
			result = _send_frame(0, 1, {"tick":tick,"input":input,"hash":"0000000000000000"})
	return result

## Bounded reordering queue. A complete contiguous batch is verified locally;
## duplicates are idempotent, conflicting duplicate inputs/hashes fail closed.
func accept_authority(frames: Array, epoch: int = 0) -> Error:
	if _busy: return ERR_BUSY
	_busy = true
	var result := _accept_authority_unlocked(frames, epoch)
	_busy = false
	return result

func _accept_authority_unlocked(frames: Array, epoch: int) -> Error:
	if _history == null or _failed: return ERR_UNCONFIGURED
	if epoch != _epoch or frames.is_empty() or frames.size() > history_ticks: return ERR_INVALID_DATA
	var validated: Dictionary = {}
	var ack: int = _history.get_acknowledged_tick()
	for frame in frames:
		if not frame is Dictionary or not frame.get("tick") is int or not frame.get("input") is PackedByteArray or not frame.get("hash") is String: return ERR_INVALID_DATA
		if not _valid_input(frame.input) or frame.hash.length() != 16 or not frame.hash.is_valid_hex_number() or frame.tick < 1 or frame.tick > 9007199254740991: return ERR_INVALID_DATA
		if frame.tick <= ack: continue
		if frame.tick > ack + history_ticks: return ERR_BUSY
		var previous: Dictionary = validated.get(frame.tick, _pending.get(frame.tick, {}))
		if not previous.is_empty() and (previous.input != frame.input or previous.hash != frame.hash): return _fail(ERR_INVALID_DATA)
		validated[frame.tick] = {"tick": frame.tick, "input": frame.input.duplicate(), "hash": frame.hash}
	var total := _pending.size()
	for tick in validated:
		if not _pending.has(tick): total += 1
	if total > history_ticks: return ERR_BUSY
	_pending.merge(validated, true)
	var contiguous: Array = []
	var next := ack + 1
	while _pending.has(next): contiguous.append(_pending[next]); next += 1
	if contiguous.is_empty(): return OK
	var before := _body_pose()
	var result: Error = _history.accept(contiguous, _epoch)
	if result != OK: return result
	for frame in contiguous: _pending.erase(frame.tick)
	var after := _body_pose()
	_present_offset += before.origin - after.origin
	_rotation_offset = (_rotation_offset * before.basis.get_rotation_quaternion() * after.basis.get_rotation_quaternion().inverse()).normalized()
	return OK

## Explicit local reset only: the caller first rebuilds a trusted synchronized
## world. Network payloads cannot invoke reset or restore remote solver bytes.
func reset_local(epoch: int) -> Error:
	if _busy: return ERR_BUSY
	if epoch <= _epoch or epoch > 9007199254740991 or _world == null: return ERR_INVALID_PARAMETER
	var world := _world
	var session := _network_provider
	var capture := _input
	var apply := _apply
	var validate := _validate
	stop_prediction()
	_epoch = epoch
	return start_prediction(world, session, capture, apply, validate)

func stop_prediction() -> Error:
	if _busy: return ERR_BUSY
	if is_instance_valid(_network_provider):
		if _network_provider.has_signal("message_received") and _network_provider.message_received.is_connected(_receive): _network_provider.message_received.disconnect(_receive)
		if _network_provider.has_signal("application_received") and _network_provider.application_received.is_connected(_receive_application): _network_provider.application_received.disconnect(_receive_application)
	_history = null
	_pending.clear()
	_present_offset = Vector3.ZERO
	_rotation_offset = Quaternion.IDENTITY
	_world = null
	_session = null
	_network_provider = null
	_bound_session = null
	return OK

func _fail(error: Error) -> Error:
	_failed = true
	_pending.clear()
	_last_error = error_string(error)
	resync_required.emit(error)
	return error

func _receive(peer: int, name: StringName, arguments: Array) -> void:
	if _history == null or _failed: return
	if name == &"egp_prediction_authority" and peer == 0 and not _is_server() and arguments.size() == 3 and arguments[0] == input_schema and arguments[1] is int and arguments[2] is Array:
		accept_authority(arguments[2], arguments[1])
	elif name == &"egp_prediction_input" and _is_server() and peer != 0 and arguments.size() == 4 and arguments[0] == input_schema and arguments[1] == _epoch and arguments[2] is int and arguments[2] > 0 and arguments[2] <= 9007199254740991 and arguments[3] is PackedByteArray and _valid_input(arguments[3]):
		remote_input.emit(peer, arguments[2], arguments[3].duplicate())

func publish_authority(frame: Dictionary, peers: PackedInt64Array) -> Error:
	if _session == null or not _is_server(): return ERR_UNAUTHORIZED
	# Canonical full-world input is authored by the game/server tick loop.
	if not frame.get("tick") is int or frame.tick < 1 or frame.tick > 9007199254740991 or not frame.get("input") is PackedByteArray or not _valid_input(frame.input) or not frame.get("hash") is String or frame.hash.length() != 16 or not frame.hash.is_valid_hex_number(): return ERR_INVALID_DATA
	for peer in peers:
		var result: Error = _send_frame(peer, 2, frame)
		if result != OK: return result
	return OK

func _session_identity(provider: Node) -> Object:
	if provider.has_method("get_session"): return provider.get_session()
	for property in provider.get_property_list():
		if property.name == "session": return provider.get("session")
	return provider

func _is_server() -> bool:
	return _session != null and _session.get_state() == "Listening"

func _send_frame(peer: int, kind: int, frame: Dictionary) -> Error:
	if _session.has_method("send_message"):
		return _session.send_message(peer, &"egp_prediction_input" if kind == 1 else &"egp_prediction_authority", [input_schema, _epoch, frame.tick, frame.input] if kind == 1 else [input_schema, _epoch, [frame]])
	var schema := str(input_schema).to_utf8_buffer()
	var packet := PackedByteArray()
	packet.resize(40 + schema.size() + frame.input.size())
	packet.encode_u32(0, 0x31505053) # SPP1 namespace, not a remote solver snapshot.
	packet[4] = kind
	packet[5] = schema.size()
	packet.encode_u16(6, frame.input.size())
	packet.encode_u64(8, _epoch)
	packet.encode_u64(16, frame.tick)
	var digest: PackedByteArray = frame.hash.to_ascii_buffer()
	for i in 16: packet[24 + i] = digest[i]
	for i in schema.size(): packet[40 + i] = schema[i]
	for i in frame.input.size(): packet[40 + schema.size() + i] = frame.input[i]
	return _session.send_application(peer, packet)

func _receive_application(peer: int, payload: PackedByteArray) -> void:
	if _history == null or _failed or payload.size() < 4 or payload.decode_u32(0) != 0x31505053: return
	if payload.size() < 40: return
	var schema_size := int(payload[5])
	var count := int(payload.decode_u16(6))
	if schema_size < 1 or schema_size > 64 or count != input_bytes or payload.size() != 40 + schema_size + count: return
	if payload.slice(40, 40 + schema_size).get_string_from_utf8() != str(input_schema): return
	var epoch: int = payload.decode_u64(8)
	var tick: int = payload.decode_u64(16)
	if epoch != _epoch or tick < 1 or tick > 9007199254740991: return
	var input := payload.slice(40 + schema_size)
	if not _valid_input(input): return
	if payload[4] == 2 and peer == 0 and not _is_server():
		accept_authority([{"tick":tick,"input":input,"hash":payload.slice(24,40).get_string_from_ascii()}], epoch)
	elif payload[4] == 1 and peer != 0 and _is_server():
		# Ownership/game action permissions are decided by the authoritative hook.
		remote_input.emit(peer, tick, input)

func _body_pose() -> Transform3D:
	var state: Dictionary = _world.get_body_state(body_id)
	return Transform3D(Basis(state.rotation), state.position) if not state.is_empty() else Transform3D.IDENTITY

func _physics_process(delta: float) -> void:
	if Engine.is_editor_hint(): return
	if _history == null and not _failed:
		var binding: Error = _bind_inspector()
		if binding != OK: _last_error = "Prediction bindings: " + error_string(binding)
		else: _last_error = ""
	if _history == null or _failed: return
	var provider := get_node_or_null(world_provider_path) if not world_provider_path.is_empty() else null
	if (provider != null and (not _property_exists(provider, world_property) or provider.get(world_property) != _world)) or not is_instance_valid(_session) or (is_instance_valid(_network_provider) and _session_identity(_network_provider) != _bound_session):
		_fail(ERR_CONNECTION_ERROR)
		return
	var state: String = _session.get_state()
	if state in ["Connected", "Listening"]: _connected_once = true
	elif _connected_once:
		_fail(ERR_CONNECTION_ERROR)
		return
	if auto_predict and _session.get_state() == "Connected" and not _is_server():
		var hz: int = _network_provider.get_tick_rate()
		_tick_credit = minf(_tick_credit + delta * hz, 4.0)
		while _tick_credit >= 1.0:
			var result := predict_next()
			if result == ERR_BUSY: break
			if result != OK: _fail(result); break
			_tick_credit -= 1.0

func _process(delta: float) -> void:
	if Engine.is_editor_hint() or _world == null or _history == null or _failed: return
	var target := get_node_or_null(presentation_path) as Node3D if not presentation_path.is_empty() else null
	if target == null: return
	var decay := exp(-maxf(presentation_speed, 0) * minf(delta, 0.25))
	_present_offset *= decay
	_rotation_offset = _rotation_offset.slerp(Quaternion.IDENTITY, 1.0 - decay)
	var pose := _body_pose()
	pose.origin += _present_offset
	pose.basis = Basis(_rotation_offset) * pose.basis
	target.global_transform = pose

func get_statistics() -> Dictionary:
	return {"ready": _history != null and not _failed, "failed": _failed, "busy": _busy, "last_error": _last_error, "epoch": _epoch, "queued_authority": _pending.size(), "tick": _history.get_tick() if _history != null else 0, "acknowledged_tick": _history.get_acknowledged_tick() if _history != null else 0, "pending_ticks": _history.get_pending_ticks() if _history != null else 0, "history_bytes": _history.get_history_bytes() if _history != null else 0}
