extends SceneTree

var server := SuperpositionWorld.new()
var client := SuperpositionWorld.new()
var server_spawner := SuperpositionSpawner.new()
var client_spawner := SuperpositionSpawner.new()
var server_rpc := SuperpositionRPC.new()
var client_rpc := SuperpositionRPC.new()
var checks := 0
var finished := false
var entity: int = 0

func _initialize() -> void:
	run.call_deferred()

func check(value: bool, message: String) -> bool:
	checks += 1
	if not value:
		finish(false, message)
	return value

func configure_components(world: SuperpositionWorld, spawner: SuperpositionSpawner, rpc: SuperpositionRPC) -> void:
	spawner.name = "Spawner"
	spawner.replication_key = "world-restart-fixture"
	var scene := SuperpositionScene.new()
	scene.prefab_id = 7
	scene.scene = load("res://spawner_rpc_actor.tscn")
	var scenes: Array[SuperpositionScene] = [scene]
	spawner.scenes = scenes
	world.add_child(spawner)
	rpc.name = "RPC"
	rpc.spawner_path = NodePath("../Spawner")
	var rule := SuperpositionRPCMethod.new()
	rule.method_id = 1
	rule.method = &"owner_action"
	rule.permission = SuperpositionRPCMethod.OWNER
	rule.argument_types = PackedInt32Array([TYPE_INT])
	var methods: Array[SuperpositionRPCMethod] = [rule]
	rpc.methods = methods
	world.add_child(rpc)

func pump() -> void:
	if server.get_session() != null:
		server.poll()
	if client.get_session() != null:
		client.poll()

func wait_for(predicate: Callable, message: String) -> bool:
	for frame in range(1000):
		pump()
		if predicate.call():
			return check(true, message)
		await process_frame
	return check(false, message + " timed out")

func owner_peer() -> int:
	if server.get_session() == null:
		return -1
	for peer in server.get_session().command(&"peers"):
		if peer.client_id == 91001:
			return peer.peer_id
	return -1

func join() -> bool:
	var issued: Dictionary = server.get_session().issue_token(91001, "127.0.0.1:%d" % server.get_statistics().session.local_port)
	return check(issued.error == OK and client.join_token(91001, issued.token) == OK, "World authenticated join")

func run() -> void:
	server.name = "ServerWorld"
	client.name = "ClientWorld"
	server.port = 0
	client.role = 1
	server.auto_poll = false
	client.auto_poll = false
	server.game_protocol = "egp-spawner-world-fixture-v1"
	client.game_protocol = server.game_protocol
	root.add_child(server)
	root.add_child(client)
	configure_components(server, server_spawner, server_rpc)
	configure_components(client, client_spawner, client_rpc)
	await process_frame
	if not check(server_spawner.get_session() == null and client_spawner.get_session() == null, "Children created before World configuration remain unbound"):
		return
	if not check(server.start_server() == OK, "World starts configured server") or not join():
		return
	if not await wait_for(func(): return server_spawner.get_session() == server.get_session() and client_spawner.get_session() == client.get_session(), "Automatic ancestor provider discovery"):
		return
	if not await wait_for(func(): return owner_peer() != -1 and server_spawner.is_peer_compatible(owner_peer()) and client_spawner.is_peer_compatible(0), "World scene handshake"):
		return
	entity = server_spawner.spawn(7, owner_peer(), {"label": "world"})
	if not check(entity != 0, "World-backed scene spawn"):
		return
	if not await wait_for(func(): return client_spawner.has_entity(entity), "Automatic client scene instantiation"):
		return
	if not check(client_rpc.send_rpc(entity, 1, [99]) == OK, "Queue pre-restart owner RPC"):
		return
	var queued := false
	for frame in range(1000):
		pump()
		if server_rpc.get_statistics().queued > 0:
			queued = true
			break
		await process_frame
	if not check(queued and server_spawner.get_spawned_node(entity).owner_value == 0, "RPC remains queued outside receive callback"):
		return
	var old_entity := entity
	var old_server_session := server.get_session()
	var old_client_session := client.get_session()
	server.stop()
	client.stop()
	if not check(server_rpc.get_statistics().queued == 0, "Disconnect invalidates queued RPC before dispatch"):
		return
	await process_frame
	await process_frame
	if not check(server_spawner.get_session() == null and client_spawner.get_session() == null and not server_spawner.has_entity(entity), "World stop clears automatically bound scene sessions"):
		return
	if not check(server.start_server() == OK, "World restart creates a fresh native session") or not join():
		return
	if not await wait_for(func(): return server_spawner.get_session() == server.get_session() and client_spawner.get_session() == client.get_session(), "Automatic provider rebinding after restart"):
		return
	if not check(server.get_session() != old_server_session and client.get_session() != old_client_session, "Rebinding uses new session references"):
		return
	if not await wait_for(func(): return owner_peer() != -1 and server_spawner.is_peer_compatible(owner_peer()), "Fresh session scene handshake"):
		return
	entity = server_spawner.spawn(7, owner_peer(), {"label": "restart"})
	if not check(entity == old_entity, "New native session deliberately reuses retired entity number"):
		return
	if not await wait_for(func(): return client_spawner.has_entity(entity), "Restart late baseline recreates scene"):
		return
	for frame in range(5):
		pump()
		await process_frame
	if not check(server_spawner.get_spawned_node(entity).owner_value == 0 and server_rpc.get_statistics().received == 0, "Old queued RPC cannot affect reused entity ID in new session"):
		return
	if not check(client_rpc.send_rpc(entity, 1, [43]) == OK, "Fresh owner RPC after automatic restart"):
		return
	if not await wait_for(func(): return server_spawner.get_spawned_node(entity).owner_value == 43, "Fresh session RPC dispatch"):
		return
	var manual := EGPNetSession.new()
	if not check(manual.configure({"game_protocol": "manual-session-v1"}) == OK, "Explicit alternate session"):
		return
	server_spawner.set_session(manual)
	await process_frame
	await process_frame
	if not check(server_spawner.get_session() == manual, "Manual explicit binding is not overwritten by ancestor discovery"):
		return
	server_spawner.set_session(null)
	if not await wait_for(func(): return server_spawner.get_session() == server.get_session(), "Cleared manual binding resumes automatic discovery"):
		return
	manual.stop()
	finish(true)

func finish(passed: bool, error: String = "") -> void:
	if finished:
		return
	finished = true
	print("EGP_SPAWNER_WORLD_RESULT=" + JSON.stringify({"passed": passed, "checks": checks, "error": error, "server_rpc": server_rpc.get_statistics()}))
	server.stop()
	client.stop()
	quit.call_deferred(0 if passed else 1)
