extends Node3D
## Explicit Box3D simulation belongs to the dedicated server. Clients render poses.
const Net = preload("res://addons/egp_net/egp_net.gd")
const COLORS := [Color("54e4d0"), Color("ffb45c"), Color("8094bc")]
var net: Node
var world: RefCounted
var pose_interpolator: RefCounted
var role := "client"
var player := 1
var runtime := ""
var duration := 0.0
var elapsed := 0.0
var bot := true
var entities: Dictionary = {}
var pose_epochs: Dictionary = {}
var visuals: Dictionary = {}
var visual_kinds: Dictionary = {}
var targets: Dictionary = {}
var controls: Dictionary = {}
var last_input: Dictionary = {}
var jump_ticks: Dictionary = {}
var sequence := 100
var input_timer := 0.0
var report_timer := 0.0
var owned := 0
var label: Label
var camera: Camera3D
var max_remote_tick := 0
var received_updates := 0
var movement_distance := 0.0
var last_position := Vector3.ZERO
var has_position := false
var captured := false
var capture_path := ""
var checkpoint := PackedByteArray()
@export var checkpoint_restores: int = 0
var reset_requested := false
var reset_request_entity := 0
var reset_request_msec := 0
var last_reset_msec := -10000
var showcased_reset := false
var latency_ms := 70.0
var jitter_ms := 20.0
var loss_percent := 2.0
var rtt_ms := 0
var ping_timer := 0.0
var ai: Dictionary = {}
var action_times: Dictionary = {}
var prop_count := 0
var next_body := 2000
var pulses := 0
var pulse_events: Array[Vector3] = []
var ai_attacks := 0
var overview := false
var effects: Array[Dictionary] = []
var showcased_spawn := false
var input_sequence := 0
var accepted_sequences: Dictionary = {}
var pickups: Dictionary = {}
var scores := {1: 0, 2: 0}
@export var pickups_collected: int = 0
@export var teal_score: int = 0
@export var amber_score: int = 0
var rescued_bodies := 0
var pose_history: Dictionary = {}
var fast_ticks: Dictionary = {}
var newest_pose_tick := 0
var newest_pose_msec := 0
var render_tick := 0.0
var camera_focus := Vector3.ZERO
var camera_focus_ready := false
var fast_updates := 0
var frame_times: Array[float] = []
var own_pose_gaps: Array[float] = []
var last_own_pose_msec := 0
var recovering := false
var recovery_attempts := 0
var recoveries := 0
var recovery_started := 0
var stall_exercised := false
var largest_frame_gap_ms := 0
var last_poll_msec := 0
var last_native_diagnostic := ""
var last_native_diagnostic_msec := 0
var last_native_statistics: Dictionary = {}
var last_process_msec := 0
var action_requests: Array[Dictionary] = []
var action_nonces: Dictionary = {}
var action_nonce := 0
var outgoing_actions: Array[Array] = []
var stress_count := 32
var mechanisms: Dictionary = {}
var pose_cursor := 0
var physics_times: Array[float] = []
var full_load_physics_times: Array[float] = []
var full_load_physics_p95_worst_ms := 0.0
var max_physics_ms := 0.0
var player_count := 2
var bind_ip := "127.0.0.2"
var brain_state := "acquiring baseline"
var brain_decisions := 0
var brain_target := 0
var brain_progress_time := 0.0
var brain_progress_position := Vector3.ZERO
var brain_escape_until := 0.0
var brain_escapes := 0
var score_dirty := false
var match_state_frozen := false
var peer_entities: Dictionary = {}
var peer_pose_cursors: Dictionary = {}
var max_peers_seen := 0
var full_load_seconds := 0.0

func _ready() -> void:
	pose_interpolator = ClassDB.instantiate("EGPNetSnapshotInterpolator")
	if pose_interpolator == null:
		fail("Native snapshot interpolation backend unavailable")
		return
	pose_interpolator.configure(60.0, 0.2, 0.1, 0.4)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--role="): role = arg.trim_prefix("--role=")
		elif arg.begins_with("--player="): player = int(arg.trim_prefix("--player="))
		elif arg.begins_with("--runtime="): runtime = arg.trim_prefix("--runtime=")
		elif arg.begins_with("--duration="): duration = float(arg.trim_prefix("--duration="))
		elif arg.begins_with("--capture="): capture_path = arg.trim_prefix("--capture=")
		elif arg.begins_with("--latency="): latency_ms = float(arg.trim_prefix("--latency="))
		elif arg.begins_with("--jitter="): jitter_ms = float(arg.trim_prefix("--jitter="))
		elif arg.begins_with("--loss="): loss_percent = float(arg.trim_prefix("--loss="))
		elif arg.begins_with("--stress="): stress_count = clampi(int(arg.trim_prefix("--stress=")), 0, 192)
		elif arg.begins_with("--players="): player_count = clampi(int(arg.trim_prefix("--players=")), 2, 52)
		elif arg.begins_with("--bind="): bind_ip = arg.trim_prefix("--bind=")
	if runtime.is_empty() or player < 1 or player > player_count:
		fail("Use launch.ps1 to supply local admission paths.")
		return
	ProjectSettings.set_setting("physics/box3d/audit_determinism", role == "server")
	world = ClassDB.instantiate("EGPBox3DWorld")
	if world == null or world.configure(60, 4, 1, Vector3(0, -9.8, 0)) != OK:
		fail("Box3D configuration failed")
		return
	net = Net.new()
	net.name = "Network"
	net.auto_poll = false
	add_child(net)
	if net.configure({"game_protocol": "egp-box3d-arena-v4", "simulation_fingerprint": world.get_simulation_fingerprint(), "tick_rate": 60, "max_players": 64, "max_entities": 512, "simulated_latency_ms": latency_ms, "simulated_jitter_ms": jitter_ms, "simulated_loss": loss_percent}) != OK:
		fail("Network configuration failed")
		return
	net.diagnostic.connect(record_native_diagnostic)
	if role == "server":
		start_server()
	else:
		if DisplayServer.get_name() != "headless": build_scene()
		net.entity_spawned.connect(spawn_visual)
		net.entity_changed.connect(update_visual)
		net.entity_despawned.connect(remove_visual)
		net.packet_received.connect(receive_poses)
		net.register_message(&"score", func(_peer: int, args: Array):
			if args.size() == 1 and args[0] is Dictionary: scores = args[0], Net.Sender.SERVER)
		var token := FileAccess.get_file_as_bytes(runtime.path_join("player%d.token" % player))
		if token.size() != 2048 or net.join_token(1000 + player, token, bind_ip) != OK:
			fail("Local admission failed")
			return
		DisplayServer.window_set_title("EGP Arena â€¢ Client %d â€¢ %s" % [player, "TEAL" if player == 1 else "AMBER"])
	print("ARENA_STARTED " + JSON.stringify({"role": role, "player": player, "version": Engine.get_version_info().string}))

func record_native_diagnostic(message: String) -> void:
	last_native_diagnostic = message.left(1024)
	last_native_diagnostic_msec = Time.get_ticks_msec()
	last_native_statistics = net.get_statistics()
	print("ARENA_DIAGNOSTIC " + message)

