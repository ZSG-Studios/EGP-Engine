class_name EGPNet
extends Node
## AIO native Yojimbo session. Add as an autoload or an ordinary game node.
## Script states are server-authoritative. Simulation ticks use the native fixed clock.

signal state_changed(state: String)
signal peer_connected(peer_id: int)
signal peer_disconnected(peer_id: int)
signal entity_spawned(entity: int, kind: int, state: Dictionary)
signal entity_changed(entity: int, state: Dictionary)
signal entity_despawned(entity: int)
signal message_received(peer_id: int, message: StringName, arguments: Array)
signal input_received(peer_id: int, entity: int, input: Dictionary)
signal packet_received(peer_id: int, payload: PackedByteArray, channel: int, delivery: int)
signal diagnostic(message: String)
signal simulation_tick(tick: int, server: bool)

enum Delivery { RELIABLE_ORDERED = 2, UNRELIABLE = 4 }
enum Sender { SERVER = 1, CLIENT = 2, BOTH = 3 }
const MAX_STATE_BYTES := 4096
const MAX_MESSAGE_BYTES := 4096
@export var auto_poll := true
var session: RefCounted
var _server := false
var _entities: Dictionary = {}
var _handlers: Dictionary = {}
var _scenes: Dictionary = {}
var _nodes: Dictionary = {}
var _tick_rate := 60
var _simulation_fingerprint := "script-state-v1"
var _blocked_peers: Dictionary = {}

func configure(options: Dictionary = {}) -> Error:
	if session != null:
		return ERR_ALREADY_IN_USE
	if not ClassDB.class_exists("EGPNetSession"):
		return ERR_UNAVAILABLE
	session = ClassDB.instantiate("EGPNetSession")
	var error: Error = session.configure(options)
	if error != OK:
		session = null
		return error
	_tick_rate = options.get("tick_rate", 60)
	_simulation_fingerprint = options.get("simulation_fingerprint", "script-state-v1")
	session.state_changed.connect(_on_state)
	session.peer_connected.connect(_on_peer_connected)
	session.peer_disconnected.connect(_on_peer_disconnected)
	session.application_received.connect(_on_message)
	session.packet_received.connect(_on_packet)
	session.simulation_tick.connect(_on_tick)
	session.diagnostic.connect(_on_diagnostic)
	return OK

func _on_peer_connected(peer: int) -> void:
	peer_connected.emit(peer)

func _on_peer_disconnected(peer: int) -> void:
	_blocked_peers.erase(peer)
	peer_disconnected.emit(peer)

func _on_packet(peer: int, data: PackedByteArray, channel: int, delivery: int) -> void:
	if not _blocked_peers.has(peer):
		packet_received.emit(peer, data, channel, delivery)

func _on_tick(tick: int, server: bool) -> void:
	simulation_tick.emit(tick, server)

func _on_diagnostic(text: String) -> void:
	diagnostic.emit(text)

func _disconnect_session(active: RefCounted) -> void:
	for link in [[&"state_changed", _on_state], [&"peer_connected", _on_peer_connected], [&"peer_disconnected", _on_peer_disconnected], [&"application_received", _on_message], [&"packet_received", _on_packet], [&"simulation_tick", _on_tick], [&"diagnostic", _on_diagnostic]]:
		if active.is_connected(link[0], link[1]):
			active.disconnect(link[0], link[1])

func host(port: int = 10515, bind_address: String = "0.0.0.0") -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	var previous_role := _server
	_server = true
	var error: Error = session.listen(port, bind_address)
	if error != OK:
		_server = previous_role
	return error

func join(address: String, port: int = 10515) -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	var previous_role := _server
	_server = false
	var error: Error = session.connect_to_server(address, port)
	if error != OK:
		_server = previous_role
	return error

## Join using a token issued by the trusted server/authentication service.
func join_token(client_id: int, token: PackedByteArray, bind_address: String = "0.0.0.0") -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	var previous_role := _server
	_server = false
	var error: Error = session.connect_token(client_id, token, bind_address)
	if error != OK:
		_server = previous_role
	return error

