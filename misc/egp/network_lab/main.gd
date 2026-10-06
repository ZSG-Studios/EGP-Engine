extends Node
## Local encrypted multi-process test. Admission files are a test-only trusted handoff.
const Net = preload("res://addons/egp_net/egp_net.gd")
var net: Node
var options: Dictionary = {}
var role := ""
var directory := ""
var clients := 2
var index := 0
var duration := 8.0
var reconnect_at := 0.0
var started := 0
var finished := false
var reconnected := false
var reconnect_wait := false
var reconnect_started := 0
var sent := false
var replies := 0
var highest_tick := 0
var entity := 0
var admissions: Dictionary = {}
var generations: Dictionary = {}
var diagnostics: Array[String] = []
var label: Label
var restart_enabled := false
var epoch := 1
var persistent_value := 100
var restored_value := 0
var restart_wait := false
var restart_wait_started := 0
var disconnected := false
var cleared_on_disconnect := false
var connection_states: Array[String] = []
var epoch_ticks: Dictionary = {}
var epoch_replies: Dictionary = {}
var epoch_inputs: Dictionary = {}
var epoch_server_pids: Dictionary = {}
var owned_entity := 0
var owner_peer := -1
var input_sent := false
var input_clients: Dictionary = {}
var owned_entities: Dictionary = {}
var last_health := 0
var health_sequence := 0
var stall_enabled := false
var stall_injected := false
var stall_recovered := false
var stall_proof: Dictionary = {}
var stall_proofs: Array[Dictionary] = []
var stall_injections := 0
var input_generations: Dictionary = {}
var server_stall_enabled := false
var server_stall_injected := false
var server_stall_proof: Dictionary = {}
var epoch_owners: Dictionary = {}
var retired_probe: Node
var retired_probe_states: Array[String] = []
var retired_probe_started := 0
var server_retired_token := PackedByteArray()
var simulation_fingerprint := "script-state-v1"

func checks_ownership() -> bool:
	return restart_enabled or stall_enabled

func _ready() -> void:
	for argument in OS.get_cmdline_user_args():
		var pair := argument.split("=", true, 1)
		if pair.size() == 2:
			options[pair[0].trim_prefix("--")] = pair[1]
	role = options.get("role", "")
	directory = options.get("admissions", "")
	clients = int(options.get("clients", 2))
	index = int(options.get("index", 0))
	duration = float(options.get("duration", 8))
	reconnect_at = float(options.get("reconnect-at", 0))
	restart_enabled = options.get("restart-enabled", "0") == "1"
	stall_enabled = float(options.get("client-stall-at", 0)) > 0
	server_stall_enabled = float(options.get("server-stall-at", 0)) > 0
	epoch = int(options.get("generation", 1))
	restored_value = int(options.get("checkpoint-value", 0))
	persistent_value = restored_value if epoch > 1 else 100
	if role not in ["server", "host", "client"] or directory.is_empty() or clients < 1 or clients > 64:
		finish(false, "invalid lab arguments")
		return
	started = Time.get_ticks_msec()
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_title("EGP Network Lab — %s %d" % [role, index])
		label = Label.new()
		label.position = Vector2(20, 20)
		add_child(label)
	net = Net.new()
	net.auto_poll = false
	add_child(net)
	net.diagnostic.connect(func(message: String): diagnostics.append(message))
	net.state_changed.connect(observe_state)
	if not configure():
		return
	if role in ["server", "host"]:
		net.register_message(&"hello", receive_hello, Net.Sender.CLIENT)
		if net.host(int(options.get("port", 0)), "127.0.0.1") != OK:
			finish(false, "loopback listener failed")
			return
		entity = net.spawn(1, authority_state(0))
		if entity == 0:
			finish(false, "authority entity failed")
			return
		net.simulation_tick.connect(func(tick: int, server: bool):
			if server and net.update_entity(entity, authority_state(tick)) != OK:
				finish(false, "authority update failed"))
		net.input_received.connect(receive_input)
		var port: int = net.get_statistics().local_port
		for client in range(clients):
			if not publish_token(client, "-epoch-%d" % epoch if restart_enabled else ""):
				return
		if not publish_json("ready-%d.json" % epoch, {"port": port, "clients": clients, "generation": epoch, "pid": OS.get_process_id()}):
			finish(false, "readiness handoff failed")
			return
	else:
		net.register_message(&"reply", receive_reply, Net.Sender.SERVER)
		net.register_message(&"input_reply", receive_input_reply, Net.Sender.SERVER)
		join("-epoch-%d" % epoch if restart_enabled else "")

