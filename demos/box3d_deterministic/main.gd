extends Node3D
const Net = preload("res://addons/egp_net/egp_net.gd")
const Replay = preload("res://addons/egp_net/egp_net_deterministic_replay.gd")
const FRAME_BYTES := 32
const WINDOW := 24
const JOURNAL_TICKS := 240
var net: Node
var world: RefCounted
var replay: RefCounted
var role := "client"
var player := 1
var runtime := ""
var duration := 0.0
var seconds := 0.0
var latency := 120.0
var jitter := 40.0
var loss := 5.0
var ready_peers: Dictionary = {}
var owners: Dictionary = {}
var ack_ticks: Dictionary = {}
var sequences: Dictionary = {}
var last_input_msec: Dictionary = {}
var latest := PackedByteArray()
var journal: Dictionary = {}
var predictions := 0
var corrections := 0
var replayed_ticks := 0
var verified_ticks := 0
var hash_failures := 0
var history_peak := 0
var sequence := 0
var input_timer := 0.0
var report_timer := 0.0
var ready_sent := false
var canonical_tick := 0
var canonical_arrival := 0
var started := false
var frame_bytes_sent := 0
var bodies: Dictionary = {}
var visual_offsets: Dictionary = {}
var label: Label
var bot := true
var last_local := PackedByteArray()
var render_enabled := false
var fault := ""
var fault_injected := false
var capture_path := ""
var captured := false

func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts := arg.split("=", true, 1)
		if parts.size() != 2: continue
		match parts[0]:
			"--role": role = parts[1]
			"--player": player = int(parts[1])
			"--runtime": runtime = parts[1]
			"--duration": duration = float(parts[1])
			"--latency": latency = float(parts[1])
			"--jitter": jitter = float(parts[1])
			"--loss": loss = float(parts[1])
			"--fault": fault = parts[1]
			"--capture": capture_path = parts[1]
	if runtime.is_empty() or player not in [1, 2]: fail("Invalid local launch configuration"); return
	render_enabled = DisplayServer.get_name() != "headless"
	world = ClassDB.instantiate("EGPBox3DWorld")
	if world.configure(60, 4, 1, Vector3(0, -9.8, 0)) != OK or genesis() != OK: fail("Deterministic genesis failed"); return
	latest.resize(12)
	last_local.resize(6)
	net = Net.new()
	net.auto_poll = false
	add_child(net)
	if net.configure({"game_protocol": "egp-deterministic-input-v1", "simulation_fingerprint": world.get_simulation_fingerprint(), "tick_rate": 60, "max_players": 2, "simulated_latency_ms": latency, "simulated_jitter_ms": jitter, "simulated_loss": loss}) != OK: fail("Native networking configuration failed"); return
	net.packet_received.connect(receive_packet)
	net.simulation_tick.connect(server_tick)
	if role == "server":
		net.register_message(&"ready", accept_ready, Net.Sender.CLIENT)
		net.peer_disconnected.connect(func(peer: int): ready_peers.erase(peer); owners.erase(peer); ack_ticks.erase(peer))
		if net.host(0, "127.0.0.1") != OK: fail("Dedicated listener failed"); return
		for index in [1, 2]:
			var admission: Dictionary = net.issue_token(500 + index, "127.0.0.1:%d" % net.get_statistics().local_port)
			var token: PackedByteArray = admission.get("token", PackedByteArray())
			var file := FileAccess.open(runtime.path_join("player%d.token" % index), FileAccess.WRITE)
			if file == null or token.size() != 2048: fail("Local admission failed"); return
			file.store_buffer(token)
	else:
		replay = Replay.new()
		if replay.configure(world.capture_snapshot, world.restore_snapshot, simulate, world.get_state_hash) != OK: fail("Local solver history setup failed"); return
		replay.corrected.connect(func(_first: int, ticks: int): corrections += 1; replayed_ticks += ticks)
		replay.resync_required.connect(func(_error: Error): hash_failures += 1)
		if net.join_token(500 + player, FileAccess.get_file_as_bytes(runtime.path_join("player%d.token" % player)), "127.0.0.%d" % (player + 1)) != OK: fail("Secure admission failed"); return
		if render_enabled: build_scene()

