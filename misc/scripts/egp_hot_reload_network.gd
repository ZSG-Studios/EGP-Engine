extends Node
## Isolated native sessions retained by live C++ and C# reloadable objects.
var native: Node
var managed: Node
var server: RefCounted
var client: RefCounted
var port := 0
var peer := 0
var entity := 0
var old_peer := 0
var old_entity := 0
var epoch := 0
var running := false
var pause_authority := false
var error_message := ""
var diagnostics: Array[String] = []
var states: Array[String] = []
var packets: Array[Dictionary] = []
var client_polls := 0

func setup(cpp: Node, cs: Node) -> void:
	native = cpp
	managed = cs

func check(condition: bool, message: String) -> bool:
	if not condition and error_message.is_empty():
		error_message = message
	return condition

func _process(_delta: float) -> void:
	if not running:
		return
	# C++ generated bindings and C# dynamic native calls remain usable after reload.
	if native.has_method("poll_network") and managed.has_method("PollNetwork"):
		check(native.poll_network(not pause_authority) == OK, "C++ network pump failed")
		check(managed.PollNetwork(not pause_authority) == OK, "C# network pump failed")
		client_polls += 2

func wait_until(predicate: Callable, timeout_ms := 5000) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while not predicate.call() and error_message.is_empty() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return check(predicate.call(), "network state watchdog")

func references_ok() -> bool:
	var cpp: Dictionary = native.network_state
	var cs: Dictionary = managed.NetworkState
	return cpp.get("server") == server and cpp.get("client") == client and cs.get("server") == server and cs.get("client") == client

func baseline_ok() -> bool:
	var rows: Array = client.command("entities")
	return rows.size() == 1 and rows[0].entity == entity and rows[0].authority_peer == peer and rows[0].state == PackedByteArray([epoch, 0, 255, 42])

func admit() -> bool:
	epoch += 1
	if not check(server.listen(port, "127.0.0.1") == OK, "authority rebind failed"):
		return false
	port = server.get_statistics().local_port
	var issued: Dictionary = server.issue_token(424242, "127.0.0.1:%d" % port)
	if not check(issued.error == OK and issued.token.size() == 2048, "fresh token issuance failed"):
		return false
	check(client.connect_token(424242, issued.token, "127.0.0.1") == OK, "fresh token join failed")
	issued.token.clear()
	pause_authority = false
	running = true
	if not await wait_until(func(): return client.get_state() == "Connected"):
		return false
	var peers: Array = server.command("peers")
	if not check(peers.size() == 1 and peers[0].client_id == 424242, "admission identity mismatch"):
		return false
	peer = peers[0].peer_id
	var spawned: Dictionary = server.command("spawn", {"kind": 17, "authority_peer": peer, "state": PackedByteArray([epoch, 0, 255, 42])})
	check(spawned.error == OK, "fresh owned entity spawn failed")
	entity = spawned.entity
	if not await wait_until(baseline_ok):
		return false
	check(client.send_application(0, PackedByteArray([epoch, 0, 255, 42])) == OK, "fresh application send failed")
	if not await wait_until(func(): return packets.size() == epoch):
		return false
	# Observe multiple authority ticks after delivery to detect duplicated callbacks.
	var tick: int = server.get_statistics().tick
	if not await wait_until(func(): return server.get_statistics().tick >= tick + 8):
		return false
	check(packets.size() == epoch and native.network_hits == epoch and managed.NetworkHits == epoch, "reload lost or duplicated native-session callbacks")
	return error_message.is_empty()

func snapshot(action: String) -> Dictionary:
	return {"action": action, "passed": error_message.is_empty(), "error": error_message,
		"pid": OS.get_process_id(), "epoch": epoch, "server_id": str(server.get_instance_id()), "client_id": str(client.get_instance_id()),
		"server_state": server.get_state(), "client_state": client.get_state(), "references_ok": references_ok(),
		"server_tick": server.get_statistics().tick, "peers": server.command("peers").size(), "entities": client.command("entities").size(),
		"peer": peer, "entity": entity, "old_peer": old_peer, "old_entity": old_entity,
		"cpp_hits": native.network_hits, "cs_hits": managed.NetworkHits, "packets": packets.duplicate(true),
		"diagnostics": diagnostics.duplicate(), "states": states.duplicate()}

func run_action(action: String) -> Dictionary:
	var evidence: Dictionary = {}
	match action:
		"network-start":
			server = ClassDB.instantiate("EGPNetSession")
			client = ClassDB.instantiate("EGPNetSession")
			var config := {"game_protocol": "egp-reload-fault-v1", "max_players": 1, "timeout_seconds": 3}
			check(server.configure(config) == OK and client.configure(config) == OK, "configuration failed")
			server.diagnostic.connect(func(message: String): diagnostics.append(message))
			client.state_changed.connect(func(state: String): states.append(state))
			server.application_received.connect(func(sender: int, payload: PackedByteArray): packets.append({"peer": sender, "payload": payload.hex_encode()}))
			server.application_received.connect(Callable(native, "receive_network"))
			server.application_received.connect(Callable(managed, "ReceiveNetwork"))
			var retained := {"server": server, "client": client}
			native.network_state = retained
			managed.NetworkState = retained
			await admit()
		"network-fault":
			old_peer = peer
			old_entity = entity
			pause_authority = true
			var polls_before := client_polls
			var started := Time.get_ticks_msec()
			while Time.get_ticks_msec() - started < 550:
				await get_tree().process_frame
			evidence.gap_ms = Time.get_ticks_msec() - started
			evidence.client_polls = client_polls - polls_before
			evidence.poll_error = server.poll()
			check(evidence.poll_error == FAILED, "authority failed to reject missed fixed-clock budget")
			await wait_until(func(): return client.get_state() == "Disconnected")
			client.stop()
			running = false
			check(evidence.client_polls >= 20, "client did not keep polling during authority stall")
			check(server.command("peers").is_empty() and server.command("entities").is_empty() and client.command("entities").is_empty(), "fault retained obsolete network cache")
			check(diagnostics == ["Fixed simulation exceeded its catch-up budget; resynchronization required."], "fault diagnostic missing or duplicated")
		"network-stopped":
			check(references_ok(), "native sessions lost during language reload")
			check(server.get_state() == "Stopped" and client.get_state() == "Stopped" and server.get_statistics().tick == 0, "reload implicitly restarted stopped authority")
			check(native.poll_network(true) == OK and managed.PollNetwork(true) == OK, "restored language API cannot poll stopped sessions")
		"network-recover":
			check(references_ok(), "recovery replaced retained native sessions")
			await admit()
			evidence.retired_peer_error = server.send_application(old_peer, PackedByteArray([9]))
			evidence.retired_entity_error = server.command("update_entity", {"entity": old_entity, "state": PackedByteArray([9])})
			check(peer > old_peer and entity > old_entity and evidence.retired_peer_error == ERR_DOES_NOT_EXIST and evidence.retired_entity_error == ERR_DOES_NOT_EXIST, "recovery reused obsolete handles")
		_:
			check(false, "unknown network action")
	var proof := snapshot(action)
	proof.merge(evidence)
	return proof

func close() -> void:
	running = false
	if client != null:
		client.close()
	if server != null:
		server.close()
