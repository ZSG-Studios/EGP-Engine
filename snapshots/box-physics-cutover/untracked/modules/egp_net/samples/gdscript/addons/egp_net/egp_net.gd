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
	session.state_changed.connect(_on_state)
	session.peer_connected.connect(func(peer: int): peer_connected.emit(peer))
	session.peer_disconnected.connect(func(peer: int): peer_disconnected.emit(peer))
	session.application_received.connect(_on_message)
	session.packet_received.connect(func(peer: int, data: PackedByteArray, channel: int, delivery: int): packet_received.emit(peer, data, channel, delivery))
	session.simulation_tick.connect(func(tick: int, server: bool): simulation_tick.emit(tick, server))
	session.diagnostic.connect(func(text: String): diagnostic.emit(text))
	return OK

func host(port: int = 10515, bind_address: String = "0.0.0.0") -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	_server = true
	return session.listen(port, bind_address)

func join(address: String, port: int = 10515) -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	_server = false
	return session.connect_to_server(address, port)

## Join using a token issued by the trusted server/authentication service.
func join_token(client_id: int, token: PackedByteArray) -> Error:
	if session == null:
		var error := configure()
		if error != OK:
			return error
	_server = false
	return session.connect_token(client_id, token)

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
	if session != null:
		session.stop()
	_clear_entities()

func close() -> void:
	if session != null:
		session.close()
		session = null
	_clear_entities()

func is_server() -> bool:
	return _server and session != null and session.get_state() == "Listening"

func get_state() -> String:
	return session.get_state() if session != null else "Unconfigured"

func get_statistics() -> Dictionary:
	return session.get_statistics() if session != null else {}

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
	_sync_entities()
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
	_sync_entities()
	return error

func despawn(entity: int) -> Error:
	if not is_server():
		return ERR_UNAUTHORIZED
	var error: Error = session.command("despawn", {"entity": entity})
	_sync_entities()
	return error

func get_entities() -> Array:
	return _entities.keys()

func get_entity(entity: int) -> Dictionary:
	return _entities.get(entity, {}).duplicate(true)

## Instantiate a registered scene for replicated entities of this kind.
func register_scene(kind: int, scene: PackedScene, parent: Node) -> Error:
	if scene == null or not is_instance_valid(parent) or _scenes.has(kind):
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
	if data.size() > MAX_MESSAGE_BYTES:
		return
	var envelope: Variant = bytes_to_var(data)
	if not envelope is Array or envelope.size() != 2 or not envelope[0] is String or not envelope[1] is Array or not _valid_value(envelope):
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

func _on_state(state: String) -> void:
	if state == "Stopped" or state == "Disconnected":
		_clear_entities()
	state_changed.emit(state)

func _sync_entities() -> void:
	if session == null:
		return
	var seen: Dictionary = {}
	for record in session.command("entities"):
		var entity: int = record.entity
		seen[entity] = true
		var previous: Dictionary = _entities.get(entity, {})
		if not previous.is_empty() and previous.revision == record.revision:
			continue
		var state: Variant = bytes_to_var(record.state) if not record.state.is_empty() else {}
		if not state is Dictionary or not _valid_value(state):
			continue
		record.state = state
		_entities[entity] = record
		if previous.is_empty():
			_create_node(record)
			entity_spawned.emit(entity, record.kind, state.duplicate(true))
		else:
			entity_changed.emit(entity, state.duplicate(true))
		_update_node(record)
	for entity in _entities.keys():
		if not seen.has(entity):
			_remove_entity(entity)

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
	return true
