extends SceneTree

const LegacyNet = preload("res://addons/egp_net/egp_net.gd")

var options: Dictionary = {}
var session := EGPNetSession.new()
var legacy := LegacyNet.new()
var sdk_reply := false
var sdk_received := 0
var compiled_assets := false
var world := Node.new()
var spawner := SuperpositionSpawner.new()
var rpc := SuperpositionRPC.new()
var role := "server"
var directory := ""
var phase := 0
var checks := 0
var entity: int = 0
var previous_entity: int = 0
var original_peer: int = -1
var first_seen := false
var hidden_seen := false
var owner_sent := false
var scene_schema_repaired := false
var started: int = Time.get_ticks_msec()
var completed := false

func _initialize() -> void:
	for option in OS.get_cmdline_user_args():
		var pair := option.split("=", true, 1)
		if pair.size() == 2:
			options[pair[0]] = pair[1]
	role = options.get("role", "server")
	directory = options.get("directory", "")
	world.name = "World"
	root.add_child.call_deferred(world)
	setup.call_deferred()

func check(value: bool, message: String) -> bool:
	checks += 1
	if not value:
		finish(false, message)
	return value

func publish(name: String, value: String = "ok") -> void:
	var file := FileAccess.open(directory.path_join(name), FileAccess.WRITE)
	if file == null:
		finish(false, "Cannot publish fixture coordination " + name)
		return
	file.store_string(value)

func exists(name: String) -> bool:
	return FileAccess.file_exists(directory.path_join(name))

func token(client: int, suffix: String = "") -> void:
	var issued := session.issue_token(10000 + client, "127.0.0.1:%d" % session.get_statistics().local_port)
	if not check(issued.error == OK and issued.token.size() == 2048, "Authenticated token issuance"):
		return
	var file := FileAccess.open(directory.path_join("client%d%s.bin" % [client, suffix]), FileAccess.WRITE)
	file.store_buffer(issued.token)

func setup() -> void:
	if options.get("compiled", "0") == "1":
		var actor_script: Script = load("res://spawner_rpc_actor.gd")
		compiled_assets = actor_script.get_source_code().is_empty() and not FileAccess.file_exists("res://spawner_rpc_actor.gd") and not FileAccess.file_exists("res://spawner_rpc_actor.tscn")
		if not check(compiled_assets, "Client loads actual exported compressed script tokens and binary scene without loose source files"):
			return
	legacy.auto_poll = false
	legacy.name = "LegacyNet"
	world.add_child(legacy)
	if not check(legacy.configure({"game_protocol": "egp-spawner-rpc-v1", "max_players": 3, "max_entities": 64, "timeout_seconds": 5, "token_lifetime_seconds": 120, "simulated_latency_ms": 40.0, "simulated_jitter_ms": 8.0, "simulated_loss": 0.01}) == OK, "Session configuration"):
		return
	session = legacy.session as EGPNetSession
	if role == "server":
		check(legacy.register_message(&"coexist_probe", receive_sdk_probe, LegacyNet.Sender.CLIENT) == OK, "Legacy server handler registration")
	else:
		check(legacy.register_message(&"coexist_ack", receive_sdk_ack, LegacyNet.Sender.SERVER) == OK, "Legacy client handler registration")
	spawner.name = "Spawner"
	spawner.replication_key = "fixture"
	spawner.spawn_path = NodePath("..")
	var scene := SuperpositionScene.new()
	scene.prefab_id = 8 if role == "2" else 7
	scene.scene = load("res://spawner_rpc_actor.tscn")
	var scenes: Array[SuperpositionScene] = [scene]
	spawner.scenes = scenes
	spawner.set_session(session)
	world.add_child(spawner)
	rpc.name = "RPC"
	rpc.spawner_path = NodePath("../Spawner")
	var methods: Array[SuperpositionRPCMethod] = []
	for row in [[1, "owner_action", SuperpositionRPCMethod.OWNER, TYPE_INT], [2, "authority_action", SuperpositionRPCMethod.AUTHORITY, TYPE_STRING], [3, "any_action", SuperpositionRPCMethod.ANY_PEER, TYPE_BOOL]]:
		var rule := SuperpositionRPCMethod.new()
		rule.method_id = row[0]
		rule.method = row[1]
		rule.permission = row[2]
		rule.argument_types = PackedInt32Array([row[3]])
		methods.append(rule)
	rpc.methods = methods
	world.add_child(rpc)
	if role == "server":
		if not check(legacy.host(0, "127.0.0.1") == OK, "Server listener"):
			return
		token(1)

func peer_for(client: int) -> int:
	for peer in session.command(&"peers"):
		if peer.client_id == 10000 + client:
			return peer.peer_id
	return -1

