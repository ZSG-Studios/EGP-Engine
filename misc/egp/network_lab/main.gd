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
	if not configure():
		return
	if role in ["server", "host"]:
		net.register_message(&"hello", receive_hello, Net.Sender.CLIENT)
		if net.host(int(options.get("port", 0)), "127.0.0.1") != OK:
			finish(false, "loopback listener failed")
			return
		entity = net.spawn(1, {"tick": 0, "host_player": role == "host"})
		if entity == 0:
			finish(false, "authority entity failed")
			return
		net.simulation_tick.connect(func(tick: int, server: bool):
			if server and net.update_entity(entity, {"tick": tick, "host_player": role == "host"}) != OK:
				finish(false, "authority update failed"))
		var port: int = net.get_statistics().local_port
		for client in range(clients):
			if not publish_token(client, ""):
				return
		var ready := FileAccess.open(directory.path_join("ready.json"), FileAccess.WRITE)
		if ready == null:
			finish(false, "readiness handoff failed")
			return
		ready.store_string(JSON.stringify({"port": port, "clients": clients}))
		ready.close()
	else:
		net.register_message(&"reply", receive_reply, Net.Sender.SERVER)
		join()

func configure() -> bool:
	var error: Error = net.configure({
		"game_protocol": "egp-network-lab-v1", "max_players": clients,
		"max_entities": 4, "timeout_seconds": 10, "token_lifetime_seconds": 120,
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
			if net.send_message(peer, &"reply", [arguments[0]]) != OK:
				finish(false, "reply enqueue failed")
			return
	finish(false, "unknown authenticated peer")

func receive_reply(_peer: int, arguments: Array) -> void:
	if arguments != [index]:
		finish(false, "reply contract mismatch")
		return
	replies += 1

func _process(_delta: float) -> void:
	if finished or net == null:
		return
	var elapsed := (Time.get_ticks_msec() - started) / 1000.0
	if role == "client" and reconnect_at > 0 and elapsed >= reconnect_at and not reconnected:
		net.close()
		reconnected = true
		reconnect_wait = true
		reconnect_started = Time.get_ticks_msec()
		sent = false
		# A fresh token is required for the new socket: reusing admission would
		# trip netcode's replay protection. The lab backend refreshes it locally.
		var request := FileAccess.open(directory.path_join("reconnect-%d.request" % index), FileAccess.WRITE)
		if request == null:
			finish(false, "admission refresh request failed")
			return
		request.close()
	if reconnect_wait:
		if Time.get_ticks_msec() - reconnect_started > 5000:
			finish(false, "admission refresh watchdog")
			return
		if not FileAccess.file_exists(directory.path_join("client-%d-reconnect.bin" % index)) or Time.get_ticks_msec() - reconnect_started < 500:
			return
		reconnect_wait = false
		if not configure():
			return
		net.register_message(&"reply", receive_reply, Net.Sender.SERVER)
		join("-reconnect")
		if finished:
			return
	if net.poll() != OK:
		finish(false, "poll failed")
		return
	if role != "client" and reconnect_at > 0:
		for client in range(clients):
			if FileAccess.file_exists(directory.path_join("reconnect-%d.request" % client)) and not FileAccess.file_exists(directory.path_join("client-%d-reconnect.bin" % client)):
				if not publish_token(client, "-reconnect"):
					return
	if role == "client":
		for handle in net.get_entities():
			var state: Dictionary = net.get_entity(handle).state
			highest_tick = maxi(highest_tick, int(state.get("tick", 0)))
		if net.get_state() == "Connected" and not sent:
			if net.send_message(0, &"hello", [index]) != OK:
				finish(false, "hello enqueue failed")
				return
			sent = true
	if label != null:
		label.text = "EGP Network Lab\n%s %d · %s\n%.1f / %.1f seconds\nPeers: %d · server tick: %d\nLatency: %s ms · jitter: %s ms · loss: %s%%\nReplies: %d · diagnostics: %d" % [role, index, net.get_state(), elapsed, duration, net.get_peers().size(), highest_tick, options.get("latency", "0"), options.get("jitter", "0"), options.get("loss", "0"), replies, diagnostics.size()]
	if elapsed >= duration + (2.0 if role != "client" else 0.0):
		if role == "client":
			finish(replies >= (2 if reconnect_at > 0 else 1) and highest_tick > 1, "encrypted admission, replies and replicated tick")
		else:
			var passed := admissions.size() == clients
			if reconnect_at > 0:
				for client in range(clients):
					passed = passed and generations.get(client, {}).size() >= 2
			finish(passed, "authenticated clients and connection generations")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	var statistics: Dictionary = net.get_statistics() if net != null else {}
	if net != null:
		net.close()
	print("EGP_NETWORK_LAB " + JSON.stringify({"passed": passed, "role": role, "index": index, "message": message, "replies": replies, "highest_tick": highest_tick, "admitted_clients": admissions.size(), "diagnostics": diagnostics, "statistics": statistics}))
	get_tree().quit(0 if passed else 1)
