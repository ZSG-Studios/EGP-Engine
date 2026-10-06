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
var baseline_sequence := 0
var client_packets: Array[Dictionary] = []
var simulation: Dictionary = {}
var world: RefCounted
var checkpoint: PackedByteArray
var checkpoint_tick := 0
var checkpoint_hash := ""
var checkpoint_y := 0.0
var clock_offset := 0
const BODY_ID := 10000

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
	return cpp.get("server") == server and cpp.get("client") == client and cs.get("server") == server and cs.get("client") == client and (world == null or (cpp.get("world") == world and cs.get("world") == world))

func physics_payload() -> PackedByteArray:
	var payload := PackedByteArray([baseline_sequence, 0, 255, 42])
	if world != null:
		var body: Dictionary = world.get_body_state(BODY_ID)
		payload.append_array(var_to_bytes({"body_id": BODY_ID, "physics_tick": world.get_tick(), "position": body.position, "linear_velocity": body.linear_velocity}))
	return payload

func client_physics() -> Dictionary:
	var rows: Array = client.command("entities")
	if world == null or rows.size() != 1 or rows[0].state.size() <= 4:
		return {}
	var body = bytes_to_var(rows[0].state.slice(4))
	return body if body is Dictionary else {}

func physics_tick(_tick: int, authority: bool) -> void:
	if world == null or not authority:
		return
	check(world.step_tick(world.get_tick() + 1) == OK, "authoritative physics step failed")
	if entity != 0:
		check(server.command("update_entity", {"entity": entity, "state": physics_payload()}) == OK, "physics baseline publication failed")

func baseline_ok() -> bool:
	var rows: Array = client.command("entities")
	if rows.size() != 1 or rows[0].entity != entity or rows[0].authority_peer != peer or rows[0].state.slice(0, 4) != PackedByteArray([baseline_sequence, 0, 255, 42]):
		return false
	if world == null:
		return rows[0].state.size() == 4
	var body := client_physics()
	return body.get("body_id") == BODY_ID and body.get("physics_tick", 0) > clock_offset and body.physics_tick <= world.get_tick()

func admit() -> bool:
	epoch += 1
	baseline_sequence = epoch
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
	var spawned: Dictionary = server.command("spawn", {"kind": 17, "authority_peer": peer, "state": physics_payload()})
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
	var rows: Array = client.command("entities")
	var facade: Dictionary = managed.GetFacadeState() if managed.has_method("GetFacadeState") else {}
	if not facade.is_empty():
		facade.server_connections = {}
		facade.client_connections = {}
		for signal_name in ["state_changed", "peer_connected", "peer_disconnected", "application_received", "packet_received", "simulation_tick", "diagnostic"]:
			facade.server_connections[signal_name] = server.get_signal_connection_list(signal_name).size()
			facade.client_connections[signal_name] = client.get_signal_connection_list(signal_name).size()
	var physics: Dictionary = {}
	if world != null:
		var cpp: Dictionary = native.get_physics_state()
		var cs: Dictionary = managed.GetPhysicsState()
		var body: Dictionary = world.get_body_state(BODY_ID)
		var received := client_physics()
		physics = {"enabled": true, "world_id": str(world.get_instance_id()), "body_id": BODY_ID, "body_count": world.get_body_count(),
			"tick": world.get_tick(), "hash": world.get_state_hash(), "fingerprint": world.get_simulation_fingerprint(),
			"position_y": body.position.y, "velocity_y": body.linear_velocity.y,
			"clock_offset": clock_offset, "checkpoint_tick": checkpoint_tick, "checkpoint_hash": checkpoint_hash,
			"cpp_state_ok": cpp.get("position") == body.position and cpp.get("linear_velocity") == body.linear_velocity and cpp.get("rotation") == body.rotation and cpp.get("angular_velocity") == body.angular_velocity and cpp.get("tick") == world.get_tick() and cpp.get("hash") == world.get_state_hash(),
			"cs_state_ok": cs.get("position") == body.position and cs.get("linear_velocity") == body.linear_velocity and cs.get("rotation") == body.rotation and cs.get("angular_velocity") == body.angular_velocity and cs.get("tick") == world.get_tick() and cs.get("hash") == world.get_state_hash(),
			"client_body_id": received.get("body_id", 0), "client_tick": received.get("physics_tick", 0),
			"client_position_y": received.position.y if received.has("position") else 0.0}
		check(physics.cpp_state_ok and physics.cs_state_ok and physics.tick == clock_offset + server.get_statistics().tick, "language physics state or fixed clock offset mismatch")
	return {"action": action, "passed": error_message.is_empty(), "error": error_message,
		"pid": OS.get_process_id(), "epoch": epoch, "server_id": str(server.get_instance_id()), "client_id": str(client.get_instance_id()),
		"server_state": server.get_state(), "client_state": client.get_state(), "references_ok": references_ok(),
		"server_tick": server.get_statistics().tick, "peers": server.command("peers").size(), "entities": client.command("entities").size(),
		"peer": peer, "entity": entity, "old_peer": old_peer, "old_entity": old_entity,
		"cpp_hits": native.network_hits, "cs_hits": managed.NetworkHits, "packets": packets.duplicate(true),
		"diagnostics": diagnostics.duplicate(), "states": states.duplicate(),
		"port": port, "total_client_polls": client_polls, "sequence": baseline_sequence,
		"client_packets": client_packets.duplicate(true), "simulation": simulation.duplicate(),
		"baseline_hex": rows[0].state.slice(0, 4).hex_encode() if rows.size() == 1 else "",
		"revision": rows[0].revision if rows.size() == 1 else 0, "physics": physics,
		"facade": facade}