func genesis() -> Error:
	var errors: Array = []
	errors.append(world.queue_create_box(1, 1, Vector3(0, -0.5, 0), Vector3(10, 0.5, 8), 0))
	for side in [-1, 1]:
		errors.append(world.queue_create_box(2 if side == -1 else 3, 1, Vector3(side * 10, 1, 0), Vector3(0.25, 1.5, 8), 0))
		errors.append(world.queue_create_box(4 if side == -1 else 5, 1, Vector3(0, 1, side * 8), Vector3(10, 1.5, 0.25), 0))
	for index in [1, 2]: errors.append(world.queue_create_sphere(100 + index, 1, Vector3(-3 if index == 1 else 3, 1, 2), 0.6))
	for index in range(12):
		var position := Vector3((index % 3 - 1) * 1.05, 0.6 + int(index / 3) * 1.05, -1)
		errors.append(world.queue_create_box(200 + index, 1, position, Vector3.ONE * 0.5))
	errors.append(world.queue_create_capsule(220, 1, Vector3(-4, 1, -3), 0.4, 0.5))
	errors.append(world.queue_create_sphere(221, 1, Vector3(4, 1, -3), 0.6))
	errors.append(world.queue_create_box(300, 1, Vector3(0, 0.4, -4), Vector3(2, 0.3, 1.5), 1))
	for error in errors:
		if error != OK: return error
	return world.apply_queued_commands()

func accept_ready(peer: int, args: Array) -> void:
	if args.size() != 1 or not args[0] is String or args[0] != world.get_state_hash() or started: return
	for record in net.get_peers():
		if record.peer_id == peer and record.client_id in [501, 502]:
			if owners.values().has(record.client_id - 500): net.disconnect_peer(peer); return
			owners[peer] = record.client_id - 500
			ready_peers[peer] = true
			ack_ticks[peer] = 0
			last_input_msec[peer] = Time.get_ticks_msec()

func server_tick(_network_tick: int, server: bool) -> void:
	if not server or ready_peers.size() != 2: return
	started = true
	var tick: int = world.get_tick() + 1
	for peer in owners:
		if Time.get_ticks_msec() - int(last_input_msec[peer]) > 300:
			for byte in range(6): latest[(int(owners[peer]) - 1) * 6 + byte] = 0
	var input := latest.duplicate()
	if simulate(tick, input, false) != OK: fail("Authority fixed step failed"); return
	journal[tick] = {"tick": tick, "input": input, "hash": world.get_state_hash()}
	journal.erase(tick - JOURNAL_TICKS)
	if tick % 3 != 0: return
	for peer in ready_peers:
		var first: int = int(ack_ticks[peer]) + 1
		if first < tick - JOURNAL_TICKS + 1: fail("Client exceeded bounded canonical journal; fresh session required"); return
		var count := mini(WINDOW, tick - first + 1)
		if count < 1: continue
		var packet := PackedByteArray()
		packet.resize(12 + count * FRAME_BYTES)
		packet.encode_u32(0, 0x44455431)
		packet.encode_u32(4, tick)
		packet.encode_u32(8, count)
		for index in range(count):
			var frame: Dictionary = journal[first + index]
			var offset := 12 + index * FRAME_BYTES
			packet.encode_u32(offset, frame.tick)
			for byte in range(12): packet[offset + 4 + byte] = frame.input[byte]
			var digest: PackedByteArray = frame.hash.to_ascii_buffer()
			for byte in range(16): packet[offset + 16 + byte] = digest[byte]
		var error: Error = net.send_packet(peer, packet, 2, Net.Delivery.UNRELIABLE)
		if error == OK: frame_bytes_sent += packet.size()
		elif error != ERR_BUSY: fail("Canonical input transmission failed")