func start_server() -> void:
	net.peer_connected.connect(joined)
	net.peer_disconnected.connect(left)
	net.input_received.connect(receive_input)
	net.packet_received.connect(receive_movement_packet)
	net.register_message(&"action", receive_action, Net.Sender.CLIENT)
	if net.host(0, "127.0.0.1") != OK:
		fail("Local server listener failed")
		return
	# Floor and containment walls are static Box3D hulls.
	world.queue_create_box(1, 1, Vector3(0, -0.5, 0), Vector3(12, 0.5, 9), 0)
	world.queue_create_box(2, 1, Vector3(-12, 1, 0), Vector3(0.25, 1.5, 9), 0)
	world.queue_create_box(3, 1, Vector3(12, 1, 0), Vector3(0.25, 1.5, 9), 0)
	world.queue_create_box(4, 1, Vector3(0, 1, -9), Vector3(12, 1.5, 0.25), 0)
	world.queue_create_box(5, 1, Vector3(0, 1, 9), Vector3(12, 1.5, 0.25), 0)
	for index in range(9):
		var body := 100 + index
		var position := Vector3((index % 3 - 1) * 1.3, 0.7 + (index / 3) * 1.3, -1.5)
		var kind := 10
		if index == 7:
			kind = 11
			world.queue_create_sphere(body, 1, position + Vector3(3, 0, 0), 0.55)
		elif index == 8:
			kind = 12
			world.queue_create_capsule(body, 1, position + Vector3(-3, 0, 0), 0.4, 0.5)
		else:
			world.queue_create_box(body, 1, position, Vector3.ONE * 0.55)
		var entity: int = net.spawn(kind, {"pose": pose(position, Quaternion.IDENTITY), "kind": kind})
		entities[entity] = body
	if world.apply_queued_commands() != OK:
		fail("Arena body creation failed")
		return
	for index in range(6):
		var body := 500 + index
		var p := Vector3(-7 + index * 2.7, 1.0, -5)
		world.queue_create_capsule(body, 1, p, 0.4, 0.45)
		var entity: int = net.spawn(20, {"pose": pose(p, Quaternion.IDENTITY), "kind": 20})
		entities[entity] = body
		ai[entity] = {"index": index, "state": 0, "stunned_until": 0, "attack_tick": -120}
	if world.apply_queued_commands() != OK:
		fail("AI body creation failed")
		return
	for index in range(stress_count):
		var body := 3000 + index
		var column := index % 8
		var row := (index / 8) % 3
		var layer := index / 24
		var p := Vector3((column - 3.5) * 1.05, 0.6 + layer * 1.1, -4.5 + row * 1.1)
		var kind := 13
		if index % 5 == 0:
			kind = 14
			world.queue_create_sphere(body, 1, p, 0.45, 2, 0.7)
		elif index % 5 == 1:
			kind = 15
			world.queue_create_capsule(body, 1, p, 0.3, 0.25, 2, 1.0)
		else:
			world.queue_create_box(body, 1, p, Vector3.ONE * 0.45, 2, 0.5 + (index % 3) * 0.7)
		var entity: int = net.spawn(kind, {"pose": pose(p, Quaternion.IDENTITY), "kind": kind})
		entities[entity] = body
	for index in range(6):
		var body := 4000 + index
		var p := Vector3(-8 if index < 3 else 8, 0.6, (index % 3 - 1) * 4.0)
		var extents := Vector3(1.4, 0.3, 1.4) if index % 2 == 0 else Vector3(0.4, 1, 1.8)
		world.queue_create_box(body, 1, p, extents, 1)
		var kind := 40 + index % 2
		var entity: int = net.spawn(kind, {"pose": pose(p, Quaternion.IDENTITY), "kind": kind})
		entities[entity] = body
		mechanisms[entity] = {"origin": p, "index": index, "extents": extents}
	if world.apply_queued_commands() != OK:
		fail("Stress and kinematic mechanism creation failed")
		return
	net.simulation_tick.connect(simulate)
	for index in range(8):
		var angle := index * TAU / 8.0
		var p := Vector3(cos(angle) * 5.0, 0.8, sin(angle) * 5.0)
		var entity: int = net.spawn(30, {"pose": pose(p, Quaternion.IDENTITY), "kind": 30})
		pickups[entity] = {"position": p, "next_tick": 0, "index": index}
	var address := "127.0.0.1:%d" % net.get_statistics().local_port
	for index in range(1, player_count + 1):
		var admission: Dictionary = net.issue_token(1000 + index, address)
		if admission.error != OK:
			fail("Token issuance failed")
			return
		var file := FileAccess.open(runtime.path_join("player%d.token" % index), FileAccess.WRITE)
		file.store_buffer(admission.token)
		file.close()
	print("ARENA_SERVER_READY " + JSON.stringify({"address": address, "bodies": world.get_body_count(), "state_bytes": var_to_bytes({"pose": pose(Vector3.ZERO, Quaternion.IDENTITY), "kind": 1}).size()}))

func pose(p: Vector3, q: Quaternion) -> PackedFloat32Array:
	return PackedFloat32Array([p.x, p.y, p.z, q.x, q.y, q.z, q.w])

func joined(peer: int) -> void:
	for identity in net.get_peers():
		if identity.peer_id != peer: continue
		var index: int = identity.client_id - 1000
		if index < 1 or index > player_count:
			net.disconnect_peer(peer)
			return
		var body := 1000 + index
		var angle := index * TAU / player_count
		var p := Vector3(cos(angle) * 7, 1.0 + (index / 16) * 1.5, sin(angle) * 6)
		sequence += 1
		world.queue_create_sphere(body, sequence, p, 0.65)
		var entity: int = net.spawn(100 + index, {"pose": pose(p, Quaternion.IDENTITY), "kind": 100 + index}, peer)
		entities[entity] = body
		peer_entities[peer] = entity
		checkpoint = PackedByteArray()
		controls[entity] = {"x": 0.0, "z": 0.0, "jump": false, "reset": false, "pulse": false, "spawn": false}
		last_input[entity] = Time.get_ticks_msec()
		print("ARENA_PEER_JOIN " + JSON.stringify({"peer": peer, "player": index, "entity": entity}))
		net.send_message(peer, &"score", [scores])

func left(peer: int) -> void:
	peer_entities.erase(peer)
	peer_pose_cursors.erase(peer)
	for entity in entities.keys():
		if net.get_entity(entity).get("authority_peer", -1) != peer: continue
		sequence += 1
		world.queue_destroy_body(entities[entity], sequence)
		if reset_request_entity == entity:
			reset_requested = false
			reset_request_entity = 0
		controls.erase(entity)
		last_input.erase(entity)
		accepted_sequences.erase(entity)
		checkpoint = PackedByteArray()
		jump_ticks.erase(entity)
		entities.erase(entity)
		pose_epochs.erase(entity)
		net.despawn(entity)
	print("ARENA_PEER_LEFT %d" % peer)

func receive_input(_peer: int, entity: int, input: Dictionary) -> void:
	# EGPNet has checked entity ownership; gameplay validates every remaining field.
	if not controls.has(entity) or input.size() != 6: return
	if not input.get("x") is float or not input.get("z") is float or not input.get("jump") is bool: return
	if not input.get("reset") is bool: return
	if not input.get("pulse") is bool or not input.get("spawn") is bool: return
	if not is_finite(input.x) or not is_finite(input.z): return
	if absf(input.x) > 1.0 or absf(input.z) > 1.0: return
	controls[entity] = input.duplicate()
	last_input[entity] = Time.get_ticks_msec()
	if input.reset: queue_checkpoint_restore(entity)