func issue_token(client_id: int, public_address: String) -> Dictionary:
	return session.issue_token(client_id, public_address) if is_server() else {"error": ERR_UNAUTHORIZED}

func set_entity_visible(entity: int, peer_id: int, visible: bool) -> Error:
	return session.command("set_visible", {"entity": entity, "peer": peer_id, "visible": visible}) if is_server() else ERR_UNAUTHORIZED

func poll() -> Error:
	if session == null:
		return ERR_UNCONFIGURED
	var active := session
	var error: Error = active.poll()
	if session == active:
		_sync_entities()
	return error

func _process(_delta: float) -> void:
	if auto_poll and session != null:
		poll()

func _exit_tree() -> void:
	close()

func stop() -> void:
	var active := session
	if active != null:
		active.stop()
	if session == active:
		_clear_entities()

func close() -> void:
	var active := session
	if active != null:
		active.close()
		_disconnect_session(active)
	if session == active:
		session = null
		_clear_entities()

func is_server() -> bool:
	return _server and session != null and session.get_state() == "Listening"

func get_state() -> String:
	return session.get_state() if session != null else "Unconfigured"

func get_statistics() -> Dictionary:
	return session.get_statistics() if session != null else {}

func get_tick_rate() -> int:
	return _tick_rate

func get_simulation_fingerprint() -> String:
	return _simulation_fingerprint

func get_peers() -> Array:
	return session.command("peers") if session != null else []

func disconnect_peer(peer_id: int) -> Error:
	return session.command("disconnect", {"peer": peer_id}) if session != null else ERR_UNCONFIGURED

func spawn(kind: int, state: Dictionary = {}, authority_peer: int = -1) -> int:
	if not is_server() or not _valid_value(state):
		return 0
	var data := var_to_bytes(state)
	if data.size() > MAX_STATE_BYTES:
		return 0
	if authority_peer != -1 and not _has_peer(authority_peer):
		return 0
	var result: Variant = session.command("spawn", {"kind": kind, "state": data, "authority_peer": authority_peer})
	if not result is Dictionary or result.get("error", FAILED) != OK:
		return 0
	_sync_entity(result.entity)
	return result.entity

func update_entity(entity: int, state: Dictionary) -> Error:
	if not is_server():
		return ERR_UNAUTHORIZED
	if not _valid_value(state):
		return ERR_INVALID_DATA
	var data := var_to_bytes(state)
	if data.size() > MAX_STATE_BYTES:
		return ERR_OUT_OF_MEMORY
	var error: Error = session.command("update_entity", {"entity": entity, "state": data})
	# Refresh the affected entity rather than copying the whole native world for
	# every body. A frame updating N bodies must not perform N full table copies.
	_sync_entity(entity)
	return error

func despawn(entity: int) -> Error:
	if not is_server():
		return ERR_UNAUTHORIZED
	var error: Error = session.command("despawn", {"entity": entity})
	_sync_entity(entity)
	return error

func get_entities() -> Array:
	return _entities.keys()

func get_entity(entity: int) -> Dictionary:
	return _entities.get(entity, {}).duplicate(true)

## Instantiate a registered scene for replicated entities of this kind.
func register_scene(kind: int, scene: PackedScene, parent: Node) -> Error:
	if scene == null or not scene.can_instantiate() or not is_instance_valid(parent) or _scenes.has(kind):
		return ERR_INVALID_PARAMETER
	_scenes[kind] = {"scene": scene, "parent": weakref(parent)}
	for record in _entities.values():
		if record.kind == kind:
			_create_node(record)
	return OK

## Only explicitly registered handlers are callable; names never invoke arbitrary Node methods.
func register_message(message: StringName, handler: Callable, allowed_sender: Sender = Sender.BOTH) -> Error:
	if not handler.is_valid() or message.is_empty() or str(message).length() > 64 or str(message).begins_with("$") or allowed_sender < 1 or allowed_sender > 3:
		return ERR_INVALID_PARAMETER
	if _handlers.has(message):
		return ERR_ALREADY_EXISTS
	_handlers[message] = {"handler": handler, "sender": allowed_sender}
	return OK

func unregister_message(message: StringName) -> void:
	_handlers.erase(message)