func configure() -> bool:
	var error: Error = net.configure({
		"game_protocol": "egp-network-lab-v1", "max_players": clients,
		"simulation_fingerprint": simulation_fingerprint,
		"max_entities": maxi(4, clients + 1), "timeout_seconds": 3 if checks_ownership() else 10, "token_lifetime_seconds": 120,
		"simulated_latency_ms": float(options.get("latency", 0)),
		"simulated_jitter_ms": float(options.get("jitter", 0)),
		"simulated_loss": float(options.get("loss", 0)),
	})
	if error != OK:
		finish(false, "session configuration failed: %d" % error)
	return error == OK

func publish_token(client: int, suffix: String) -> bool:
	var port: int = net.get_statistics().local_port
	var issued: Dictionary = net.issue_token(10000 + client, "127.0.0.1:%d" % port)
	if issued.error != OK or issued.token.size() != 2048:
		finish(false, "token issuance failed")
		return false
	var filename := directory.path_join("client-%d%s.bin" % [client, suffix])
	var file := FileAccess.open(filename + ".tmp", FileAccess.WRITE)
	if file == null:
		finish(false, "token handoff failed")
		return false
	file.store_buffer(issued.token)
	file.close()
	if DirAccess.rename_absolute(filename + ".tmp", filename) != OK:
		finish(false, "token handoff publication failed")
		return false
	return true

func join(suffix: String = "") -> void:
	var token := FileAccess.get_file_as_bytes(directory.path_join("client-%d%s.bin" % [index, suffix]))
	if token.size() != 2048 or net.join_token(10000 + index, token) != OK:
		finish(false, "encrypted join failed")

func receive_hello(peer: int, arguments: Array) -> void:
	if arguments.size() != 1 or not arguments[0] is int or arguments[0] < 0 or arguments[0] >= clients:
		finish(false, "invalid hello contract")
		return
	for record in net.get_peers():
		if record.peer_id == peer:
			if record.client_id != 10000 + arguments[0]:
				finish(false, "authenticated account mismatch")
				return
			admissions[arguments[0]] = true
			var connections: Dictionary = generations.get(arguments[0], {})
			connections[peer] = true
			generations[arguments[0]] = connections
			var reply: Array = [arguments[0]]
			if checks_ownership():
				if stall_enabled and owned_entities.has(arguments[0]):
					var old: int = owned_entities[arguments[0]]
					if net.get_entity(old).get("authority_peer", -2) != -1 or net.despawn(old) != OK:
						finish(false, "old connection ownership was not revoked")
						return
				var generation: int = connections.size() if stall_enabled else epoch
				var owned: int = net.spawn(2, {"account": record.client_id, "generation": generation}, peer)
				if owned == 0:
					finish(false, "client authority spawn failed")
					return
				owned_entities[arguments[0]] = owned
				reply = [arguments[0], generation, peer, owned]
			if net.send_message(peer, &"reply", reply) != OK:
				finish(false, "reply enqueue failed")
			return
	finish(false, "unknown authenticated peer")

func receive_reply(_peer: int, arguments: Array) -> void:
	if (not checks_ownership() and arguments != [index]) or (checks_ownership() and (arguments.size() != 4 or arguments[0] != index or arguments[1] != epoch)):
		finish(false, "reply contract mismatch")
		return
	replies += 1
	epoch_replies[epoch] = true
	if checks_ownership():
		owner_peer = arguments[2]
		owned_entity = arguments[3]
		epoch_owners[epoch] = {"peer": owner_peer, "entity": owned_entity}

func authority_state(tick: int) -> Dictionary:
	return {"tick": tick, "host_player": role == "host", "generation": epoch,
		"server_pid": OS.get_process_id(), "persistent_value": persistent_value, "restored_value": restored_value}

func publish_json(filename: String, value: Dictionary) -> bool:
	var path := directory.path_join(filename)
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value))
	file.close()
	return DirAccess.rename_absolute(path + ".tmp", path) == OK