func receive_movement_packet(peer: int, data: PackedByteArray, channel: int, delivery: int) -> void:
	if channel == 0 and delivery == Net.Delivery.UNRELIABLE and data.size() == 12 and data.decode_u32(0) == 0x50494e47:
		net.send_packet(peer, data, 0, Net.Delivery.UNRELIABLE)
		return
	if channel != 1 or delivery != Net.Delivery.UNRELIABLE or data.size() != 28: return
	if data.decode_u32(0) != 0x41524e41: return
	var entity := data.decode_u64(4)
	if not controls.has(entity) or net.get_entity(entity).get("authority_peer", -1) != peer: return
	var stamp := data.decode_u32(12)
	if stamp <= int(accepted_sequences.get(entity, 0)): return
	var x := data.decode_float(16)
	var z := data.decode_float(20)
	var flags := data.decode_u32(24)
	if not is_finite(x) or not is_finite(z) or absf(x) > 1 or absf(z) > 1 or flags > 15: return
	accepted_sequences[entity] = stamp
	receive_input(peer, entity, {"x": x, "z": z, "jump": bool(flags & 1), "reset": bool(flags & 2), "pulse": bool(flags & 4), "spawn": bool(flags & 8)})

func queue_checkpoint_restore(entity: int) -> void:
	if reset_requested or not controls.has(entity): return
	reset_requested = true
	reset_request_entity = entity
	reset_request_msec = Time.get_ticks_msec()
	print("ARENA_CHECKPOINT_REQUEST_QUEUED entity=%d peers=%d ready=%s" % [entity, net.get_peers().size(), str(not checkpoint.is_empty())])

func simulate(tick: int, server: bool) -> void:
	if not server: return
	var tick_actions: Dictionary = {}
	for request in action_requests:
		if not controls.has(request.entity): continue
		if request.action == "reset": queue_checkpoint_restore(request.entity)
		var actions: Dictionary = tick_actions.get(request.entity, {})
		actions[request.action] = true
		tick_actions[request.entity] = actions
	action_requests.clear()
	# Admission may have queued body creation since the last tick.
	if world.apply_queued_commands() != OK:
		fail("Queued admission bodies failed")
		return
	# Only server-captured solver bytes are restored, at an empty command boundary.
	if reset_requested and (not controls.has(reset_request_entity) or Time.get_ticks_msec() - reset_request_msec > 15000):
		print("ARENA_CHECKPOINT_REQUEST_EXPIRED readiness or admission changed")
		reset_requested = false
		reset_request_entity = 0
	if reset_requested and not checkpoint.is_empty() and Time.get_ticks_msec() - last_reset_msec > 3000 and net.get_peers().size() == player_count:
		if world.restore_snapshot(checkpoint) != OK:
			fail("Trusted checkpoint restore failed")
			return
		checkpoint_restores += 1
		for entity in entities: pose_epochs[entity] = int(pose_epochs.get(entity, 0)) + 1
		last_reset_msec = Time.get_ticks_msec()
		print("ARENA_CHECKPOINT_RESTORED %d" % world.get_tick())
		reset_requested = false
		reset_request_entity = 0
	for entity in mechanisms:
		var mechanism: Dictionary = mechanisms[entity]
		var phase: float = tick / 60.0 * 0.8 + mechanism.index
		var origin: Vector3 = mechanism.origin
		var p := origin + Vector3(0, (sin(phase) + 1) * 1.5, 0) if mechanism.index % 2 == 0 else origin + Vector3(sin(phase) * 2.0, 0, 0)
		var velocity := Vector3(0, cos(phase) * 1.2, 0) if mechanism.index % 2 == 0 else Vector3(cos(phase) * 1.6, 0, 0)
		sequence += 1
		world.queue_body_state(entities[entity], sequence, p, Quaternion.IDENTITY, velocity, Vector3.ZERO)
	for entity in entities:
		var body: Dictionary = world.get_body_state(entities[entity])
		if body.is_empty(): continue
		var p: Vector3 = body.position
		if p.y < -6 or absf(p.x) > 15 or absf(p.z) > 12:
			sequence += 1
			var safe := Vector3((int(entities[entity]) % 7 - 3) * 1.2, 3, 0)
			world.queue_body_state(entities[entity], sequence, safe, Quaternion.IDENTITY, Vector3.ZERO, Vector3.ZERO)
			pose_epochs[entity] = int(pose_epochs.get(entity, 0)) + 1
			rescued_bodies += 1
	update_ai(tick)
	for entity in controls:
		var body: Dictionary = world.get_body_state(entities[entity])
		if body.is_empty(): continue
		var input: Dictionary = controls[entity]
		var direction := Vector3(input.x, 0, input.z).limit_length()
		if Time.get_ticks_msec() - int(last_input[entity]) > 300:
			direction = Vector3.ZERO
			input = {"jump": false, "pulse": false, "spawn": false}
		var v: Vector3 = body.linear_velocity
		var desired := direction * 5.5
		var impulse := Vector3(desired.x - v.x, 0, desired.z - v.z) * 0.12
		if input.jump and body.position.y < 0.9 and tick - int(jump_ticks.get(entity, -60)) > 45:
			impulse.y = 6.5
			jump_ticks[entity] = tick
		sequence += 1
		world.queue_impulse(entities[entity], sequence, impulse)
		if tick_actions.get(entity, {}).get("pulse", false) and Time.get_ticks_msec() - int(action_times.get("pulse%d" % entity, -10000)) > 1800:
			action_times["pulse%d" % entity] = Time.get_ticks_msec()
			shockwave(body.position, entity, tick)
		if tick_actions.get(entity, {}).get("spawn", false) and Time.get_ticks_msec() - int(action_times.get("spawn%d" % entity, -10000)) > 1000:
			action_times["spawn%d" % entity] = Time.get_ticks_msec()
			spawn_prop(body.position + Vector3(0, 4, -2))
	var physics_started := Time.get_ticks_usec()
	var physics_error: Error = world.step_tick(world.get_tick() + 1)
	var physics_ms := (Time.get_ticks_usec() - physics_started) / 1000.0
	physics_times.append(physics_ms)
	if net.get_peers().size() == player_count:
		full_load_physics_times.append(physics_ms)
		if full_load_physics_times.size() > 600: full_load_physics_times.pop_front()
	if physics_times.size() > 600: physics_times.pop_front()
	max_physics_ms = maxf(max_physics_ms, physics_ms)
	if physics_error != OK:
		fail("Authoritative physics step failed")
		return
	if checkpoint.is_empty() and net.get_peers().size() == player_count and world.get_tick() >= 120:
		checkpoint = world.capture_snapshot()
		if checkpoint.is_empty():
			fail("Trusted checkpoint capture failed")
			return
		print("ARENA_CHECKPOINT_CAPTURED %d bytes" % checkpoint.size())
	# Compact public poses stay below the native 128-byte inline-state limit.
	if tick % 3 == 0:
		for entity in entities:
			var body: Dictionary = world.get_body_state(entities[entity])
			var kind: int = 20 + int(ai[entity].state) if ai.has(entity) else net.get_entity(entity).kind
			if tick % 60 == 0 and net.update_entity(entity, {"pose": pose(body.position, body.rotation), "kind": kind}) != OK:
				fail("Authoritative pose replication failed")
				return
		broadcast_poses(tick)
	if tick % 6 == 0:
		update_pickups(tick)