func simulate(tick: int, input: PackedByteArray, _replay: bool) -> Error:
	if input.size() != 12 or world.get_tick() + 1 != tick: return ERR_INVALID_DATA
	var phase := tick / 60.0
	var result: Error = world.queue_body_state(300, tick, Vector3(sin(phase) * 3, 0.5 + (sin(phase * 0.5) + 1) * 0.8, -4), Quaternion.IDENTITY, Vector3(cos(phase) * 3, cos(phase * 0.5) * 0.4, 0), Vector3.ZERO)
	if result != OK: return result
	for index in [0, 1]:
		var x := input.decode_s16(index * 6)
		var z := input.decode_s16(index * 6 + 2)
		var flags := input.decode_u16(index * 6 + 4)
		if absi(x) > 1000 or absi(z) > 1000 or flags > 3: world.clear_pending_commands(); return ERR_INVALID_DATA
		var body: Dictionary = world.get_body_state(101 + index)
		var direction := Vector3(x / 1000.0, 0, z / 1000.0).limit_length()
		var impulse: Vector3 = (direction * 5.5 - Vector3(body.linear_velocity.x, 0, body.linear_velocity.z)) * 0.12
		if flags & 1 and body.position.y < 0.85: impulse.y = 6
		result = world.queue_impulse(101 + index, tick, impulse)
		if result != OK: world.clear_pending_commands(); return result
		if flags & 2 and tick % 30 == 0:
			for id in range(200, 212):
				var other: Dictionary = world.get_body_state(id)
				var offset: Vector3 = other.position - body.position
				if offset.length() < 5:
					result = world.queue_impulse(id, tick * 2 + index, offset.normalized() * 3 + Vector3(0, 2, 0))
					if result != OK: world.clear_pending_commands(); return result
	for id in [101, 102, 220, 221] + range(200, 212):
		var body: Dictionary = world.get_body_state(id)
		if body.position.y < -5 or absf(body.position.x) > 12 or absf(body.position.z) > 10:
			result = world.queue_body_state(id, tick * 3 + 1000, Vector3((id % 7 - 3) * 1.2, 3, 0), Quaternion.IDENTITY, Vector3.ZERO, Vector3.ZERO)
			if result != OK: world.clear_pending_commands(); return result
	return world.step_tick(tick)

func receive_packet(peer: int, packet: PackedByteArray, channel: int, delivery: int) -> void:
	if delivery != Net.Delivery.UNRELIABLE: return
	if role == "server":
		if channel != 1 or packet.size() != 20 or packet.decode_u32(0) != 0x494e5031 or not owners.has(peer): return
		var stamp := packet.decode_u32(4)
		var ack := packet.decode_u32(16)
		if stamp <= int(sequences.get(peer, 0)) or ack > world.get_tick(): return
		if absi(packet.decode_s16(8)) > 1000 or absi(packet.decode_s16(10)) > 1000 or packet.decode_u32(12) > 3: return
		sequences[peer] = stamp
		ack_ticks[peer] = maxi(int(ack_ticks[peer]), ack)
		last_input_msec[peer] = Time.get_ticks_msec()
		for byte in range(6): latest[(int(owners[peer]) - 1) * 6 + byte] = packet[8 + byte]
	else:
		if peer != 0 or channel != 2 or packet.size() < 12 or packet.decode_u32(0) != 0x44455431: return
		var count := packet.decode_u32(8)
		if count < 1 or count > WINDOW or packet.size() != 12 + count * FRAME_BYTES: return
		var frames: Array = []
		for index in range(count):
			var offset := 12 + index * FRAME_BYTES
			frames.append({"tick": packet.decode_u32(offset), "input": packet.slice(offset + 4, offset + 16), "hash": packet.slice(offset + 16, offset + 32).get_string_from_ascii()})
		var before: int = replay.get_acknowledged_tick()
		# A duplicated/reordered window entirely behind the acknowledged tick is harmless.
		if frames.back().tick <= before: return
		if frames[0].tick > before + 1: return
		if fault == "hash" and player == 2 and not fault_injected and before > 120:
			frames.back().hash = "0000000000000000"
			fault_injected = true
		var before_positions: Dictionary = {}
		if render_enabled:
			for id in bodies: before_positions[id] = world.get_body_state(id).position
		var result: Error = replay.accept(frames)
		if result != OK: fail("Deterministic replay/hash verification failed: %d" % result); return
		# Smooth only presentation corrections. Exact solver poses and hashes stay untouched.
		for id in before_positions:
			visual_offsets[id] = visual_offsets.get(id, Vector3.ZERO) + before_positions[id] - world.get_body_state(id).position
		verified_ticks += replay.get_acknowledged_tick() - before
		latest = frames.back().input.duplicate()
		canonical_tick = packet.decode_u32(4)
		canonical_arrival = Time.get_ticks_msec()
		started = true

