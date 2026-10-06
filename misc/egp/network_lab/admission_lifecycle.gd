extends Node
## Bounded local admission test. Tokens and test keys never leave process memory.
const Net = preload("res://addons/egp_net/egp_net.gd")
const LIFETIME := 120
var server: Object
var client: Object
var facade := false
var diagnostics: Array[String] = []
var states: Array[String] = []
var proof: Dictionary = {}

func check(condition: bool, message: String) -> bool:
	if condition:
		return true
	finish(false, message)
	return false

func native_session(session: Object) -> Object:
	return session.session if facade else session

func listen(port: int) -> Error:
	return server.host(port, "127.0.0.1") if facade else server.listen(port, "127.0.0.1")

func peers() -> Array:
	return native_session(server).command("peers")

func entities() -> Array:
	return native_session(server).command("entities")

func _ready() -> void:
	var args: Dictionary = {}
	for argument in OS.get_cmdline_user_args():
		var pair := argument.split("=", true, 1)
		if pair.size() == 2:
			args[pair[0]] = pair[1]
	var api: String = args.get("api", "native")
	var key_mode: String = args.get("key", "generated")
	var boundary: String = args.get("boundary", "same")
	var fault: String = args.get("fault", "clock")
	proof = {"api": api, "key": key_mode, "boundary": boundary, "fault": fault, "pid": OS.get_process_id()}
	if not check(api in ["native", "gdscript"] and key_mode in ["zero", "nonzero", "generated"] and boundary in ["same", "cross"] and fault in ["clock", "graceful"], "invalid fixture options"):
		return
	facade = api == "gdscript"
	server = Net.new() if facade else ClassDB.instantiate("EGPNetSession")
	client = Net.new() if facade else ClassDB.instantiate("EGPNetSession")
	if facade:
		server.auto_poll = false
		client.auto_poll = false
		add_child(server)
		add_child(client)
	var config := {"game_protocol": "egp-admission-lifecycle-v1", "max_players": 1, "timeout_seconds": 3, "token_lifetime_seconds": LIFETIME}
	if key_mode != "generated":
		var key := PackedByteArray()
		key.resize(32)
		key.fill(0 if key_mode == "zero" else 7)
		config.private_key = key
	if not check(server.configure(config) == OK and client.configure({"game_protocol": "egp-admission-lifecycle-v1", "timeout_seconds": 3, "token_lifetime_seconds": LIFETIME}) == OK, "configuration failed"):
		return
	server.diagnostic.connect(func(message: String): diagnostics.append(message))
	client.state_changed.connect(func(state: String): states.append(state))
	if not check(listen(0) == OK, "listener failed"):
		return
	var port: int = server.get_statistics().local_port
	# Begin token issuance just after a UTC second boundary, polling while waiting.
	var previous_second := int(Time.get_unix_time_from_system())
	var wait_started := Time.get_ticks_msec()
	while int(Time.get_unix_time_from_system()) == previous_second:
		if not check(server.poll() == OK and Time.get_ticks_msec() - wait_started < 2000, "issuance boundary watchdog"):
			return
		OS.delay_msec(2)
	var issued: Dictionary = server.issue_token(900000, "127.0.0.1:%d" % port)
	if not check(issued.error == OK and issued.token.size() == 2048, "issuance failed"):
		return
	# netcode public header: 13 version bytes, 8 protocol bytes, creation/expiry u64.
	proof.token_created_utc = int(issued.token.decode_u64(21))
	proof.token_expires_utc = int(issued.token.decode_u64(29))
	var before := Time.get_ticks_msec()
	OS.delay_msec(550 if boundary == "same" else 1100)
	proof.gap_ms = Time.get_ticks_msec() - before
	if fault == "clock":
		proof.poll_error = server.poll()
		if not check(proof.poll_error == FAILED and diagnostics == ["Fixed simulation exceeded its catch-up budget; resynchronization required."], "clock fault did not fail closed"):
			return
	else:
		server.stop()
		proof.poll_error = OK
	if not check(server.get_state() == "Stopped" and peers().is_empty() and entities().is_empty() and server.get_statistics().tick == 0, "authority not cleared"):
		return
	proof.restart_before_utc = int(Time.get_unix_time_from_system())
	if not check(listen(port) == OK, "rebind failed"):
		return
	proof.restart_after_utc = int(Time.get_unix_time_from_system())
	proof.peers_before_join = peers().size()
	proof.entities_before_join = entities().size()
	# No retries or inferred success when OS scheduling misses the requested boundary.
	if not check(proof.restart_before_utc == proof.restart_after_utc and (proof.token_created_utc == proof.restart_after_utc if boundary == "same" else proof.token_created_utc < proof.restart_before_utc), "requested timestamp boundary missed"):
		return
	var joined: Error = client.join_token(900000, issued.token) if facade else client.connect_token(900000, issued.token)
	issued.token.clear()
	if not check(joined == OK, "token join failed"):
		return
	var expected: bool = key_mode != "generated" and boundary == "same"
	var started := Time.get_ticks_msec()
	while client.get_state() not in ["Connected", "Disconnected"] and Time.get_ticks_msec() - started < 4500:
		if not check(server.poll() == OK and client.poll() == OK, "admission poll failed"):
			return
		OS.delay_msec(2)
	proof.expected_connected = expected
	proof.final_state = client.get_state()
	proof.peers_before_close = peers().size()
	proof.token_lifetime_seconds = LIFETIME
	proof.fingerprint = native_session(server).get_fingerprint()
	finish((proof.final_state == "Connected" and proof.peers_before_close == 1) if expected else (proof.final_state == "Disconnected" and proof.peers_before_close == 0 and not "Synchronizing" in states), "retained-key admission depends on restart timestamp; generated key rotates")

func finish(passed: bool, message: String) -> void:
	proof.passed = passed
	proof.message = message
	proof.diagnostics = diagnostics
	proof.states = states
	if client != null:
		client.close()
	if server != null:
		server.close()
	print("EGP_ADMISSION_LIFECYCLE " + JSON.stringify(proof))
	get_tree().quit(0 if passed else 1)