func _process(delta: float) -> void:
	if net == null: return
	var now := Time.get_ticks_msec()
	var wall_gap := now - last_process_msec if last_process_msec > 0 else 0
	if last_process_msec > 0: largest_frame_gap_ms = maxi(largest_frame_gap_ms, now - last_process_msec)
	last_process_msec = now
	elapsed += delta
	if wall_gap > 0: frame_times.append(float(wall_gap))
	if frame_times.size() > 600: frame_times.pop_front()
	if recovering:
		try_recovery()
		return
	if role == "client" and player <= 2 and duration > 0 and elapsed > 16.0 + player * 2 and not stall_exercised:
		stall_exercised = true
		print("ARENA_EXERCISE_CLIENT_STALL 700 ms")
		OS.delay_msec(700)
	var poll_started := Time.get_ticks_msec()
	var poll_gap := poll_started - last_poll_msec if last_poll_msec > 0 else 0
	last_poll_msec = poll_started
	var poll_error: Error = net.poll()
	if poll_error != OK:
		var statistics: Dictionary = net.get_statistics()
		var failure := {"role": role, "player": player, "seconds": elapsed, "poll_error": poll_error, "poll_gap_ms": poll_gap, "poll_duration_ms": Time.get_ticks_msec() - poll_started, "frame_gap_ms": wall_gap, "largest_frame_gap_ms": largest_frame_gap_ms, "state": net.get_state(), "native_last_error": statistics.get("last_error", last_native_diagnostic), "native_diagnostic_age_ms": Time.get_ticks_msec() - last_native_diagnostic_msec if last_native_diagnostic_msec > 0 else -1, "native_statistics_after_failure": statistics, "native_statistics_at_diagnostic": last_native_statistics}
		print("ARENA_POLL_FAILURE " + JSON.stringify(failure))
		var file := FileAccess.open(runtime.path_join("%s%d-poll-failure.json" % [role, player]), FileAccess.WRITE)
		if file != null: file.store_string(JSON.stringify(failure, "\t"))
		if role == "client": begin_recovery("poll watchdog error=%d gap=%dms" % [poll_error, poll_gap])
		else: fail("Networking poll failed error=%d gap=%dms: %s" % [poll_error, poll_gap, str(failure.native_last_error)])
		return
	if role == "server": service_admission_requests()
	if role == "server":
		var peer_count: int = net.get_peers().size()
		max_peers_seen = maxi(max_peers_seen, peer_count)
		if peer_count == player_count: full_load_seconds += delta
	if role == "client" and net.get_state() in ["Disconnected", "Stopped"]:
		begin_recovery("connection stopped")
		return
	if role == "client":
		input_timer += delta
		if owned != 0 and input_timer >= 0.05:
			input_timer = minf(input_timer - 0.05, 0.05)
			var x := float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A))
			var z := float(Input.is_physical_key_pressed(KEY_S)) - float(Input.is_physical_key_pressed(KEY_W))
			var jump := Input.is_physical_key_pressed(KEY_SPACE)
			var reset := Input.is_physical_key_pressed(KEY_R)
			var pulse := Input.is_physical_key_pressed(KEY_Q)
			var spawn := Input.is_physical_key_pressed(KEY_E)
			if x != 0 or z != 0 or jump: bot = false
			if bot:
				var decision := think_player()
				x = decision.direction.x
				z = decision.direction.y
				jump = decision.jump
				if duration > 0 and player == 1 and elapsed > 10.0 and not showcased_reset:
					reset = true
					showcased_reset = true
				if elapsed > 5.0 and not showcased_spawn:
					spawn = true
					showcased_spawn = true
				pulse = decision.pulse
			if pulse: request_action("pulse", 1900)
			if spawn: request_action("spawn", 1100)
			if reset: request_action("reset", 3200)
			var packet := PackedByteArray()
			packet.resize(28)
			packet.encode_u32(0, 0x41524e41)
			packet.encode_u64(4, owned)
			input_sequence += 1
			packet.encode_u32(12, input_sequence)
			packet.encode_float(16, x)
			packet.encode_float(20, z)
			packet.encode_u32(24, int(jump))
			var sent: Error = net.send_packet(0, packet, 1, Net.Delivery.UNRELIABLE)
			if sent != OK and sent != ERR_BUSY:
				fail("Owned input transmission failed: %d" % sent)
				return
		if not outgoing_actions.is_empty() and net.get_state() == "Connected":
			var sent: Error = net.send_message(0, &"action", outgoing_actions[0])
			if sent == OK: outgoing_actions.pop_front()
			elif sent != ERR_BUSY:
				fail("Reliable action transmission failed")
				return
		ping_timer += delta
		if owned != 0 and ping_timer >= 1.0:
			ping_timer = 0.0
			var ping := PackedByteArray()
			ping.resize(12)
			ping.encode_u32(0, 0x50494e47)
			ping.encode_u64(4, Time.get_ticks_msec())
			net.send_packet(0, ping, 0, Net.Delivery.UNRELIABLE)
		render_tick = pose_interpolator.advance(delta)
		for entity in visuals if DisplayServer.get_name() != "headless" else []:
			if pose_history.has(entity) and not pose_history[entity].is_empty():
				visuals[entity].transform = pose_interpolator.sample(entity, delta)
			else:
				var target: Transform3D = targets[entity]
				visuals[entity].transform = visuals[entity].transform.interpolate_with(target, 1.0 - exp(-delta * 18.0))
		if camera != null and owned != 0 and visuals.has(owned) and not overview:
			var focus: Vector3 = visuals[owned].position
			if not camera_focus_ready:
				camera_focus = focus
				camera_focus_ready = true
			camera_focus = camera_focus.lerp(focus, 1.0 - exp(-delta * 4.0))
			camera.position = camera_focus + Vector3(0, 11, 14)
			camera.look_at(camera_focus + Vector3(0, 0, -2))
		for effect in effects.duplicate():
			effect.age += delta
			effect.node.scale = Vector3.ONE * (1.0 + effect.age * 12.0)
			if effect.age > 0.5:
				effect.node.queue_free()
				effects.erase(effect)
		if label != null:
			label.text = "EGP / PHYSICS ARENA\n\nCLIENT %d / %s\n%s / %d shared entities\n%d independent tactical players\n\nBox3D 60 Hz / 4 substeps\nEncrypted UDP / poses 20 Hz\n%d mixed props / 6 mechanisms\n\n%s\nBRAIN: %s\nDecisions %d / escape routes %d\nDistance %.1f m / FPS %d\n\nWASD Move / SPACE Jump\nQ Shockwave / E Spawn prop\nR Checkpoint / B Toggle AI\nTAB Camera overview\n\nNETWORK PROFILE\n%.0f ms each way / jitter ±%.0f ms\n%.1f%% loss / echo RTT %d ms\nFast poses %d\n\nCOLLECT ENERGY / EVADE THE PACK\nTEAL %d / AMBER %d\n6 enemies: flank / strike / evade" % [player, "TEAL" if player == 1 else "AMBER", net.get_state().to_upper(), visuals.size(), player_count, stress_count, "TACTICAL AUTOPILOT" if bot else "YOU HAVE CONTROL", brain_state, brain_decisions, brain_escapes, movement_distance, Engine.get_frames_per_second(), latency_ms, jitter_ms, loss_percent, rtt_ms, fast_updates, teal_score, amber_score]
		if not capture_path.is_empty() and not captured and elapsed > 8.0 and DisplayServer.get_name() != "headless":
			captured = true
			capture.call_deferred()
	report_timer += delta
	if report_timer > 3.0:
		report_timer = 0.0
		report()
	if duration > 0 and elapsed >= duration:
		report()
		get_tree().quit()

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_B:
		bot = not bot
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_TAB:
		overview = not overview
		camera.position = Vector3(18, 23, 27)
		camera.look_at(Vector3(-2, 0, 0))