func send_message(peer_id: int, message: StringName, arguments: Array = []) -> Error:
	if message.is_empty() or str(message).length() > 64 or str(message).begins_with("$") or not _valid_value(arguments):
		return ERR_INVALID_PARAMETER
	return _send_envelope(peer_id, message, arguments)

func broadcast_message(message: StringName, arguments: Array = []) -> Error:
	for peer in get_peers():
		var error := send_message(peer.peer_id, message, arguments)
		if error != OK:
			return error
	return OK

## Server emits input_received only when this transport peer owns the entity.
## Gameplay must validate values before changing authority or physics.
func send_input(entity: int, input: Dictionary) -> Error:
	if _server or not _valid_value(input):
		return ERR_UNAUTHORIZED
	var peers := get_peers()
	if peers.size() != 1:
		return ERR_UNCONFIGURED
	return _send_envelope(peers[0].peer_id, &"$input", [entity, input])

func send_packet(peer_id: int, payload: PackedByteArray, channel: int = 0, delivery: Delivery = Delivery.RELIABLE_ORDERED) -> Error:
	if session == null:
		return ERR_UNCONFIGURED
	return session.command("send_packet", {"peer": peer_id, "payload": payload, "channel": channel, "delivery": delivery})

func broadcast_packet(payload: PackedByteArray, channel: int = 0, delivery: Delivery = Delivery.RELIABLE_ORDERED) -> Error:
	for peer in get_peers():
		var error := send_packet(peer.peer_id, payload, channel, delivery)
		if error != OK:
			return error
	return OK

func _send_envelope(peer: int, message: StringName, arguments: Array) -> Error:
	if session == null:
		return ERR_UNCONFIGURED
	var data := var_to_bytes([str(message), arguments])
	if data.size() > MAX_MESSAGE_BYTES:
		return ERR_OUT_OF_MEMORY
	return session.send_application(peer, data)

func _on_message(peer: int, data: PackedByteArray) -> void:
	if _blocked_peers.has(peer):
		return
	# Preflight the fixed envelope before decoding; malformed clients are quarantined.
	if data.size() < 24 or data.size() > MAX_MESSAGE_BYTES or data.decode_u32(0) != TYPE_ARRAY or data.decode_u32(4) != 2 or data.decode_u32(8) != TYPE_STRING:
		_bad_envelope(peer)
		return
	var name_bytes := data.decode_u32(12)
	var arguments_offset := 16 + ((name_bytes + 3) & ~3)
	if name_bytes < 1 or name_bytes > 256 or arguments_offset + 8 > data.size() or (data.decode_u32(arguments_offset) & 0xffff) != TYPE_ARRAY:
		_bad_envelope(peer)
		return
	var envelope: Variant = bytes_to_var(data)
	if not envelope is Array or envelope.size() != 2 or not envelope[0] is String or not envelope[1] is Array or not _valid_value(envelope):
		_bad_envelope(peer)
		return
	if envelope[0].is_empty() or envelope[0].length() > 64:
		_bad_envelope(peer)
		return
	var message := StringName(envelope[0])
	var args: Array = envelope[1]
	if message == &"$input":
		if not is_server() or args.size() != 2 or not args[0] is int or not args[1] is Dictionary:
			return
		if _entities.get(args[0], {}).get("authority_peer", -1) != peer:
			return
		input_received.emit(peer, args[0], args[1])
		return
	if not _handlers.has(message):
		return
	var handler: Dictionary = _handlers[message]
	var sender := Sender.CLIENT if _server else Sender.SERVER
	if (handler.sender & sender) == 0 or not handler.handler.is_valid():
		return
	message_received.emit(peer, message, args)
	handler.handler.call(peer, args)

func _bad_envelope(peer: int) -> void:
	_blocked_peers[peer] = true
	diagnostic.emit("Malformed application envelope; disconnecting peer %d." % peer)
	if session != null:
		disconnect_peer(peer)

func _on_state(state: String) -> void:
	if state == "Stopped" or state == "Disconnected":
		_blocked_peers.clear()
		_clear_entities()
	state_changed.emit(state)

