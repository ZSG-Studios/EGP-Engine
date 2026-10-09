extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
const CLIENT_ID := 9876
var net: Node
var role := ""
var admission := ""
var started := Time.get_ticks_msec()
var sent := false
var replied := false
var saw_entity := false
var drain_frames := 0
var finished := false

func _ready() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--role="):
			role = argument.trim_prefix("--role=")
		elif argument.begins_with("--admission="):
			admission = argument.trim_prefix("--admission=")
	if role not in ["server", "client"] or admission.is_empty():
		finish(false, "missing process fixture arguments")
		return
	net = Net.new()
	net.auto_poll = false
	add_child(net)
	if net.configure({"game_protocol": "egp-process-fixture-v1", "max_players": 2, "max_entities": 2}) != OK:
		finish(false, "process configuration")
		return
	if role == "server":
		net.register_message(&"hello", func(peer: int, arguments: Array):
			var peers: Array = net.get_peers()
			if peers.size() != 1 or peers[0].client_id != CLIENT_ID or peers[0].peer_id != peer or arguments != [77]:
				finish(false, "authenticated process identity")
				return
			if net.send_message(peer, &"reply", [88]) != OK:
				finish(false, "process reply queue")
				return
			drain_frames = 1, Net.Sender.CLIENT)
		net.peer_connected.connect(func(peer: int):
			if net.spawn(1, {"stamp": 55, "position": Vector3(1, 2, 3)}, peer) == 0:
				finish(false, "process entity spawn"))
		if net.host(0, "127.0.0.1") != OK:
			finish(false, "secure process listener")
			return
		var issued: Dictionary = net.issue_token(CLIENT_ID, "127.0.0.1:%d" % net.get_statistics().local_port)
		if issued.error != OK or issued.token.size() != 2048:
			finish(false, "process admission issuance")
			return
		# Test-only trusted local handoff. This is not an account service.
		var file := FileAccess.open(admission, FileAccess.WRITE)
		if file == null:
			finish(false, "process admission handoff")
			return
		file.store_buffer(issued.token)
		file.close()
	else:
		net.register_message(&"reply", func(_peer: int, arguments: Array):
			replied = arguments == [88], Net.Sender.SERVER)
		var token := FileAccess.get_file_as_bytes(admission)
		if token.size() != 2048 or net.join_token(CLIENT_ID, token) != OK:
			finish(false, "secure process client")

func _process(_delta: float) -> void:
	if finished:
		return
	if Time.get_ticks_msec() - started > 10000:
		finish(false, "process fixture watchdog")
		return
	if net.poll() != OK:
		finish(false, "process polling")
		return
	if role == "server":
		if drain_frames > 0:
			drain_frames += 1
			if drain_frames >= 15:
				finish(true, "authenticated identity, replicated entity and message exchange")
	else:
		var entities: Array = net.get_entities()
		if entities.size() == 1:
			var record: Dictionary = net.get_entity(entities[0])
			saw_entity = record.state.get("stamp") == 55 and record.state.get("position") == Vector3(1, 2, 3)
		if saw_entity and not sent:
			if net.send_message(0, &"hello", [77]) != OK:
				finish(false, "process request queue")
				return
			sent = true
		if saw_entity and replied:
			finish(true, "encrypted connection, authoritative baseline and server reply")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	if net != null:
		net.close()
	print("EGP_NETWORK_PROCESS " + JSON.stringify({"passed": passed, "role": role, "message": message}))
	get_tree().quit(0 if passed else 1)