func report() -> void:
	var data := {"role": role, "player": player, "state": net.get_state(), "peers": net.get_peers().size(), "entities": net.get_entities().size(), "tick": world.get_tick() if role == "server" else max_remote_tick, "updates": received_updates, "distance": movement_distance, "seconds": elapsed, "checkpoint_restores": checkpoint_restores}
	data.merge({"ai": ai.size(), "ai_attacks": ai_attacks, "pulses": pulses, "spawned_props": prop_count, "latency_ms_each_way": latency_ms, "jitter_ms": jitter_ms, "loss_percent": loss_percent, "measured_rtt_ms": rtt_ms})
	data.merge({"pickups_collected": pickups_collected, "scores": scores})
	data.rescued_bodies = rescued_bodies
	data.checkpoint_restore_pending = reset_requested
	data.checkpoint_ready = not checkpoint.is_empty()
	data.superposition = $MatchReplication.get_statistics()
	data.match_state_quiescent = match_state_frozen
	data.match_state = {"teal_score": teal_score, "amber_score": amber_score, "pickups_collected": pickups_collected, "checkpoint_restores": checkpoint_restores}
	if role == "server": data.merge({"backend_verified_steps": world.get_meta("box3d_audit_verified_steps", 0), "backend_verified_boundaries": world.get_meta("box3d_audit_verified_boundaries", 0), "backend_hash_mismatches": world.get_meta("box3d_audit_hash_mismatches", 0)})
	data.merge({"client_id": 1000 + player, "bind_ip": bind_ip, "local_port": net.get_statistics().get("local_port", 0), "expected_players": player_count})
	data.merge({"brain_state": brain_state, "brain_decisions": brain_decisions, "brain_target": brain_target, "brain_escapes": brain_escapes})
	data.merge({"max_peers_seen": max_peers_seen, "full_load_seconds": full_load_seconds})
	data.merge({"recoveries": recoveries, "recovery_attempts": recovery_attempts, "largest_frame_gap_ms": largest_frame_gap_ms})
	var full_sorted := full_load_physics_times.duplicate()
	full_sorted.sort()
	if not full_sorted.is_empty():
		full_load_physics_p95_worst_ms = maxf(full_load_physics_p95_worst_ms, full_sorted[int(full_sorted.size() * 0.95)])
	data.full_load_physics_p95_worst_ms = full_load_physics_p95_worst_ms
	if role == "client":
		var presentation: Dictionary = pose_interpolator.get_statistics()
		data.owned_presentation = presentation.entities.get(owned, {})
		var classes: Dictionary = {}
		for entity in presentation.entities:
			var kind: int = int(visual_kinds.get(entity, 0))
			var group := "players" if kind >= 100 else ("ai" if kind in [20, 21, 22, 23] else ("mechanisms" if kind in [40, 41] else "props"))
			var stats: Dictionary = presentation.entities[entity]
			if not classes.has(group): classes[group] = {"held_moving": 0, "held_stationary": 0, "interpolated": 0, "extrapolated": 0, "epoch_resets": 0}
			classes[group].held_moving += int(stats.held_moving_samples)
			classes[group].held_stationary += int(stats.held_stationary_samples)
			classes[group].interpolated += int(stats.interpolated_samples)
			classes[group].extrapolated += int(stats.extrapolated_samples)
			classes[group].epoch_resets += int(stats.epoch_resets)
		data.presentation_classes = classes
		presentation.erase("entities")
		data.presentation = presentation
	var sorted_physics := physics_times.duplicate()
	sorted_physics.sort()
	data.merge({"stress_bodies": stress_count, "physics_step_p95_ms": sorted_physics[int(sorted_physics.size() * 0.95)] if not sorted_physics.is_empty() else 0, "physics_step_max_ms": max_physics_ms, "kinematic_mechanisms": mechanisms.size(), "world_bodies": world.get_body_count()})
	var sorted_frames := frame_times.duplicate()
	sorted_frames.sort()
	var sorted_gaps := own_pose_gaps.duplicate()
	sorted_gaps.sort()
	data.merge({"fps": Engine.get_frames_per_second(), "frame_p95_ms": sorted_frames[int(sorted_frames.size() * 0.95)] if not sorted_frames.is_empty() else 0, "fast_pose_updates": fast_updates, "owned_pose_gap_p95_ms": sorted_gaps[int(sorted_gaps.size() * 0.95)] if not sorted_gaps.is_empty() else 0})
	if role == "client": data.tick = newest_pose_tick
	print("ARENA_STATUS " + JSON.stringify(data))
	var file := FileAccess.open(runtime.path_join("%s%d-status.json" % [role, player]), FileAccess.WRITE)
	if file != null: file.store_string(JSON.stringify(data, "\t"))

func spawn_visual(entity: int, kind: int, state: Dictionary) -> void:
	visual_kinds[entity] = kind
	if DisplayServer.get_name() == "headless":
		var anchor := Node3D.new()
		add_child(anchor)
		visuals[entity] = anchor
		update_visual(entity, state)
		anchor.transform = targets[entity]
		if kind == 100 + player: owned = entity
		return
	var mesh: Mesh
	if kind in [1, 2, 11, 14, 30] or kind >= 100:
		var sphere := SphereMesh.new()
		sphere.radius = 0.65 if kind < 3 or kind >= 100 else 0.55
		if kind == 30: sphere.radius = 0.25
		elif kind == 14: sphere.radius = 0.45
		sphere.height = sphere.radius * 2
		mesh = sphere
	elif kind in [12, 15] or kind in [20, 21, 22, 23]:
		var capsule := CapsuleMesh.new()
		capsule.radius = 0.4
		capsule.height = 1.8
		if kind == 15:
			capsule.radius = 0.3
			capsule.height = 1.1
		mesh = capsule
	else:
		var box := BoxMesh.new()
		box.size = Vector3.ONE * 1.1
		if kind == 13: box.size = Vector3.ONE * 0.9
		if kind == 40: box.size = Vector3(2.8, 0.6, 2.8)
		elif kind == 41: box.size = Vector3(0.8, 2, 3.6)
		mesh = box
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material(COLORS[kind - 1] if kind < 3 else COLORS[2], kind < 3)
	if kind >= 100: instance.material_override = material(COLORS[(kind - 101) % 2], true)
	if kind == 30: instance.material_override = material(Color("ffcc66"), true)
	if kind in [40, 41]: instance.material_override = material(Color("667bd6"), true)
	if kind in [20, 21, 22, 23]:
		instance.material_override = material(Color("e45a87"), true)
		var eye := MeshInstance3D.new()
		var eye_mesh := BoxMesh.new()
		eye_mesh.size = Vector3(0.62, 0.12, 0.12)
		eye.mesh = eye_mesh
		eye.material_override = material(Color("ffe9ad"), true)
		eye.position = Vector3(0, 0.35, 0.38)
		instance.add_child(eye)
	add_child(instance)
	visuals[entity] = instance
	update_visual(entity, state)
	instance.transform = targets[entity]
	if kind == 100 + player: owned = entity

func update_visual(entity: int, state: Dictionary) -> void:
	if not visuals.has(entity): return
	if fast_ticks.has(entity): return
	var values: PackedFloat32Array = state.pose
	if values.size() != 7: return
	var p := Vector3(values[0], values[1], values[2])
	var q := Quaternion(values[3], values[4], values[5], values[6]).normalized()
	targets[entity] = Transform3D(Basis(q), p)
	received_updates += 1
	if int(state.kind) in [20, 21, 22, 23] and DisplayServer.get_name() != "headless":
		var colors := [Color("8867cd"), Color("ef658e"), Color("ff6544"), Color("56a9dc")]
		visuals[entity].material_override.albedo_color = colors[clampi(int(state.kind) - 20, 0, 3)]
	if entity == owned:
		if has_position: movement_distance += last_position.distance_to(p)
		last_position = p
		has_position = true
	max_remote_tick = int(net.get_statistics().get("tick", 0))

func remove_visual(entity: int) -> void:
	if visuals.has(entity): visuals[entity].queue_free()
	visuals.erase(entity)
	visual_kinds.erase(entity)
	targets.erase(entity)
	pose_history.erase(entity)
	pose_interpolator.remove(entity)
	pose_epochs.erase(entity)
	fast_ticks.erase(entity)
	if owned == entity: owned = 0