func health() -> Dictionary:
	return {"pid": OS.get_process_id(), "generation": epoch, "admitted_clients": admissions.size(),
		"input_clients": input_clients.size(), "persistent_value": persistent_value,
		"root_authority": net.get_entity(entity).get("authority_peer", -2), "tick": net.get_statistics().get("tick", 0)}

func capture_checkpoint() -> Dictionary:
	return health()

func valid_owner_state(state: Dictionary) -> bool:
	return state == {"account": 10000 + index, "generation": epoch}

func observe_state(state: String) -> void:
	connection_states.append(state)
	if restart_enabled and role == "client" and epoch == 1 and state == "Disconnected" and epoch_replies.has(1):
		disconnected = true
		cleared_on_disconnect = net.get_entities().is_empty()
		restart_wait = true
		restart_wait_started = Time.get_ticks_msec()

func receive_input(peer: int, handle: int, input: Dictionary) -> void:
	if not checks_ownership():
		return
	for record in net.get_peers():
		if record.peer_id != peer:
			continue
		var client: int = record.client_id - 10000
		var generation: int = generations.get(client, {}).size() if stall_enabled else epoch
		var accepted: Dictionary = input_generations.get(client, {})
		if client < 0 or client >= clients or owned_entities.get(client, 0) != handle or input != {"account": record.client_id, "generation": generation, "sequence": 1} or accepted.has(generation):
			finish(false, "owner-authorized input contract failed")
			return
		input_clients[client] = true
		accepted[generation] = true
		input_generations[client] = accepted
		persistent_value += client + 1
		if net.send_message(peer, &"input_reply", [client, generation]) != OK:
			finish(false, "input acknowledgment enqueue failed")
		return
	finish(false, "input from unknown authenticated peer")

func receive_input_reply(_peer: int, arguments: Array) -> void:
	if not checks_ownership() or arguments != [index, epoch]:
		finish(false, "input acknowledgment contract failed")
		return
	epoch_inputs[epoch] = true
	if stall_recovered:
		stall_proof.input_acknowledged = true

func recover_server() -> void:
	if not cleared_on_disconnect or not net.get_entities().is_empty():
		finish(false, "stale replicated entities survived disconnect")
		return
	var path := directory.path_join("ready-2.json")
	if not FileAccess.file_exists(path):
		if Time.get_ticks_msec() - restart_wait_started > 5000 + int(float(options.get("server-down-for", 1)) * 1000):
			finish(false, "replacement admission watchdog")
		return
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK or not parser.data is Dictionary or int(parser.data.get("generation", 0)) != 2:
		finish(false, "invalid replacement readiness")
		return
	# Reconfigure only after poll returns; a fresh server issues a fresh token.
	net.close()
	epoch = 2
	restart_wait = false
	sent = false
	input_sent = false
	owned_entity = 0
	owner_peer = -1
	if configure():
		join("-epoch-2")

func reconnect_suffix(generation: int) -> String:
	return "-reconnect-%d" % generation if stall_enabled else "-reconnect"

func reconnect_request(client: int, generation: int) -> String:
	return "reconnect-%d-epoch-%d.request" % [client, generation] if stall_enabled else "reconnect-%d.request" % client

func request_reconnect() -> void:
	net.close()
	reconnect_wait = true
	reconnect_started = Time.get_ticks_msec()
	sent = false
	input_sent = false
	owned_entity = 0
	owner_peer = -1
	# This is the lab's trusted local backend. Production games use their auth service.
	var request := FileAccess.open(directory.path_join(reconnect_request(index, epoch)), FileAccess.WRITE)
	if request == null:
		finish(false, "admission refresh request failed")
		return
	request.close()