func _sync_entities() -> void:
	if session == null:
		return
	var seen: Dictionary = {}
	for record in session.command("entities"):
		var entity: int = record.entity
		seen[entity] = true
		_apply_entity_record(record)
	for entity in _entities.keys():
		if not seen.has(entity):
			_remove_entity(entity)

func _sync_entity(entity: int) -> void:
	if session == null: return
	var record: Variant = session.command("entity", {"entity": entity})
	# Older compatible engines lack the optional single-entity command.
	if not record is Dictionary:
		_sync_entities()
		return
	if record.is_empty():
		if _entities.has(entity): _remove_entity(entity)
		return
	_apply_entity_record(record)

func _apply_entity_record(record: Dictionary) -> void:
	var entity: int = record.entity
	var previous: Dictionary = _entities.get(entity, {})
	if not previous.is_empty() and previous.revision == record.revision: return
	var state: Variant = bytes_to_var(record.state) if not record.state.is_empty() else {}
	if not state is Dictionary or not _valid_value(state): return
	record.state = state
	_entities[entity] = record
	if previous.is_empty():
		_create_node(record)
		entity_spawned.emit(entity, record.kind, state.duplicate(true))
	else:
		entity_changed.emit(entity, state.duplicate(true))
	_update_node(record)

func _has_peer(peer_id: int) -> bool:
	for peer in get_peers():
		if peer.peer_id == peer_id:
			return true
	return false

func _create_node(record: Dictionary) -> void:
	if not _scenes.has(record.kind) or _nodes.has(record.entity):
		return
	var registration: Dictionary = _scenes[record.kind]
	var parent: Node = registration.parent.get_ref()
	if not is_instance_valid(parent):
		return
	var node: Node = registration.scene.instantiate()
	_nodes[record.entity] = weakref(node)
	node.set_meta("egp_entity", record.entity)
	parent.add_child(node)
	_update_node(record)

func _update_node(record: Dictionary) -> void:
	if _nodes.has(record.entity):
		var node: Node = _nodes[record.entity].get_ref()
		if is_instance_valid(node) and node.has_method("apply_network_state"):
			node.apply_network_state(record.state.duplicate(true))

func _remove_entity(entity: int) -> void:
	_entities.erase(entity)
	if _nodes.has(entity):
		var node: Node = _nodes[entity].get_ref()
		if is_instance_valid(node):
			node.queue_free()
		_nodes.erase(entity)
	entity_despawned.emit(entity)

func _clear_entities() -> void:
	for entity in _entities.keys():
		_remove_entity(entity)

static func _valid_value(value: Variant, depth: int = 0) -> bool:
	if depth > 16:
		return false
	match typeof(value):
		TYPE_OBJECT, TYPE_RID, TYPE_CALLABLE, TYPE_SIGNAL:
			return false
		TYPE_ARRAY:
			if value.size() > 256:
				return false
			for item in value:
				if not _valid_value(item, depth + 1):
					return false
		TYPE_DICTIONARY:
			if value.size() > 256:
				return false
			for key in value:
				if not (key is String or key is StringName or key is int) or not _valid_value(value[key], depth + 1):
					return false
		TYPE_FLOAT:
			return is_finite(value)
		TYPE_VECTOR2, TYPE_VECTOR3, TYPE_VECTOR4, TYPE_QUATERNION, TYPE_BASIS, TYPE_TRANSFORM2D, TYPE_TRANSFORM3D, TYPE_RECT2, TYPE_AABB, TYPE_PLANE:
			return value.is_finite()
		TYPE_COLOR:
			return is_finite(value.r) and is_finite(value.g) and is_finite(value.b) and is_finite(value.a)
		TYPE_PROJECTION:
			return value.x.is_finite() and value.y.is_finite() and value.z.is_finite() and value.w.is_finite()
		TYPE_PACKED_BYTE_ARRAY:
			return value.size() <= MAX_STATE_BYTES
		TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY:
			if value.size() > 256:
				return false
			for item in value:
				if not is_finite(item):
					return false
		TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_VECTOR3_ARRAY, TYPE_PACKED_VECTOR4_ARRAY:
			if value.size() > 256:
				return false
			for item in value:
				if not item.is_finite():
					return false
	return true
