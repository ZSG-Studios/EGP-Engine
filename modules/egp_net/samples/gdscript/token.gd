extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
var server: Node
var client: Node
var entity := 0
var started := Time.get_ticks_msec()
var finished := false
var phase := 0

func _ready() -> void:
	server = Net.new()
	client = Net.new()
	for net in [server, client]:
		net.auto_poll = false
		add_child(net)
		if net.configure({"game_protocol": "secure-fixture-v1"}) != OK:
			finish(false, "secure configuration")
			return
	if server.host(0) != OK:
		finish(false, "wildcard listener")
		return
	if client.join("127.0.0.1", server.get_statistics().local_port) != ERR_UNAUTHORIZED:
		finish(false, "direct join must require explicit development mode")
		return
	var admission: Dictionary = server.issue_token(1234, "127.0.0.1:" + str(server.get_statistics().local_port))
	if admission.get("error", FAILED) != OK or admission.token.size() != 2048 or client.join_token(1234, admission.token) != OK:
		finish(false, "token issuance and join")
		return
	entity = server.spawn(1, {"authenticated": true})
	if entity == 0 or server.spawn(1, {"bad": Vector3(NAN, 0, 0)}) != 0:
		finish(false, "bounded finite scene state")
		return

func _process(_delta: float) -> void:
	if finished:
		return
	if Time.get_ticks_msec() - started > 8000:
		finish(false, "secure network watchdog")
		return
	if server.poll() != OK or client.poll() != OK:
		finish(false, "secure native pump")
		return
	if phase == 0 and client.get_state() == "Connected" and not client.get_entity(entity).is_empty():
		if server.get_peers().size() != 1 or server.get_peers()[0].client_id != 1234 or not client.get_entity(entity).state.authenticated:
			finish(false, "secure token identity and baseline")
			return
		if client.session.send_application(0, PackedByteArray([1, 2, 3])) != OK:
			finish(false, "malformed fixture enqueue")
			return
		phase = 1
	elif phase == 1 and server.get_peers().is_empty() and client.get_state() == "Disconnected":
		finish(true, "GDScript secure identity, authoritative baseline and malformed-peer quarantine")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	for net in [server, client]:
		if net != null:
			net.close()
	print("EGP_NETWORK_TOKEN " + JSON.stringify({"passed": passed, "message": message}))
	get_tree().quit(0 if passed else 1)