func check_component_binding(node: Node, id: int) -> bool:
	var owner_replication: Superposition = node.get_node("OwnerReplication")
	var any_replication: Superposition = node.get_node("AnyReplication")
	return check(owner_replication.get_session() == session and any_replication.get_session() == session and owner_replication.replication_key == "fixture/%d/./OwnerReplication" % id and any_replication.replication_key == "fixture/%d/./AnyReplication" % id, "Nested replication components bind the same session and distinct entity-relative keys")

func first_entity() -> int:
	for row in session.command(&"entities"):
		if spawner.has_entity(row.entity):
			return row.entity
	return 0

func forged(method_id: int, arguments: Array, digest: String = "") -> void:
	var envelope := ["SPRPC1", "fixture", entity, method_id, arguments, rpc.get_method_fingerprint() if digest.is_empty() else digest]
	var payload := "EGPRPC01".to_ascii_buffer()
	payload.append_array(var_to_bytes(envelope))
	check(session.send_application(0, payload) == OK, "Forged packet delivered to permission validator")

func _process(_delta: float) -> bool:
	if completed:
		return false
	if Time.get_ticks_msec() - started > 45000:
		finish(false, "Fixture watchdog expired in phase %d" % phase)
		return false
	if session.get_state() == "Unconfigured":
		return false
	if legacy.poll() != OK:
		finish(false, "Native poll failed")
		return false
	if role == "server":
		server_step()
	else:
		client_step(int(role))
	return false

func server_step() -> void:
	if phase == 0:
		var peer := peer_for(1)
		if peer == -1 or not spawner.is_peer_compatible(peer):
			return
		original_peer = peer
		entity = spawner.spawn(7, peer, {"label": "initial"})
		if not check(entity > 0 and spawner.has_entity(entity), "Server scene spawned"):
			return
		if not check(spawner.get_owner_client_id(entity) == 10001, "Stable authenticated owner identity"):
			return
		if not check_component_binding(spawner.get_spawned_node(entity), entity):
			return
		phase = 1
	elif phase == 1:
		var actor = spawner.get_spawned_node(entity)
		if actor.owner_value == 11 and actor.any_count == 1:
			token(2)
			phase = 2
	elif phase == 2:
		var actor = spawner.get_spawned_node(entity)
		if not exists("2-latejoin") or not exists("2-properties") or not exists("1-sdk") or not exists("2-sdk") or actor.any_count != 2 or rpc.get_statistics().rejected < 4:
			return
		if not check(actor.owner_value == 11 and actor.authority_text.is_empty(), "Owner and authority forgery rejected"):
			return
		if not check(rpc.send_rpc(entity, 2, ["server-authority"]) == OK, "Authority RPC broadcast"):
			return
		phase = 3
	elif phase == 3:
		if not exists("1-authority") or not exists("2-authority"):
			return
		if not check(spawner.set_visible(entity, peer_for(2), false) == OK, "Interest removal"):
			return
		phase = 4
	elif phase == 4:
		if not exists("2-hidden"):
			return
		if not check(spawner.set_visible(entity, peer_for(2), true) == OK, "Interest re-entry"):
			return
		phase = 5
	elif phase == 5:
		if not exists("2-reentered"):
			return
		previous_entity = entity
		if not check(spawner.despawn(entity) == OK, "Server despawn"):
			return
		publish("despawn-requested")
		phase = 6
	elif phase == 6:
		if not exists("1-despawned") or not exists("2-despawned"):
			return
		entity = spawner.spawn(7, peer_for(1), {"label": "reconnect"})
		if not check(entity > 0 and entity != previous_entity, "Fresh entity generation after despawn"):
			return
		publish("reconnect-requested")
		phase = 7
	elif phase == 7:
		if peer_for(1) != -1:
			return
		token(1, "-reconnect")
		phase = 8
	elif phase == 8:
		var peer := peer_for(1)
		var actor = spawner.get_spawned_node(entity)
		if peer == -1 or not spawner.is_peer_compatible(peer) or actor.owner_value != 22:
			return
		if not check(peer != original_peer and spawner.get_owner_client_id(entity) == 10001, "Reconnect preserves identity without trusting recycled peer slot"):
			return
		publish("complete")
		phase = 9
	elif phase == 9:
		if exists("1-done") and exists("2-done"):
			finish(true)