func material(color: Color, glow := false) -> StandardMaterial3D:
	var result := StandardMaterial3D.new()
	result.albedo_color = color
	result.roughness = 0.55
	result.metallic = 0.25
	result.emission_enabled = glow
	result.emission = color * 0.25
	return result

func box_visual(p: Vector3, size: Vector3, color: Color) -> void:
	var instance := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	instance.mesh = box
	instance.material_override = material(color)
	instance.position = p
	add_child(instance)

func build_scene() -> void:
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color("091321")
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color = Color("b4cced")
	settings.ambient_light_energy = 0.65
	settings.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.environment = settings
	add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -25, 0)
	light.light_energy = 1.5
	light.shadow_enabled = true
	add_child(light)
	box_visual(Vector3(0, -0.5, 0), Vector3(24, 1, 18), Color("1b2e42"))
	for x in range(-12, 13, 2): box_visual(Vector3(x, 0.006, 0), Vector3(0.025, 0.012, 18), Color("30485e"))
	for z in range(-8, 9, 2): box_visual(Vector3(0, 0.006, z), Vector3(24, 0.012, 0.025), Color("30485e"))
	for x in [-12, 12]: box_visual(Vector3(x, 1, 0), Vector3(0.5, 3, 18), Color("263f56"))
	for z in [-9, 9]: box_visual(Vector3(0, 1, z), Vector3(24, 3, 0.5), Color("263f56"))
	for index in [0, 1]:
		box_visual(Vector3(-4 if index == 0 else 4, 0.015, 3), Vector3(2.3, 0.02, 2.3), COLORS[index] * 0.6)
	camera = Camera3D.new()
	camera.position = Vector3(18, 23, 27)
	add_child(camera)
	camera.look_at(Vector3(-2, 0, 0))
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	camera.fov = 65
	camera.current = true
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var panel := Panel.new()
	panel.position = Vector2(20, 20)
	panel.size = Vector2(275, 600)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.025, 0.055, 0.09, 0.94)
	style.border_color = COLORS[player - 1]
	style.border_width_left = 3
	style.corner_radius_bottom_right = 12
	style.corner_radius_top_right = 12
	panel.add_theme_stylebox_override("panel", style)
	canvas.add_child(panel)
	label = Label.new()
	label.position = Vector2(36, 32)
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color("d8e7f7"))
	canvas.add_child(label)
	var title := Label3D.new()
	title.text = "B O X 3 D   /   E G P"
	title.position = Vector3(0, 3.5, -8.8)
	title.font_size = 64
	title.modulate = Color("9dbdd7")
	add_child(title)
	for side in [-1, 1]:
		for z in [-6, 0, 6]:
			box_visual(Vector3(side * 11.5, 3.2, z), Vector3(0.2, 3.5, 0.2), Color("ef658e"))
			box_visual(Vector3(side * 11.5, 5, z), Vector3(0.6, 0.3, 0.6), Color("54e4d0"))
	var reactor := MeshInstance3D.new()
	var ring := TorusMesh.new()
	ring.inner_radius = 2.0
	ring.outer_radius = 2.3
	reactor.mesh = ring
	reactor.position = Vector3(0, 6, -5)
	reactor.material_override = material(Color("54e4d0"), true)
	add_child(reactor)
	box_visual(Vector3(0, 0.01, -5), Vector3(5, 0.02, 5), Color("314467"))

func update_ai(tick: int) -> void:
	for entity in ai:
		var agent: Dictionary = ai[entity]
		var body: Dictionary = world.get_body_state(entities[entity])
		if body.is_empty(): continue
		var target := Vector3(sin(tick * 0.007 + agent.index) * 7, 0, cos(tick * 0.007 + agent.index) * 5)
		var distance := 100.0
		var target_body: Dictionary = {}
		for candidate in controls:
			var other: Dictionary = world.get_body_state(entities[candidate])
			if other.is_empty(): continue
			var d: float = body.position.distance_to(other.position)
			if d < distance:
				distance = d
				target_body = other
		agent.state = 0
		var direction: Vector3 = target - body.position
		if not target_body.is_empty() and distance < 14:
			agent.state = 1
			# Lead moving targets, with alternate agents taking opposite flanks.
			target = target_body.position + target_body.linear_velocity * 0.35
			direction = target - body.position
			if distance > 3:
				direction += Vector3(-direction.z, 0, direction.x).normalized() * (2.5 if agent.index % 2 == 0 else -2.5)
			if distance < 1.9 and tick - int(agent.attack_tick) > 100:
				agent.state = 2
				agent.attack_tick = tick
				ai_attacks += 1
				sequence += 1
				world.queue_impulse(entities[entity], sequence, direction.normalized() * 5 + Vector3(0, 2, 0))
			if tick - int(agent.attack_tick) < 15: agent.state = 2
		if tick < int(agent.stunned_until):
			agent.state = 3
			direction = -direction
		# Separate neighbors so the pursuit pack does not all choose one point.
		for other_entity in ai:
			if other_entity == entity: continue
			var other: Dictionary = world.get_body_state(entities[other_entity])
			if other.is_empty(): continue
			var offset: Vector3 = body.position - other.position
			if offset.length() < 1.5: direction += offset.normalized() * 3.0
		direction.y = 0
		var desired := direction.normalized() * (2.0 if agent.state == 3 else 3.3)
		var v: Vector3 = body.linear_velocity
		sequence += 1
		world.queue_impulse(entities[entity], sequence, Vector3(desired.x - v.x, 0, desired.z - v.z) * 0.07)

func shockwave(p: Vector3, source: int, tick: int) -> void:
	pulses += 1
	for entity in entities:
		if entity == source: continue
		var body: Dictionary = world.get_body_state(entities[entity])
		if body.is_empty(): continue
		var offset: Vector3 = body.position - p
		if offset.length() < 6:
			sequence += 1
			world.queue_impulse(entities[entity], sequence, offset.normalized() * (6 - offset.length()) * 2 + Vector3(0, 3, 0))
			if ai.has(entity): ai[entity].stunned_until = tick + 180
	if pulse_events.size() < 32: pulse_events.append(p)

func spawn_prop(p: Vector3) -> void:
	if prop_count >= 20: return
	next_body += 1
	sequence += 1
	var kind := 11 if prop_count % 2 == 0 else 10
	if kind == 11: world.queue_create_sphere(next_body, sequence, p, 0.55)
	else: world.queue_create_box(next_body, sequence, p, Vector3.ONE * 0.55)
	var entity: int = net.spawn(kind, {"pose": pose(p, Quaternion.IDENTITY), "kind": kind})
	if entity == 0:
		fail("Spawnable prop allocation failed")
		return
	entities[entity] = next_body
	prop_count += 1
	# Membership changed; discard the old checkpoint to avoid restoring missing bodies.
	checkpoint = PackedByteArray()

func pulse_visual(p: Vector3) -> void:
	if DisplayServer.get_name() == "headless": return
	var instance := MeshInstance3D.new()
	var ring := TorusMesh.new()
	ring.inner_radius = 0.85
	ring.outer_radius = 1.0
	instance.mesh = ring
	instance.material_override = material(Color("54e4d0"), true)
	instance.position = p + Vector3(0, 0.2, 0)
	add_child(instance)
	effects.append({"node": instance, "age": 0.0})

