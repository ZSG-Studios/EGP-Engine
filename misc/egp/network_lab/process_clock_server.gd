extends Node
## Isolated test authority. Token files are a trusted local fixture handoff.
const Net = preload("res://addons/egp_net/egp_net.gd")
const Physics = preload("res://addons/egp_net/egp_net_box3d.gd")
var net: Node
var adapter: RefCounted
var world: RefCounted
var handoff := ""
var epoch := 1
var entity := 0
var peer_id := 0
var checkpoint_tick := 0
var inputs := 0
var ready_tick := 0
var ack := false
var drain := 0
var done := false
var started := Time.get_ticks_msec()
var diagnostics: Array[String] = []
var epochs: Array = []
var recoveries: Array = []

func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--handoff="):
			handoff = arg.trim_prefix("--handoff=")
	if handoff.is_empty():
		finish(false, "missing trusted fixture directory")
		return
	world = ClassDB.instantiate("EGPBox3DWorld")
	if world.configure(60, 4, 1, Vector3(0, -9.8, 0)) != OK or world.queue_create_sphere(10000, 1, Vector3(0, 10, 0), 0.5) != OK:
		finish(false, "physics configuration")
		return
	net = Net.new()
	net.auto_poll = false
	add_child(net)
	var fingerprint: String = world.get_simulation_fingerprint()
	if net.configure({"simulation_fingerprint": fingerprint, "max_players": 1, "max_entities": 2}) != OK:
		finish(false, "authority configuration")
		return
	net.diagnostic.connect(func(message: String): diagnostics.append(message))
	net.peer_connected.connect(func(peer: int):
		peer_id = peer
		entity = net.spawn(1, {"epoch": epoch}, peer)
		if entity == 0 or adapter.track(entity, 10000) != OK:
			finish(false, "owned stable physics body"))
	net.input_received.connect(func(peer: int, handle: int, input: Dictionary):
		if peer != peer_id or handle != entity or input != {"epoch": epoch} or inputs != epoch - 1:
			finish(false, "invalid or duplicate owner input")
		else:
			inputs += 1)
	net.register_message(&"ready", func(peer: int, args: Array):
		var peers: Array = net.get_peers()
		if peer != peer_id or peers.size() != 1 or peers[0].client_id != 9876 or args.size() != 3 or args[0] != epoch or args[1] != entity or args[2] <= checkpoint_tick:
			finish(false, "authenticated restored client baseline")
			return
		ready_tick = args[2]
		if net.send_message(peer, &"reply", [epoch]) != OK:
			finish(false, "ready reply"), Net.Sender.CLIENT)
	net.register_message(&"ack", func(peer: int, args: Array):
		if peer != peer_id or args != [epoch] or ack:
			finish(false, "reply acknowledgement")
		else:
			ack = true, Net.Sender.CLIENT)
	adapter = Physics.new()
	if net.host(0, "127.0.0.1") != OK or adapter.attach(net, world) != OK:
		finish(false, "authority listener/clock")
		return
	var file := FileAccess.open(handoff.path_join("profile.txt"), FileAccess.WRITE)
	if file == null:
		finish(false, "profile handoff")
		return
	file.store_string(fingerprint)
	file.close()
	issue()

func issue() -> void:
	var token: Dictionary = net.issue_token(9876, "127.0.0.1:%d" % net.get_statistics().local_port)
	var file := FileAccess.open(handoff.path_join("epoch-%d.bin" % epoch), FileAccess.WRITE)
	if token.error != OK or token.token.size() != 2048 or file == null:
		finish(false, "fresh admission handoff")
		return
	file.store_buffer(token.token)
	file.close()

func _process(_delta: float) -> void:
	if done:
		return
	if Time.get_ticks_msec() - started > 30000 or net.poll() != OK:
		finish(false, "authority watchdog/poll")
		return
	if not ack or inputs != epoch or ready_tick <= checkpoint_tick:
		return
	if drain == 0:
		epochs.append({"epoch": epoch, "entity": entity, "peer_id": peer_id, "client_id": 9876, "client_physics_tick": ready_tick, "world_tick": world.get_tick(), "inputs": inputs})
	drain += 1
	if epoch == 4:
		if drain >= 25:
			finish(true, "three independent authority faults and four authenticated physics baselines")
		return
	if drain < 8:
		return
	var retired := entity
	var native_id: int = net.session.get_instance_id()
	var port: int = net.get_statistics().local_port
	var snapshot: PackedByteArray = world.capture_snapshot()
	checkpoint_tick = world.get_tick()
	var state_hash: String = world.get_state_hash()
	var start_utc := int(Time.get_unix_time_from_system() * 1000)
	var before := Time.get_ticks_msec()
	OS.delay_msec(550)
	var error: Error = net.poll()
	var end_utc := int(Time.get_unix_time_from_system() * 1000)
	var gap := Time.get_ticks_msec() - before
	if error != FAILED or net.get_state() != "Stopped" or not net.get_entities().is_empty() or not net.get_peers().is_empty() or net.get_statistics().tick != 0:
		finish(false, "clock failure must clear authority")
		return
	adapter.detach()
	if world.step_tick(checkpoint_tick + 1) != OK or world.restore_snapshot(snapshot) != OK or world.get_tick() != checkpoint_tick or world.get_state_hash() != state_hash:
		finish(false, "trusted checkpoint restoration")
		return
	if net.host(port, "127.0.0.1") != OK or net.session.get_instance_id() != native_id or not net.get_entity(retired).is_empty() or net.update_entity(retired, {}) != ERR_DOES_NOT_EXIST or adapter.attach(net, world) != OK:
		finish(false, "retained authority/retired handle/clock rebind")
		return
	recoveries.append({"cycle": epoch, "old_entity": retired, "checkpoint_tick": checkpoint_tick, "checkpoint_hash": state_hash, "start_utc_ms": start_utc, "end_utc_ms": end_utc, "gap_ms": gap, "poll_error": error, "cleared": true, "same_session": true})
	epoch += 1
	ack = false
	ready_tick = 0
	drain = 0
	issue()

func finish(passed: bool, message: String) -> void:
	if done:
		return
	done = true
	if adapter != null:
		adapter.detach()
	if net != null:
		net.close()
	print("EGP_CLOCK_PROCESS " + JSON.stringify({"passed": passed, "role": "server", "pid": OS.get_process_id(), "epochs": epochs, "recoveries": recoveries, "diagnostics": diagnostics, "message": message}))
	get_tree().quit(0 if passed else 1)
