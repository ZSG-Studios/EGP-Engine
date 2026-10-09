extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
const Actor = preload("res://addons/egp_net/egp_net_entity_3d.gd")
var server: Node
var client: Node
var stranger: Node
var _started := Time.get_ticks_msec()
var _phase := 0
var _entity := 0
var _first_handle := 0
var _late_entity := 0
var _messages := 0
var _client_peer := -1
var _sent_hello := false
var _input := 0
var _raw: Dictionary = {}
var _spawned_nodes := 0
var _finished := false
var _checks: Array[String] = []

func require(condition: bool, text: String) -> bool:
	if not condition:
		finish(false, text)
		return false
	_checks.append(text)
	return true

func _ready() -> void:
	for legacy in ["ENetConnection", "ENetPacketPeer", "ENetMultiplayerPeer", "SceneMultiplayer", "MultiplayerSpawner", "MultiplayerSynchronizer", "WebRTCPeerConnection", "WebRTCMultiplayerPeer", "WebSocketMultiplayerPeer", "MultiplayerAPIExtension", "MultiplayerPeerExtension"]:
		if not require(not ClassDB.class_exists(legacy), "legacy class absent: " + legacy):
			return
	if not require(not ClassDB.class_has_method("Node", "rpc") and not ClassDB.class_has_method("Node", "get_multiplayer"), "legacy Node RPC API removed"):
		return
	server = Net.new()
	client = Net.new()
	stranger = Net.new()
	for net in [server, client, stranger]:
		net.auto_poll = false
		add_child(net)
		if not require(net.configure({"game_protocol": "aio-smoke-v1", "max_entities": 2, "allow_insecure_loopback": true}) == OK, "engine bootstrap without C# game code"):
			return
	server.register_message(&"hello", func(peer: int, args: Array):
		_client_peer = peer
		server.send_message(peer, &"reply", args), Net.Sender.CLIENT)
	client.register_message(&"reply", func(_peer: int, args: Array):
		if args == ["hi", 42]:
			_messages += 1, Net.Sender.SERVER)
	server.input_received.connect(func(_peer: int, entity: int, input: Dictionary):
		if entity == _entity and input == {"move": Vector2(1, 0)}:
			_input += 1)
	server.packet_received.connect(func(peer: int, bytes: PackedByteArray, channel: int, delivery: int): server.send_packet(peer, bytes, channel, delivery))
	client.packet_received.connect(func(_peer: int, bytes: PackedByteArray, channel: int, delivery: int):
		if bytes == PackedByteArray([channel, delivery]):
			_raw[channel * 5 + delivery] = true)
	var actor := Actor.new()
	var scene := PackedScene.new()
	scene.pack(actor)
	actor.free()
	client.register_scene(7, scene, self)
	if not require(server.host(0, "127.0.0.1") == OK, "high-level host starts"):
		return
	var port: int = server.get_statistics().local_port
	client.join("127.0.0.1", port)
	stranger.join("127.0.0.1", port)

func _process(_delta: float) -> void:
	if _finished:
		return
	if Time.get_ticks_msec() - _started > 20000:
		finish(false, "watchdog phase " + str(_phase))
		return
	for net in [server, client, stranger]:
		if net != null:
			if not require_poll(net):
				return
	match _phase:
		0:
			if client.get_state() != "Connected" or stranger.get_state() != "Connected":
				return
			if not _sent_hello:
				client.send_message(client.get_peers()[0].peer_id, &"hello", ["hi", 42])
				_sent_hello = true
			if _client_peer < 0 or _messages != 1:
				return
			var owner: int = _client_peer
			_entity = server.spawn(7, {"health": 100, "transform": Transform3D.IDENTITY}, owner)
			_first_handle = _entity
			if not require(_entity > 0, "authoritative scene entity spawned"):
				return
			_phase = 1
		1:
			if client.get_entities().size() != 1 or _messages != 1:
				return
			client.send_input(_entity, {"move": Vector2(1, 0)})
			stranger.send_input(_entity, {"move": Vector2(1, 0)})
			for channel in range(4):
				for delivery in [2, 4]:
					client.send_packet(client.get_peers()[0].peer_id, PackedByteArray([channel, delivery]), channel, delivery)
			if not require(client.update_entity(_entity, {"health": 0}) == ERR_UNAUTHORIZED, "client state mutation rejected"):
				return
			if not require(server.update_entity(_entity, {"health": 75, "transform": Transform3D.IDENTITY.translated(Vector3(1, 2, 3))}) == OK, "server state update accepted"):
				return
			_late_entity = server.spawn(8, {"late": true})
			if not require(server.spawn(9, {}) == 0, "entity admission budget enforced"):
				return
			_phase = 2
		2:
			if client.get_entities().size() != 2 or client.get_entity(_entity).state.health != 75 or _input < 1 or _raw.size() != 8:
				return
			if not require(_input == 1, "only owning transport peer input accepted"):
				return
			var nodes := get_children().filter(func(node: Node): return node.has_meta("egp_entity"))
			if not require(nodes.size() == 1 and nodes[0].network_state.health == 75, "scene factory applied replicated dictionary state"):
				return
			if not require(client.send_packet(client.get_peers()[0].peer_id, PackedByteArray(), 4) != OK, "raw reserved channel rejected"):
				return
			server.despawn(_entity)
			server.despawn(_late_entity)
			_phase = 3
		3:
			if not client.get_entities().is_empty():
				return
			client.stop()
			_phase = 4
		4:
			if server.get_peers().size() != 1:
				return
			server.spawn(7, {"health": 33})
			client.join("127.0.0.1", server.get_statistics().local_port)
			_phase = 5
		5:
			if client.get_state() != "Connected" or client.get_entities().size() != 1:
				return
			if not require(client.get_entities()[0] != _first_handle and client.get_entity(client.get_entities()[0]).state.health == 33, "reconnect receives current baseline and versioned reference"):
				return
			finish(true, "GDScript high-level and low-level AIO passed")

func require_poll(net: Node) -> bool:
	if net.poll() != OK:
		finish(false, "poll failed")
		return false
	return true

func finish(passed: bool, message: String) -> void:
	if _finished:
		return
	_finished = true
	for net in [server, client, stranger]:
		if net != null:
			net.close()
	var report := {"passed": passed, "message": message, "checks": _checks, "raw_cases": _raw.size()}
	print("EGP_GDSCRIPT_AIO " + JSON.stringify(report))
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--report="):
			var file := FileAccess.open(argument.substr(9), FileAccess.WRITE)
			file.store_string(JSON.stringify(report))
	get_tree().quit(0 if passed else 1)