func update_pickups(tick: int) -> void:
	# Bounded validation gives every staggered client time to read one stable
	# authoritative score revision. Physics, inputs, AI and poses keep running.
	if duration > 0 and elapsed >= duration - (30.0 if player_count > 2 else 14.0):
		if not match_state_frozen:
			match_state_frozen = true
			if score_dirty:
				net.broadcast_message(&"score", [scores])
				score_dirty = false
			print("ARENA_MATCH_STATE_QUIESCENT " + JSON.stringify({"teal_score": teal_score, "amber_score": amber_score, "pickups_collected": pickups_collected, "checkpoint_restores": checkpoint_restores}))
		return
	for entity in pickups:
		var pickup: Dictionary = pickups[entity]
		if pickup.next_tick > 0:
			if tick < int(pickup.next_tick): continue
			pickup.next_tick = 0
			net.update_entity(entity, {"pose": pose(pickup.position, Quaternion.IDENTITY), "kind": 30})
		for owner in controls:
			var body: Dictionary = world.get_body_state(entities[owner])
			if body.is_empty() or body.position.distance_to(pickup.position) > 1.3: continue
			var index: int = net.get_entity(owner).kind - 100
			scores[index] = int(scores.get(index, 0)) + 1
			if index % 2 == 1: teal_score += 1
			else: amber_score += 1
			pickups_collected += 1
			pickup.next_tick = tick + 300
			net.update_entity(entity, {"pose": pose(Vector3(0, -20, 0), Quaternion.IDENTITY), "kind": 30})
			score_dirty = true
			break
	if score_dirty and tick % 30 == 0:
		net.broadcast_message(&"score", [scores])
		score_dirty = false

func broadcast_poses(tick: int) -> void:
	if not pulse_events.is_empty():
		var events := PackedByteArray()
		events.resize(8 + pulse_events.size() * 12)
		events.encode_u32(0, 0x50554c53)
		events.encode_u32(4, pulse_events.size())
		for index in range(pulse_events.size()):
			events.encode_float(8 + index * 12, pulse_events[index].x)
			events.encode_float(12 + index * 12, pulse_events[index].y)
			events.encode_float(16 + index * 12, pulse_events[index].z)
		for peer in net.get_peers(): net.send_packet(peer.peer_id, events, 3, Net.Delivery.UNRELIABLE)
		pulse_events.clear()
	# Encode bodies once, then build a bounded personalized stream for each peer.
	var states: Dictionary = {}
	var encoded: Dictionary = {}
	for entity in entities:
		states[entity] = world.get_body_state(entities[entity])
		var body: Dictionary = states[entity]
		var row := PackedByteArray()
		row.resize(38)
		row.encode_u32(0, entity)
		row.encode_u32(4, 20 + int(ai[entity].state) if ai.has(entity) else net.get_entity(entity).kind)
		row.encode_u32(8, int(pose_epochs.get(entity, 0)))
		var data: PackedFloat32Array = pose(body.position, body.rotation)
		data.append_array(PackedFloat32Array([body.linear_velocity.x, body.linear_velocity.y, body.linear_velocity.z, body.angular_velocity.x, body.angular_velocity.y, body.angular_velocity.z]))
		for field in range(13):
			var scale_value := 32767.0 if field in [3, 4, 5, 6] else 100.0
			row.encode_s16(12 + field * 2, clampi(roundi(data[field] * scale_value), -32767, 32767))
		encoded[entity] = row
	for peer in net.get_peers():
		var owner: int = peer_entities.get(peer.peer_id, 0)
		if not states.has(owner): continue
		var handles: Array = [owner] + ai.keys() + mechanisms.keys()
		var position: Vector3 = states[owner].position
		var near_players := 0
		var near_props := 0
		for entity in entities:
			if handles.has(entity) or position.distance_squared_to(states[entity].position) > 36: continue
			if controls.has(entity) and near_players < 6:
				handles.append(entity)
				near_players += 1
			elif not controls.has(entity) and near_props < 12:
				handles.append(entity)
				near_props += 1
		var distant: Array = []
		for entity in entities:
			if not handles.has(entity): distant.append(entity)
		var cursor: int = peer_pose_cursors.get(peer.peer_id, 0)
		var distant_budget := maxi(0, 62 - handles.size())
		for index in range(mini(distant_budget, distant.size())): handles.append(distant[(cursor + index) % distant.size()])
		if not distant.is_empty(): peer_pose_cursors[peer.peer_id] = (cursor + distant_budget) % distant.size()
		for start in range(0, handles.size(), 23):
			var count := mini(23, handles.size() - start)
			var packet := PackedByteArray()
			packet.resize(16)
			packet.encode_u32(0, 0x504f5334)
			packet.encode_u64(4, tick)
			packet.encode_u32(12, count)
			for index in range(count): packet.append_array(encoded[handles[start + index]])
			var result: Error = net.send_packet(peer.peer_id, packet, 2, Net.Delivery.UNRELIABLE)
			if result != OK and result != ERR_BUSY: fail("Fast pose stream failed: %d" % result)

func receive_poses(peer: int, packet: PackedByteArray, channel: int, delivery: int) -> void:
	if peer == 0 and delivery == Net.Delivery.UNRELIABLE:
		if channel == 0 and packet.size() == 12 and packet.decode_u32(0) == 0x50494e47:
			var sent := packet.decode_u64(4)
			if sent <= Time.get_ticks_msec(): rtt_ms = Time.get_ticks_msec() - sent
			return
		if channel == 3 and packet.size() >= 8 and packet.decode_u32(0) == 0x50554c53:
			var count := packet.decode_u32(4)
			if count > 32 or packet.size() != 8 + count * 12: return
			for index in range(count):
				var p := Vector3(packet.decode_float(8 + index * 12), packet.decode_float(12 + index * 12), packet.decode_float(16 + index * 12))
				if p.is_finite(): pulse_visual(p)
			return
	if peer != 0 or channel != 2 or delivery != Net.Delivery.UNRELIABLE or packet.size() < 16: return
	if packet.decode_u32(0) != 0x504f5334: return
	var tick := packet.decode_u64(4)
	var count := packet.decode_u32(12)
	if count < 1 or count > 23 or packet.size() != 16 + count * 38: return
	if tick > newest_pose_tick:
		newest_pose_tick = tick
		newest_pose_msec = Time.get_ticks_msec()
	for index in range(count):
		var offset := 16 + index * 38
		var entity := packet.decode_u32(offset)
		if not visuals.has(entity) or tick <= int(fast_ticks.get(entity, 0)): continue
		var kind := packet.decode_u32(offset + 4)
		var epoch := packet.decode_u32(offset + 8)
		var p := Vector3(packet.decode_s16(offset + 12), packet.decode_s16(offset + 14), packet.decode_s16(offset + 16)) / 100.0
		var q := Quaternion(packet.decode_s16(offset + 18) / 32767.0, packet.decode_s16(offset + 20) / 32767.0, packet.decode_s16(offset + 22) / 32767.0, packet.decode_s16(offset + 24) / 32767.0)
		var v := Vector3(packet.decode_s16(offset + 26), packet.decode_s16(offset + 28), packet.decode_s16(offset + 30)) / 100.0
		var angular := Vector3(packet.decode_s16(offset + 32), packet.decode_s16(offset + 34), packet.decode_s16(offset + 36)) / 100.0
		if not p.is_finite() or not q.is_finite() or not v.is_finite() or not angular.is_finite() or q.length_squared() < 0.1: continue
		q = q.normalized()
		var history: Array = pose_history.get(entity, [])
		if not pose_interpolator.submit(entity, tick, Transform3D(Basis(q), p), v, epoch, angular): continue
		if pose_epochs.has(entity) and epoch > int(pose_epochs[entity]):
			history.clear()
			# Explicit reset epoch marks checkpoint/rescue, even for short relocations.
			visuals[entity].transform = Transform3D(Basis(q), p)
		history.append({"tick": tick, "pose": Transform3D(Basis(q), p), "velocity": v})
		while history.size() > 16: history.pop_front()
		pose_history[entity] = history
		pose_epochs[entity] = epoch
		fast_ticks[entity] = tick
		fast_updates += 1
		if entity == owned:
			if last_own_pose_msec > 0:
				own_pose_gaps.append(float(Time.get_ticks_msec() - last_own_pose_msec))
				if own_pose_gaps.size() > 600: own_pose_gaps.pop_front()
			last_own_pose_msec = Time.get_ticks_msec()
			if has_position: movement_distance += last_position.distance_to(p)
			last_position = p
			has_position = true
		if kind in [20, 21, 22, 23] and DisplayServer.get_name() != "headless":
			var colors := [Color("8867cd"), Color("ef658e"), Color("ff6544"), Color("56a9dc")]
			visuals[entity].material_override.albedo_color = colors[int(kind) - 20]