func run_action(action: String) -> Dictionary:
	var evidence: Dictionary = {}
	match action:
		"network-start", "network-live-start":
			if managed.has_method("CreateNetworkSessions"):
				var owned: Dictionary = managed.CreateNetworkSessions()
				server = owned.server
				client = owned.client
			else:
				server = ClassDB.instantiate("EGPNetSession")
				client = ClassDB.instantiate("EGPNetSession")
			var config := {"game_protocol": "egp-reload-fault-v1", "max_players": 1, "timeout_seconds": 3}
			if FileAccess.file_exists("res://physics_enabled"):
				world = ClassDB.instantiate("EGPBox3DWorld")
				check(world.configure(60, 4, 1, Vector3(0, -9.8, 0)) == OK and world.queue_create_box(BODY_ID, 1, Vector3(0, 10000, 0), Vector3(0.5, 0.5, 0.5)) == OK and world.apply_queued_commands() == OK, "physics world setup failed")
				config.simulation_fingerprint = world.get_simulation_fingerprint()
				server.simulation_tick.connect(physics_tick)
			if action == "network-live-start":
				simulation = JSON.parse_string(FileAccess.get_file_as_string("res://network_options.json"))
				config.merge(simulation)
			check(server.configure(config) == OK and client.configure(config) == OK, "configuration failed")
			server.diagnostic.connect(func(message: String): diagnostics.append(message))
			client.state_changed.connect(func(state: String): states.append(state))
			server.application_received.connect(func(sender: int, payload: PackedByteArray): packets.append({"peer": sender, "payload": payload.hex_encode()}))
			server.application_received.connect(Callable(native, "receive_network"))
			server.application_received.connect(Callable(managed, "ReceiveNetwork"))
			client.application_received.connect(func(sender: int, payload: PackedByteArray): client_packets.append({"peer": sender, "payload": payload.hex_encode()}))
			var retained := {"server": server, "client": client}
			if world != null:
				retained.world = world
			native.network_state = retained
			managed.NetworkState = retained
			await admit()
			if action == "network-live-start":
				check(server.send_application(peer, PackedByteArray([128 + baseline_sequence, 0, 255, 42])) == OK, "initial server application send failed")
				await wait_until(func(): return client_packets.size() == baseline_sequence)
		"network-live-check":
			check(references_ok() and client.get_state() == "Connected" and server.get_state() == "Listening", "active reload interrupted native session")
			baseline_sequence += 1
			check(server.command("update_entity", {"entity": entity, "state": physics_payload()}) == OK, "live entity update failed")
			await wait_until(baseline_ok)
			check(client.send_application(0, PackedByteArray([baseline_sequence, 0, 255, 42])) == OK and server.send_application(peer, PackedByteArray([128 + baseline_sequence, 0, 255, 42])) == OK, "live application exchange failed")
			await wait_until(func(): return packets.size() == baseline_sequence and client_packets.size() == baseline_sequence)
			var tick: int = server.get_statistics().tick
			await wait_until(func(): return server.get_statistics().tick >= tick + 8)
			check(packets.size() == baseline_sequence and client_packets.size() == baseline_sequence and native.network_hits == baseline_sequence and managed.NetworkHits == baseline_sequence, "active reload lost or duplicated callbacks")
		"network-fault":
			old_peer = peer
			old_entity = entity
			pause_authority = true
			if world != null:
				checkpoint = world.capture_snapshot()
				checkpoint_tick = world.get_tick()
				checkpoint_hash = world.get_state_hash()
				checkpoint_y = world.get_body_state(BODY_ID).position.y
				check(not checkpoint.is_empty(), "trusted physics checkpoint capture failed")
				var file := FileAccess.open("res://physics_checkpoint.bin", FileAccess.WRITE)
				file.store_buffer(checkpoint)
				file.close()
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
			clock_offset = checkpoint_tick
			check(evidence.client_polls >= 20, "client did not keep polling during authority stall")
			check(server.command("peers").is_empty() and server.command("entities").is_empty() and client.command("entities").is_empty(), "fault retained obsolete network cache")
			check(diagnostics == ["Fixed simulation exceeded its catch-up budget; resynchronization required."], "fault diagnostic missing or duplicated")
		"network-stopped":
			check(references_ok(), "native sessions lost during language reload")
			check(server.get_state() == "Stopped" and client.get_state() == "Stopped" and server.get_statistics().tick == 0, "reload implicitly restarted stopped authority")
			check(native.poll_network(true) == OK and managed.PollNetwork(true) == OK, "restored language API cannot poll stopped sessions")
		"network-recover":
			check(references_ok(), "recovery replaced retained native sessions")
			if world != null:
				check(world.step_tick(checkpoint_tick + 1) == OK and world.get_state_hash() != checkpoint_hash, "physics rollback control did not change state")
				var changed_hash: String = world.get_state_hash()
				var corrupt := checkpoint.duplicate()
				corrupt[corrupt.size() - 1] ^= 255
				evidence.corrupt_snapshot_error = world.restore_snapshot(corrupt)
				evidence.corrupt_restore_unchanged = world.get_state_hash() == changed_hash and world.get_tick() == checkpoint_tick + 1
				check(evidence.corrupt_snapshot_error == ERR_FILE_CORRUPT and evidence.corrupt_restore_unchanged, "corrupt checkpoint changed live physics state")
				check(world.restore_snapshot(checkpoint) == OK, "trusted checkpoint restore failed")
				evidence.restored_hash = world.get_state_hash()
				evidence.restored_tick = world.get_tick()
				evidence.restored_y = world.get_body_state(BODY_ID).position.y
				check(evidence.restored_hash == checkpoint_hash and evidence.restored_tick == checkpoint_tick and evidence.restored_y == checkpoint_y, "trusted restore lost exact physics state")
				entity = 0
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
	if managed != null and managed.has_method("CloseFacades"):
		managed.CloseFacades()
	if client != null:
		client.close()
	if server != null:
		server.close()