func recover_stall(error: Error) -> bool:
	if not stall_injected or stall_recovered or error != FAILED or diagnostics.size() != stall_injections or stall_proofs.size() + 1 != stall_injections:
		return false
	for message in diagnostics:
		if message != "Fixed simulation exceeded its catch-up budget; resynchronization required.":
			return false
	stall_proof.poll_error = error
	stall_proof.state_after_failure = net.get_state()
	stall_proof.entities_after_failure = net.get_entities().size()
	stall_proof.tick_after_failure = net.get_statistics().get("tick", -1)
	stall_proof.input_after_failure = net.send_input(stall_proof.old_entity, {})
	if stall_proof.state_after_failure != "Stopped" or stall_proof.entities_after_failure != 0 or stall_proof.tick_after_failure != 0 or stall_proof.input_after_failure != ERR_UNCONFIGURED:
		return false
	stall_recovered = true
	stall_proofs.append(stall_proof)
	epoch += 1
	# Poll has returned. Close/reconfigure safely, then obtain a fresh token/socket.
	request_reconnect()
	return not finished

func clients_acknowledged() -> bool:
	var paths := DirAccess.get_files_at(directory)
	for client in range(clients):
		var latest := ""
		for path in paths:
			if path.begins_with("client-health-%d-" % client) and path.ends_with(".json") and path > latest:
				latest = path
		if latest.is_empty():
			return false
		var record: Variant = JSON.parse_string(FileAccess.get_file_as_string(directory.path_join(latest)))
		if not record is Dictionary or record.get("generation") != 1 or record.get("state") != "Connected" or record.get("epoch_inputs", {}).get("1") != true:
			return false
	return true

func recover_server_stall(error: Error) -> bool:
	if not server_stall_injected or epoch != 1 or error != FAILED or diagnostics != ["Fixed simulation exceeded its catch-up budget; resynchronization required."]:
		return false
	server_stall_proof.poll_error = error
	server_stall_proof.state_after_failure = net.get_state()
	server_stall_proof.entities_after_failure = net.get_entities().size()
	server_stall_proof.peers_after_failure = net.get_peers().size()
	server_stall_proof.tick_after_failure = net.get_statistics().get("tick", -1)
	server_stall_proof.spawn_after_failure = net.spawn(1, {})
	server_stall_proof.update_after_failure = net.update_entity(entity, {})
	if net.get_state() != "Stopped" or not net.get_entities().is_empty() or not net.get_peers().is_empty() or server_stall_proof.tick_after_failure != 0 or server_stall_proof.spawn_after_failure != 0 or server_stall_proof.update_after_failure != ERR_UNAUTHORIZED:
		return false
	# Poll has returned. Keep the configured Session so retired handles stay retired.
	# listen() creates a new secure key/socket and starts a fresh fixed clock.
	net.stop()
	epoch = 2
	restored_value = server_stall_proof.checkpoint.persistent_value
	persistent_value = restored_value
	admissions.clear()
	input_clients.clear()
	owned_entities.clear()
	if net.host(server_stall_proof.port, "127.0.0.1") != OK:
		return false
	entity = net.spawn(1, authority_state(0))
	if entity == 0 or entity == server_stall_proof.old_root:
		return false
	server_stall_proof.new_root = entity
	server_stall_proof.recovered_port = net.get_statistics().local_port
	for client in range(clients):
		if not publish_token(client, "-epoch-2"):
			return false
	# Test an unused account before publishing readiness, while listener slots are free.
	# Duplicate account/full-server rejection must not mask a surviving retired key.
	retired_probe = Net.new()
	retired_probe.auto_poll = false
	add_child(retired_probe)
	retired_probe.state_changed.connect(func(state: String): retired_probe_states.append(state))
	retired_probe_started = Time.get_ticks_msec()
	var admitted: bool = retired_probe.configure({"game_protocol": "egp-network-lab-v1", "simulation_fingerprint": simulation_fingerprint, "timeout_seconds": 3}) == OK and retired_probe.join_token(900000, server_retired_token) == OK
	server_retired_token.clear()
	return admitted

func poll_retired_admission() -> bool:
	if retired_probe == null:
		return true
	if retired_probe.poll() != OK or retired_probe.get_state() in ["Connected", "Synchronizing"] or not retired_probe.get_entities().is_empty():
		finish(false, "retired admission reached recovered authority")
		return false
	if retired_probe.get_state() == "Disconnected":
		server_stall_proof.retired_admission = {"client_id": 900000, "states": retired_probe_states.duplicate(), "entities": retired_probe.get_entities().size(), "peers": retired_probe.get_peers().size(), "server_peers": net.get_peers().size(), "admitted_clients": admissions.size()}
		retired_probe.close()
		retired_probe.queue_free()
		retired_probe = null
		if not publish_json("ready-2.json", {"port": net.get_statistics().local_port, "clients": clients, "generation": epoch, "pid": OS.get_process_id()}):
			finish(false, "recovered readiness publication failed")
			return false
	elif Time.get_ticks_msec() - retired_probe_started > 5000:
		finish(false, "retired admission rejection watchdog")
		return false
	return true