func client_step(client: int) -> void:
	if phase == 0:
		if not exists("client%d.bin" % client):
			return
		if not check(legacy.join_token(10000 + client, FileAccess.get_file_as_bytes(directory.path_join("client%d.bin" % client))) == OK, "Authenticated client connection"):
			return
		phase = 1
	if session.get_state() != "Connected":
		if phase == 4 and exists("client1-reconnect.bin"):
			if not check(legacy.join_token(10001, FileAccess.get_file_as_bytes(directory.path_join("client1-reconnect.bin"))) == OK, "Fresh token reconnect"):
				return
			phase = 5
		return
	if client == 2 and not scene_schema_repaired and spawner.get_statistics().rejected > 0:
		if not check(spawner.get_statistics().instances == 0 and not spawner.is_peer_compatible(0), "Mismatched scene allowlist fails closed before scene instantiation"):
			return
		var scenes: Array[SuperpositionScene] = spawner.scenes
		scenes[0].prefab_id = 7
		spawner.scenes = scenes
		scene_schema_repaired = true
	entity = first_entity()
	if entity != 0 and not first_seen:
		var actor = spawner.get_spawned_node(entity)
		if not check(actor.get_meta("superposition_spawn_data").label == "initial" and spawner.get_owner_client_id(entity) == 10001, "Late baseline instantiates allowlisted scene and ownership"):
			return
		if not check_component_binding(actor, entity):
			return
		first_seen = true
		check(legacy.send_message(0, &"coexist_probe", [client]) == OK, "Legacy typed codec message beside native RPC")
		if client == 1:
			check(rpc.send_rpc(entity, 1, [11]) == OK, "Owner RPC send")
		else:
			publish("2-latejoin")
			check(rpc.send_rpc(entity, 1, [999]) == OK, "Non-owner request sent for server rejection")
			forged(2, ["forged-authority"])
			forged(3, [true], "mismatched-schema")
			forged(3, [{"unexpected": "container"}])
			check(spawner.spawn(7) == 0 and spawner.despawn(entity) == ERR_UNAUTHORIZED, "Client cannot spawn or despawn authoritative scene")
		check(rpc.send_rpc(entity, 3, [true]) == OK, "Any-peer RPC send")
	if client == 2 and entity != 0 and not exists("2-properties"):
		var actor = spawner.get_spawned_node(entity)
		if actor.owner_value == 11 and actor.any_count == 2:
			check(true, "Both nested property streams replicate after late join")
			publish("2-properties")
	if entity != 0 and spawner.get_spawned_node(entity).authority_text == "server-authority":
		if not exists("%d-authority" % client):
			publish("%d-authority" % client)
	if client == 2 and first_seen and entity == 0 and not exists("despawn-requested"):
		hidden_seen = true
		publish("2-hidden")
	if client == 2 and hidden_seen and entity != 0 and not exists("2-reentered"):
		check(spawner.get_spawned_node(entity).get_meta("superposition_spawn_data").label == "initial", "Interest re-entry recreates matching scene")
		publish("2-reentered")
	if exists("despawn-requested") and entity == 0 and not exists("%d-despawned" % client):
		check(first_seen, "Explicit despawn removes local spawned node")
		publish("%d-despawned" % client)
	if client == 1 and phase == 1 and exists("reconnect-requested") and entity != 0:
		session.stop()
		phase = 4
		return
	if client == 1 and phase == 5 and entity != 0 and not owner_sent:
		check(spawner.get_spawned_node(entity).get_meta("superposition_spawn_data").label == "reconnect", "Reconnect baseline restores scene")
		check(rpc.send_rpc(entity, 1, [22]) == OK, "Same authenticated owner may RPC after reconnect")
		owner_sent = true
	if exists("complete"):
		if not check(sdk_reply and legacy.get_state() == "Connected" and legacy.get("_blocked_peers").is_empty(), "Legacy wrapper and native components remain admitted together"):
			return
		publish("%d-done" % client)
		finish(true)

func receive_sdk_probe(peer: int, arguments: Array) -> void:
	if not check(arguments.size() == 1 and arguments[0] is int and peer == peer_for(arguments[0]), "Legacy message carries authenticated client identity"):
		return
	sdk_received += 1
	check(legacy.send_message(peer, &"coexist_ack", arguments) == OK, "Legacy server reply beside native messages")

func receive_sdk_ack(_peer: int, arguments: Array) -> void:
	if not check(arguments == [int(role)], "Legacy client reply validated"):
		return
	sdk_reply = true
	publish("%s-sdk" % role)

func finish(passed: bool, error: String = "") -> void:
	if completed:
		return
	completed = true
	print("EGP_SPAWNER_RPC_RESULT=" + JSON.stringify({"passed": passed, "role": role, "checks": checks, "error": error, "phase": phase, "compiled_assets": compiled_assets, "legacy_sdk_received": sdk_received, "legacy_sdk_reply": sdk_reply, "spawner": spawner.get_statistics(), "rpc": rpc.get_statistics()}))
	session.stop()
	quit.call_deferred(0 if passed else 1)