func service_admission_requests() -> void:
	for index in range(1, player_count + 1):
		var request := runtime.path_join("player%d.request" % index)
		if not FileAccess.file_exists(request): continue
		DirAccess.remove_absolute(request)
		var issued: Dictionary = net.issue_token(1000 + index, "127.0.0.1:%d" % net.get_statistics().local_port)
		if issued.error != OK:
			fail("Fresh local admission issuance failed")
			return
		var file := FileAccess.open(runtime.path_join("player%d.recovery.token" % index), FileAccess.WRITE)
		if file == null:
			fail("Fresh admission handoff failed")
			return
		file.store_buffer(issued.token)
		file.close()

func begin_recovery(reason: String) -> void:
	if recovery_attempts >= 3:
		fail("Client recovery exhausted: " + reason)
		return
	print("ARENA_RECOVERY_REQUEST " + reason)
	net.close()
	pose_history.clear()
	pose_interpolator.clear()
	pose_epochs.clear()
	outgoing_actions.clear()
	fast_ticks.clear()
	owned = 0
	newest_pose_tick = 0
	render_tick = 0.0
	camera_focus_ready = false
	has_position = false
	last_own_pose_msec = 0
	recovering = true
	recovery_attempts += 1
	recovery_started = Time.get_ticks_msec()
	var response := runtime.path_join("player%d.recovery.token" % player)
	if FileAccess.file_exists(response): DirAccess.remove_absolute(response)
	var request := FileAccess.open(runtime.path_join("player%d.request" % player), FileAccess.WRITE)
	if request == null:
		fail("Local recovery request failed")
		return
	request.store_string(str(recovery_attempts))
	request.close()
	if label != null: label.text = "RECONNECTING\nRequesting fresh admission and server baselineâ€¦"

func try_recovery() -> void:
	var response := runtime.path_join("player%d.recovery.token" % player)
	if not FileAccess.file_exists(response):
		if Time.get_ticks_msec() - recovery_started > 5000:
			begin_recovery("admission response timeout")
		return
	var token := FileAccess.get_file_as_bytes(response)
	if token.size() != 2048: return
	DirAccess.remove_absolute(response)
	var options := {"game_protocol": "egp-box3d-arena-v4", "simulation_fingerprint": world.get_simulation_fingerprint(), "tick_rate": 60, "max_players": 64, "max_entities": 512, "simulated_latency_ms": latency_ms, "simulated_jitter_ms": jitter_ms, "simulated_loss": loss_percent}
	if net.configure(options) != OK or net.join_token(1000 + player, token, bind_ip) != OK:
		begin_recovery("fresh admission rejected")
		return
	recovering = false
	recoveries += 1
	print("ARENA_RECOVERY_REJOIN fresh token submitted")

func request_action(action: String, interval_ms: int) -> void:
	if Time.get_ticks_msec() - int(action_times.get(action, -10000)) < interval_ms or outgoing_actions.size() >= 8: return
	action_times[action] = Time.get_ticks_msec()
	action_nonce += 1
	outgoing_actions.append([owned, action, action_nonce])

func receive_action(peer: int, args: Array) -> void:
	if args.size() != 3 or not args[0] is int or not args[1] is String or not args[2] is int: return
	var entity: int = args[0]
	if not controls.has(entity) or net.get_entity(entity).get("authority_peer", -1) != peer: return
	if args[1] not in ["pulse", "spawn", "reset"] or args[2] <= int(action_nonces.get(entity, 0)): return
	if action_requests.size() >= 16: return
	action_nonces[entity] = args[2]
	action_requests.append({"entity": entity, "action": args[1]})

func observed_position(entity: int) -> Vector3:
	if pose_history.has(entity) and not pose_history[entity].is_empty(): return pose_history[entity].back().pose.origin
	return targets[entity].origin if targets.has(entity) else Vector3(0, -100, 0)

func think_player() -> Dictionary:
	brain_decisions += 1
	var p := observed_position(owned)
	if elapsed - brain_progress_time > 1.5:
		if brain_progress_time > 0 and p.distance_to(brain_progress_position) < 0.4:
			brain_escape_until = elapsed + 1.0
			brain_escapes += 1
		brain_progress_time = elapsed
		brain_progress_position = p
	var best_score := 100000.0
	var goal := Vector3(cos(elapsed * 0.25 + player) * 6, 0.8, sin(elapsed * 0.25 + player) * 5)
	var avoidance := Vector3.ZERO
	var nearest_threat := 100.0
	var threats := 0
	var obstacle_ahead := false
	brain_state = "seek energy"
	for entity in visuals:
		if entity == owned: continue
		var other := observed_position(entity)
		if other.y < -5: continue
		var kind: int = visual_kinds.get(entity, 0)
		var predicted := other
		if pose_history.has(entity) and not pose_history[entity].is_empty(): predicted += pose_history[entity].back().velocity * 0.3
		var distance := p.distance_to(predicted)
		if kind == 30:
			# Per-client preference spreads the crowd across different objectives.
			var utility := distance + float((entity + player * 3) % 8) * 0.65
			if utility < best_score:
				best_score = utility
				goal = other
				brain_target = entity
		elif kind in [20, 21, 22, 23]:
			nearest_threat = minf(nearest_threat, distance)
			if distance < 4:
				threats += 1
				avoidance += (p - predicted).normalized() * (4 - distance) * 1.5
		elif kind >= 100:
			if distance < 2: avoidance += (p - predicted).normalized() * (2 - distance) * 1.5
		elif kind in [40, 41]:
			if distance < 3: avoidance += (p - predicted).normalized() * (3 - distance)
		elif distance < 1.5:
			obstacle_ahead = true
	var direction := goal - p
	direction.y = 0
	direction = direction.normalized() + avoidance
	if absf(p.x) > 9.5: direction.x -= signf(p.x) * (absf(p.x) - 9.5) * 2
	if absf(p.z) > 6.5: direction.z -= signf(p.z) * (absf(p.z) - 6.5) * 2
	if nearest_threat < 2.5: brain_state = "evade + counterattack"
	elif obstacle_ahead: brain_state = "jump obstacle"
	if elapsed < brain_escape_until:
		brain_state = "escape blocked route"
		direction += Vector3(cos(player * 2.4 + brain_escapes), 0, sin(player * 2.4 + brain_escapes)) * 2
		obstacle_ahead = true
	direction.y = 0
	direction = direction.limit_length()
	return {"direction": Vector2(direction.x, direction.z), "jump": obstacle_ahead or (nearest_threat < 2 and fmod(elapsed + player, 2.5) < 0.1), "pulse": threats >= 2 or nearest_threat < 1.8}

func capture() -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(capture_path)
	print("ARENA_CAPTURED " + capture_path)

func fail(message: String) -> void:
	push_error("ARENA_FAILURE " + message)
	get_tree().quit(1)