func local_input() -> PackedByteArray:
	var direction := Vector2(Input.get_axis("ui_left", "ui_right"), Input.get_axis("ui_up", "ui_down"))
	var jump := Input.is_physical_key_pressed(KEY_SPACE)
	var pulse := Input.is_physical_key_pressed(KEY_Q)
	if direction != Vector2.ZERO or jump or pulse: bot = false
	if bot:
		var p: Vector3 = world.get_body_state(100 + player).position
		var goal := Vector3(sin(seconds * 0.75 + player * 2) * 6, 0, cos(seconds * 0.8 + player) * 4)
		direction = Vector2(goal.x - p.x, goal.z - p.z).limit_length()
		jump = fmod(seconds + player * 0.2, 3) < 0.16
		pulse = fmod(seconds + player * 0.3, 2) < 0.6
	var result := PackedByteArray()
	result.resize(6)
	result.encode_s16(0, roundi(direction.x * 1000))
	result.encode_s16(2, roundi(direction.y * 1000))
	result.encode_u16(4, int(jump) | (int(pulse) << 1))
	return result

func _process(delta: float) -> void:
	seconds += delta
	if net == null: return
	if net.poll() != OK: fail("Native fixed-clock watchdog failed"); return
	if role == "client" and net.get_state() == "Connected":
		if not ready_sent:
			ready_sent = net.send_message(0, &"ready", [world.get_state_hash()]) == OK
		input_timer += delta
		last_local = local_input()
		if input_timer >= 0.05:
			input_timer = minf(input_timer - 0.05, 0.05)
			sequence += 1
			var packet := PackedByteArray()
			packet.resize(20)
			packet.encode_u32(0, 0x494e5031)
			packet.encode_u32(4, sequence)
			for byte in range(6): packet[8 + byte] = last_local[byte]
			packet.encode_u32(16, replay.get_acknowledged_tick())
			net.send_packet(0, packet, 1, Net.Delivery.UNRELIABLE)
		if started:
			var target := canonical_tick + int((Time.get_ticks_msec() - canonical_arrival + latency) * 0.06) + 2
			for _step in range(mini(4, maxi(0, target - replay.get_tick()))):
				var input := latest.duplicate()
				for byte in range(6): input[(player - 1) * 6 + byte] = last_local[byte]
				var result: Error = replay.predict(replay.get_tick() + 1, input)
				if result != OK: fail("Prediction history or fixed step failed: %d" % result); return
				predictions += 1
			history_peak = maxi(history_peak, replay.get_history_bytes())
		if render_enabled:
			for id in bodies:
				var state: Dictionary = world.get_body_state(id)
				var offset: Vector3 = visual_offsets.get(id, Vector3.ZERO) * exp(-delta * 14)
				if offset.length() > 4: offset = Vector3.ZERO
				visual_offsets[id] = offset
				var current: Quaternion = bodies[id].basis.get_rotation_quaternion()
				bodies[id].transform = Transform3D(Basis(current.slerp(state.rotation, 1.0 - exp(-delta * 18))), state.position + offset)
			label.text = "EGP / DETERMINISTIC NETWORKING\n\nPLAYER %d / %s\n\nLOCAL PREDICTION / 60 Hz\nAUTHORITATIVE INPUT REPLAY\n\nVerified physics ticks: %d\nRollbacks: %d\nReplayed solver ticks: %d\nHash mismatches: %d\nPending ticks: %d\nLocal history: %.2f MiB\n\n%.0f ms each way / %.0f ms jitter\n%.1f%% packet loss\n\nARROWS Move / SPACE Jump\nQ Shockwave / B Toggle AI\n\nNo solver snapshots on the wire\nInputs + diagnostic hashes only" % [player, net.get_state(), verified_ticks, corrections, replayed_ticks, hash_failures, replay.get_pending_ticks(), replay.get_history_bytes() / 1048576.0, latency, jitter, loss]
			if not captured and not capture_path.is_empty() and seconds > 8:
				captured = true
				capture.call_deferred()
	report_timer += delta
	if report_timer > 2:
		report_timer = 0
		report()
	if duration > 0 and seconds >= duration:
		report()
		get_tree().quit()

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_B: bot = not bot