func _process(_delta: float) -> void:
	if finished or net == null:
		return
	var elapsed := (Time.get_ticks_msec() - started) / 1000.0
	if restart_wait:
		recover_server()
		return
	if role == "client" and reconnect_at > 0 and elapsed >= reconnect_at and not reconnected:
		reconnected = true
		request_reconnect()
		if finished:
			return
	if reconnect_wait:
		if Time.get_ticks_msec() - reconnect_started > 5000:
			finish(false, "admission refresh watchdog")
			return
		if not FileAccess.file_exists(directory.path_join("client-%d%s.bin" % [index, reconnect_suffix(epoch)])) or Time.get_ticks_msec() - reconnect_started < 500:
			return
		reconnect_wait = false
		if not configure():
			return
		join(reconnect_suffix(epoch))
		if finished:
			return
	var next_stall := float(options.get("client-stall-at", 0)) + stall_injections * float(options.get("client-stall-interval", 8))
	if stall_enabled and role == "client" and index == int(options.get("client-stall-index", 0)) and stall_injections < int(options.get("client-stall-count", 1)) and elapsed >= next_stall and epoch_inputs.has(epoch) and (stall_injections == 0 or stall_proof.has("recovered_tick")) and net.get_state() == "Connected":
		stall_injected = true
		stall_recovered = false
		stall_injections += 1
		stall_proof = {"old_peer": owner_peer, "old_entity": owned_entity, "old_generation": epoch, "before_tick": highest_tick, "before_value": persistent_value, "injected_at_ms": Time.get_ticks_msec() - started}
		var before := Time.get_ticks_msec()
		OS.delay_msec(int(options.get("client-stall-ms", 750)))
		stall_proof.elapsed_ms = Time.get_ticks_msec() - before
	if server_stall_enabled and role != "client" and not server_stall_injected and elapsed >= float(options.get("server-stall-at", 0)) and admissions.size() == clients and input_clients.size() == clients and clients_acknowledged():
		var issued: Dictionary = net.issue_token(900000, "127.0.0.1:%d" % net.get_statistics().local_port)
		if issued.error != OK or issued.token.size() != 2048:
			finish(false, "retired probe admission issuance failed")
			return
		server_retired_token = issued.token
		server_stall_injected = true
		server_stall_proof = {"checkpoint": capture_checkpoint(), "port": net.get_statistics().local_port, "old_root": entity, "old_owners": owned_entities.duplicate(), "old_peers": generations.duplicate(true), "injected_at_ms": Time.get_ticks_msec() - started}
		if finished:
			return
		var before := Time.get_ticks_msec()
		OS.delay_msec(int(options.get("server-stall-ms", 750)))
		server_stall_proof.elapsed_ms = Time.get_ticks_msec() - before
	var poll_error: Error = net.poll()
	if poll_error != OK:
		if server_stall_enabled and role != "client" and recover_server_stall(poll_error):
			return
		if stall_enabled and role == "client" and recover_stall(poll_error):
			return
		finish(false, "poll failed")
		return
	if not poll_retired_admission():
		return
	if restart_wait:
		recover_server()
		return
	if restart_enabled and role != "client":
		if Time.get_ticks_msec() - last_health >= 100:
			last_health = Time.get_ticks_msec()
			# Immutable publications avoid replacing a file held open by a reader on Windows.
			health_sequence += 1
			if not publish_json("health-%d-%06d.json" % [epoch, health_sequence], health()):
				finish(false, "health publication failed")
				return
		if epoch == 1 and FileAccess.file_exists(directory.path_join("stop.request")):
			if not publish_json("checkpoint.json", health()):
				finish(false, "checkpoint publication failed")
				return
			finish(admissions.size() == clients and input_clients.size() == clients, "graceful restart checkpoint")
			return
	if role != "client" and (reconnect_at > 0 or stall_enabled):
		for client in range(clients):
			var generation: int = generations.get(client, {}).size() + 1 if stall_enabled else epoch
			if FileAccess.file_exists(directory.path_join(reconnect_request(client, generation))) and not FileAccess.file_exists(directory.path_join("client-%d%s.bin" % [client, reconnect_suffix(generation)])):
				if stall_enabled and net.get_peers().any(func(peer: Dictionary): return peer.client_id == 10000 + client):
					continue # Wait for native transport disconnect/ownership revocation.
				if not publish_token(client, reconnect_suffix(generation)):
					return
	if role == "client":
		for handle in net.get_entities():
			var record: Dictionary = net.get_entity(handle)
			var state: Dictionary = record.state
			highest_tick = maxi(highest_tick, int(state.get("tick", 0)))
			if checks_ownership() and record.kind == 1:
				if record.authority_peer != -1 or int(state.get("generation", 0)) != (1 if stall_enabled else epoch):
					finish(false, "stale generation or root authority")
					return
				epoch_ticks[epoch] = maxi(int(epoch_ticks.get(epoch, 0)), int(state.get("tick", 0)))
				epoch_server_pids[epoch] = int(state.get("server_pid", 0))
				if epoch == 2 or stall_enabled:
					restored_value = int(state.get("restored_value", 0))
					persistent_value = int(state.get("persistent_value", 0))
		if checks_ownership() and owned_entity != 0 and not input_sent and net.get_state() == "Connected":
			var record: Dictionary = net.get_entity(owned_entity)
			if not record.is_empty():
				if record.authority_peer != owner_peer or not valid_owner_state(record.state):
					finish(false, "client ownership mismatch")
					return
				if stall_recovered:
					stall_proof.new_peer = owner_peer
					stall_proof.new_entity = owned_entity
					stall_proof.new_generation = epoch
					if owner_peer == stall_proof.old_peer or owned_entity == stall_proof.old_entity or net.send_input(stall_proof.old_entity, {"account": 10000 + index, "generation": epoch, "sequence": 1}) != OK:
						finish(false, "fresh owner or stale-input enqueue failed")
						return
					stall_proof.stale_input_enqueued = true # Server must drop this revoked entity input.
				if server_stall_enabled and epoch == 2:
					var old: Dictionary = epoch_owners[1]
					if old.peer == owner_peer or old.entity == owned_entity or net.send_input(old.entity, {"account": 10000 + index, "generation": epoch, "sequence": 1}) != OK:
						finish(false, "server recovery reused retired ownership")
						return
					epoch_owners[2].stale_input_enqueued = true
				if net.send_input(owned_entity, {"account": 10000 + index, "generation": epoch, "sequence": 1}) != OK:
					finish(false, "owner input enqueue failed")
					return
				input_sent = true
		if net.get_state() == "Connected" and not sent:
			if net.send_message(0, &"hello", [index]) != OK:
				finish(false, "hello enqueue failed")
				return
			sent = true
		if stall_recovered and epoch_inputs.has(epoch) and highest_tick > stall_proof.before_tick and persistent_value > stall_proof.before_value and not stall_proof.has("recovered_tick"):
			stall_proof.recovered_tick = highest_tick
			stall_proof.recovered_value = persistent_value
		if restart_enabled and Time.get_ticks_msec() - last_health >= 1000:
			last_health = Time.get_ticks_msec()
			health_sequence += 1
			if not publish_json("client-health-%d-%06d.json" % [index, health_sequence], {
				"pid": OS.get_process_id(), "generation": epoch, "state": net.get_state(),
				"replies": replies, "entities": net.get_entities(), "owned_entity": owned_entity,
				"input_sent": input_sent, "epoch_inputs": epoch_inputs, "highest_tick": highest_tick}):
				finish(false, "client health publication failed")
				return
	if label != null:
		label.text = "EGP Network Lab\n%s %d · %s\n%.1f / %.1f seconds\nPeers: %d · server tick: %d\nLatency: %s ms · jitter: %s ms · loss: %s%%\nReplies: %d · diagnostics: %d" % [role, index, net.get_state(), elapsed, duration, net.get_peers().size(), highest_tick, options.get("latency", "0"), options.get("jitter", "0"), options.get("loss", "0"), replies, diagnostics.size()]
		if stall_enabled:
			if role == "client" and index == int(options.get("client-stall-index", 0)):
				label.text += "\nStalls: %d / %s · last recovery complete: %s" % [stall_injections, options.get("client-stall-count", "1"), stall_proof.has("recovered_tick")]
			else:
				label.text += "\nWatching client %s: %s gaps of %s ms" % [options.get("client-stall-index", "0"), options.get("client-stall-count", "1"), options.get("client-stall-ms", "750")]
		if server_stall_enabled:
			label.text += "\nServer gap: %s ms · generation: %d · checkpoint: %d" % [options.get("server-stall-ms", "750"), epoch, restored_value]
	if elapsed >= duration + (2.0 if role != "client" else 0.0):
		if role == "client":
			var stalled_client := stall_enabled and index == int(options.get("client-stall-index", 0))
			var passed := replies >= (2 if reconnect_at > 0 or restart_enabled or stalled_client else 1) and highest_tick > 1
			if restart_enabled:
				passed = passed and disconnected and cleared_on_disconnect and epoch == 2 and epoch_ticks.get(1, 0) > 1 and epoch_ticks.get(2, 0) > 1 and epoch_inputs.has(1) and epoch_inputs.has(2) and restored_value >= 100 and persistent_value > restored_value
			if stall_enabled:
				passed = passed and epoch_inputs.has(1) and epoch_ticks.get(1, 0) > 1
				if stalled_client:
					passed = passed and stall_injections == int(options.get("client-stall-count", 1)) and stall_proofs.size() == stall_injections and epoch == stall_injections + 1 and replies == epoch
					for proof in stall_proofs:
						passed = passed and proof.get("input_acknowledged", false) and proof.get("recovered_tick", 0) > proof.before_tick and proof.get("recovered_value", 0) > proof.before_value and epoch_inputs.has(proof.new_generation) and epoch_server_pids.get(proof.old_generation) == epoch_server_pids.get(proof.new_generation)
			finish(passed, "encrypted admission, replies and replicated tick; restart authority/state when requested")
		else:
			var passed := admissions.size() == clients
			if checks_ownership():
				passed = passed and input_clients.size() == clients
			if reconnect_at > 0:
				for client in range(clients):
					passed = passed and generations.get(client, {}).size() >= 2
			if stall_enabled:
				for client in range(clients):
					passed = passed and generations.get(client, {}).size() == (int(options.get("client-stall-count", 1)) + 1 if client == int(options.get("client-stall-index", 0)) else 1)
			if server_stall_enabled:
				passed = passed and server_stall_injected and epoch == 2 and server_stall_proof.has("retired_admission") and restored_value == 100 + clients * (clients + 1) / 2 and persistent_value == 100 + clients * (clients + 1)
				for client in range(clients):
					passed = passed and generations.get(client, {}).size() == 2 and input_generations.get(client, {}).size() == 2
			finish(passed, "authenticated clients and connection generations")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	var statistics: Dictionary = net.get_statistics() if net != null else {}
	if net != null:
		net.close()
	if retired_probe != null:
		retired_probe.close()
	print("EGP_NETWORK_LAB " + JSON.stringify({"passed": passed, "role": role, "index": index, "message": message, "replies": replies, "highest_tick": highest_tick, "admitted_clients": admissions.size(), "diagnostics": diagnostics, "statistics": statistics,
		"generation": epoch, "restart_enabled": restart_enabled, "disconnected": disconnected,
		"cleared_on_disconnect": cleared_on_disconnect, "connection_states": connection_states,
		"epoch_ticks": epoch_ticks, "epoch_inputs": epoch_inputs, "epoch_server_pids": epoch_server_pids,
		"restored_value": restored_value, "persistent_value": persistent_value, "input_clients": input_clients.size(),
		"stall_enabled": stall_enabled, "stall_proof": stall_proof, "stall_proofs": stall_proofs, "peer_generations": generations, "input_generations": input_generations,
		"server_stall_enabled": server_stall_enabled, "server_stall_proof": server_stall_proof, "epoch_owners": epoch_owners}))
	get_tree().quit(0 if passed else 1)