func report() -> void:
	var data := {"role": role, "player": player, "state": net.get_state(), "seconds": seconds, "physics_tick": world.get_tick(), "state_hash": world.get_state_hash(), "predictions": predictions, "corrections": corrections, "replayed_ticks": replayed_ticks, "verified_ticks": verified_ticks, "hash_failures": hash_failures, "history_peak_bytes": history_peak, "input_frame_bytes_sent": frame_bytes_sent, "fps": Engine.get_frames_per_second(), "fault_injected": fault_injected}
	if role == "client":
		data.acknowledged_tick = replay.get_acknowledged_tick()
		data.acknowledged_hash = replay.get_acknowledged_hash()
	else:
		var hashes: Dictionary = {}
		for tick in journal: hashes[str(tick)] = journal[tick].hash
		data.hashes = hashes
	print("DETERMINISTIC_STATUS " + JSON.stringify(data))
	var file := FileAccess.open(runtime.path_join("%s%d-status.json" % [role, player]), FileAccess.WRITE)
	if file != null: file.store_string(JSON.stringify(data))

func build_scene() -> void:
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color("071320")
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_energy = 0.7
	environment.environment = settings
	add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -30, 0)
	light.shadow_enabled = true
	add_child(light)
	for id in [1, 2, 3, 4, 5, 101, 102, 220, 221, 300] + range(200, 212):
		var mesh: Mesh
		if id in [101, 102, 221]:
			var sphere := SphereMesh.new()
			sphere.radius = 0.6
			sphere.height = 1.2
			mesh = sphere
		elif id == 220:
			var capsule := CapsuleMesh.new()
			capsule.radius = 0.4
			capsule.height = 1.8
			mesh = capsule
		else:
			var box := BoxMesh.new()
			box.size = Vector3.ONE
			if id == 1: box.size = Vector3(20, 1, 16)
			elif id in [2, 3]: box.size = Vector3(0.5, 3, 16)
			elif id in [4, 5]: box.size = Vector3(20, 3, 0.5)
			elif id == 300: box.size = Vector3(4, 0.6, 3)
			mesh = box
		var material := StandardMaterial3D.new()
		material.albedo_color = Color("54e4d0") if id == 101 else Color("ffb45c") if id == 102 else Color("758cc0") if id >= 200 else Color("21344a")
		var instance := MeshInstance3D.new()
		instance.mesh = mesh
		instance.material_override = material
		add_child(instance)
		bodies[id] = instance
	var camera := Camera3D.new()
	add_child(camera)
	camera.position = Vector3(18, 22, 25)
	camera.fov = 55
	camera.look_at(Vector3(-2, 0, 0))
	camera.current = true
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var panel := Panel.new()
	panel.position = Vector2(20, 20)
	panel.size = Vector2(310, 590)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.02, 0.04, 0.07, 0.95)
	style.border_width_left = 3
	style.border_color = Color("54e4d0") if player == 1 else Color("ffb45c")
	panel.add_theme_stylebox_override("panel", style)
	canvas.add_child(panel)
	label = Label.new()
	label.position = Vector2(36, 32)
	label.add_theme_font_size_override("font_size", 14)
	canvas.add_child(label)

func capture() -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(capture_path)

func fail(message: String) -> void:
	push_error("DETERMINISTIC_FAILURE " + message)
	report()
	get_tree().quit(1)
