extends Node

const SCHEMA_ID := 53001
const POSE_CHANNEL := 1
const COMMAND_CHANNEL := 0
var physics: EGPBox3DWorld
var peers: Array[SuperposWorld] = []
var sessions: Array[SuperposSession] = []
var handles: Array[Dictionary] = [{}, {}]
var ids: Array[int] = []
var meshes: Array[Dictionary] = [{}, {}]
var targets: Dictionary = {}
var cameras: Array[Camera3D] = []
var tickets: Array[Array] = [[], []]
var baseline := PackedByteArray()
var checkpoint := PackedByteArray()
var checkpoint_hash := ""
var previous_checkpoint := PackedByteArray()
var previous_hash := ""
var sequence := 0
var received_sequence := 0
var snapshots_received := 0
var snapshots_applied := 0
var commands_applied := 0
var rewinds := 0
var last_ack_ms := 0.0
var elapsed := 0.0
var frames := 0
var smoke := false
var capture_path := ""
var smoke_sent := 0
var finishing := false
var failure := ""
var gravity_observed := false
var yaw := 0.18
var distance := 32.0
var height := 19.0
var metrics: Label
var status: Label
var actions: Array[Button] = []
var rng := RandomNumberGenerator.new()

func require(ok: bool, reason: String) -> bool:
	if not ok and failure.is_empty():
		failure = reason
		push_error(reason)
		if smoke:
			get_tree().quit(1)
	return ok

func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg == "--smoke":
			smoke = true
		if arg.begins_with("--capture="):
			capture_path = arg.trim_prefix("--capture=")
	rng.seed = 44017
	_build_interface()
	physics = EGPBox3DWorld.new()
	if not require(physics.configure(60, 4, 1) == OK, "Box3D configuration failed"):
		return
	require(physics.queue_create_box(1, 0, Vector3(0, -0.5, 0), Vector3(10, 0.5, 12), 0) == OK, "Floor command")
	for tower in range(9):
		for level in range(5):
			var id := 10 + tower * 5 + level
			var pos := Vector3((tower % 3 - 1) * 3.0, 0.46 + level * 0.92, 1.0 + floori(float(tower) / 3.0) * 3.0)
			ids.append(id)
			require(physics.queue_create_box(id, 0, pos, Vector3.ONE * 0.45) == OK, "Crate command")
			_add_body_visual(id, pos, false, Color.from_hsv(float(tower) / 12.0 + 0.44, 0.58, 0.82), 0.45)
	for i in range(6):
		var id := 60 + i
		var pos := Vector3(-6 + i * 2.4, 4 + i * 0.8, -4)
		ids.append(id)
		require(physics.queue_create_sphere(id, 0, pos, 0.5) == OK, "Sphere command")
		_add_body_visual(id, pos, true, Color("#ffb65c"), 0.5)
	ids.append(90)
	require(physics.queue_create_sphere(90, 0, Vector3(0, 0.66, -10), 0.65, 2, 4.0) == OK, "Cannon command")
	_add_body_visual(90, Vector3(0, 0.66, -10), true, Color("#f3788c"), 0.65)
	require(physics.apply_queued_commands() == OK, "Physics baseline")
	baseline = physics.capture_snapshot()
	checkpoint = baseline
	checkpoint_hash = physics.get_state_hash()
	previous_checkpoint = baseline
	previous_hash = checkpoint_hash
	require(not baseline.is_empty(), "Local snapshot capture")
	_configure_network()
	_update_cameras()

func _configure_network() -> void:
	var schema := SuperposSchema.new()
	schema.schema_id = SCHEMA_ID
	var field := SuperposField.new()
	field.field_id = 1
	field.field_name = &"arena_poses"
	field.codec_id = 8
	field.max_bytes = 4 + ids.size() * 28
	schema.fields = [field]
	var key := Crypto.new().generate_random_bytes(32)
	var port := 28500 + int(Time.get_ticks_msec() % 12000)
	for index in range(2):
		var world := SuperposWorld.new()
		world.schemas = [schema]
		world.max_objects = 4
		world.automatic_ticks = true
		require(world.configure() == OK, "Superpos world configuration")
		var session := world.get_session()
		var initial: Dictionary = {}
		for id in ids:
			initial[id] = _pose(physics.get_body_state(id))
		var handle := session.spawn_object(SCHEMA_ID, 0, _canonical_frame(initial))
		require(handle != 0, "Canonical arena frame allocation")
		handles[index][0] = handle
		var result := session.configure_udp(index == 0, "127.0.0.1", port + index, "127.0.0.1", port + 1 - index, 53001, 53002, key)
		require(result == OK, "Loopback UDP configuration: " + str(result))
		peers.append(world)
		sessions.append(session)
		add_child(world)

func _pose(state: Dictionary) -> PackedByteArray:
	var pos: Vector3 = state.position
	var rotation: Quaternion = state.rotation
	var values := [pos.x, pos.y, pos.z, rotation.x, rotation.y, rotation.z, rotation.w]
	var bytes := PackedByteArray()
	bytes.resize(28)
	for i in range(7):
		bytes.encode_float(i * 4, values[i])
	return bytes

func _canonical_frame(poses: Dictionary) -> PackedByteArray:
	var frame := PackedByteArray()
	frame.resize(4)
	frame.encode_u32(0, ids.size() * 28)
	for id in ids:
		frame.append_array(poses[id])
	return frame

func _publish(index: int, poses: Dictionary) -> bool:
	var object: Dictionary = sessions[index].read_object(handles[index][0])
	if object.error != OK:
		return false
	var frame := _canonical_frame(poses)
	var packed := "SPGO".to_ascii_buffer()
	packed.resize(12)
	packed.encode_u32(4, 1)
	packed.encode_u32(8, 1)
	packed.append_array(SuperposUInt64.to_bytes(handles[index][0]))
	packed.append_array(SuperposUInt64.to_bytes(object.revision))
	var length := PackedByteArray()
	length.resize(4)
	length.encode_u32(0, frame.size())
	packed.append_array(length)
	packed.append_array(frame)
	return sessions[index].publish_packed(packed) == OK

func _physics_process(delta: float) -> void:
	if not failure.is_empty() or sessions.size() != 2:
		return
	elapsed += delta
	frames += 1
	_retire_tickets(0, POSE_CHANNEL)
	_retire_tickets(1, COMMAND_CHANNEL)
	var pending_ack: Array[Dictionary] = []
	if sessions[0].get_admission_state().ready:
		for _i in range(4):
			var packet := sessions[0].read_packet(COMMAND_CHANNEL)
			if packet.error != OK:
				break
			var bytes: PackedByteArray = packet.payload
			if not require(bytes.size() == 12 and bytes.slice(0, 4).get_string_from_ascii() == "SPIN", "Malformed input packet"):
				return
			var action := bytes.decode_u32(4)
			if not require(action < 4, "Unknown input action"):
				return
			_apply_action(action)
			pending_ack.append(packet)
			# A lease remains readable until acknowledged after the physics step.
			break
	if not require(physics.step_tick(physics.get_tick() + 1) == OK, "Fixed physics step"):
		return
	for packet in pending_ack:
		require(sessions[0].acknowledge_packet(packet.message, packet.binding_generation, COMMAND_CHANNEL) == OK, "Input application acknowledgement")
		commands_applied += 1
	if frames % 120 == 0:
		previous_checkpoint = checkpoint
		previous_hash = checkpoint_hash
		checkpoint = physics.capture_snapshot()
		checkpoint_hash = physics.get_state_hash()
	for id in ids:
		var state := physics.get_body_state(id)
		var q: Quaternion = state.rotation
		meshes[0][id].transform = Transform3D(Basis(q), state.position)
		if id == 65 and state.position.y < 5:
			gravity_observed = true
	if frames % 6 == 0 and sessions[0].get_admission_state().ready and tickets[0].size() < 2:
		_send_snapshot()
	if sessions[1].get_admission_state().ready:
		for _i in range(4):
			var packet := sessions[1].read_packet(POSE_CHANNEL)
			if packet.error != OK:
				break
			if not _receive_snapshot(packet.payload):
				return
			require(sessions[1].acknowledge_packet(packet.message, packet.binding_generation, POSE_CHANNEL) == OK, "Pose application acknowledgement")
	if smoke and sessions[1].get_admission_state().ready:
		if elapsed > 1 and smoke_sent == 0:
			_send_action(0)
			smoke_sent = 1
		elif elapsed > 2 and smoke_sent == 1:
			_send_action(1)
			smoke_sent = 2
		elif elapsed > 4 and smoke_sent == 2:
			_send_action(3)
			smoke_sent = 3
		elif elapsed > 6 and smoke_sent == 3:
			_send_action(2)
			smoke_sent = 4
	if smoke and elapsed >= 8 and not finishing:
		finishing = true
		_finish_smoke()

func _send_snapshot() -> void:
	var poses: Dictionary = {}
	for id in ids:
		poses[id] = _pose(physics.get_body_state(id))
	if not require(_publish(0, poses), "Atomic authority pose publication"):
		return
	sequence += 1
	var bytes := "SPWV".to_ascii_buffer()
	bytes.resize(24)
	bytes.encode_u32(4, 1)
	bytes.encode_u32(8, sequence)
	bytes.encode_u64(12, physics.get_tick())
	bytes.encode_u32(20, ids.size())
	for id in ids:
		var identity := PackedByteArray()
		identity.resize(4)
		identity.encode_u32(0, id)
		bytes.append_array(identity)
		bytes.append_array(poses[id])
	var ticket := sessions[0].enqueue_packet(bytes, POSE_CHANNEL)
	if ticket.error == OK:
		ticket.sent_at = Time.get_ticks_msec()
		tickets[0].append(ticket)

func _receive_snapshot(bytes: PackedByteArray) -> bool:
	if not require(bytes.size() == 24 + ids.size() * 32 and bytes.slice(0, 4).get_string_from_ascii() == "SPWV" and bytes.decode_u32(4) == 1 and bytes.decode_u32(20) == ids.size(), "Snapshot framing"):
		return false
	var incoming := bytes.decode_u32(8)
	if not require(incoming > received_sequence, "Monotonic publication sequence"):
		return false
	var poses: Dictionary = {}
	var decoded: Dictionary = {}
	for i in range(ids.size()):
		var offset := 24 + i * 32
		var id := bytes.decode_u32(offset)
		if not require(id == ids[i], "Stable body identity"):
			return false
		var pose := bytes.slice(offset + 4, offset + 32)
		for n in range(7):
			if not require(is_finite(pose.decode_float(n * 4)), "Finite pose"):
				return false
		var position := Vector3(pose.decode_float(0), pose.decode_float(4), pose.decode_float(8))
		var q := Quaternion(pose.decode_float(12), pose.decode_float(16), pose.decode_float(20), pose.decode_float(24))
		if not require(absf(q.length_squared() - 1.0) < 0.02, "Normalized orientation"):
			return false
		poses[id] = pose
		decoded[id] = Transform3D(Basis(q.normalized()), position)
	if not require(_publish(1, poses), "Atomic receiving pose publication"):
		return false
	if smoke:
		var object := sessions[1].read_object(handles[1][0])
		if not require(object.error == OK and object.canonical == _canonical_frame(poses), "Canonical receive integrity"):
			return false
	for id in ids:
		if not targets.has(id):
			meshes[1][id].transform = decoded[id]
		targets[id] = decoded[id]
	received_sequence = incoming
	snapshots_received += 1
	return true

func _retire_tickets(index: int, channel: int) -> void:
	for i in range(tickets[index].size() - 1, -1, -1):
		var ticket: Dictionary = tickets[index][i]
		var outcome := sessions[index].get_packet_outcome(ticket.message, ticket.binding_generation, channel)
		if outcome.error == OK and outcome.outcome == "Applied":
			require(sessions[index].retire_packet(ticket.message, ticket.binding_generation, channel) == OK, "Ticket retirement")
			if index == 0:
				last_ack_ms = float(Time.get_ticks_msec() - int(ticket.sent_at))
				snapshots_applied += 1
			tickets[index].remove_at(i)
		elif outcome.error == OK and outcome.outcome in ["Failed", "Retired"]:
			require(false, "Delivery failed")

func _send_action(action: int) -> void:
	if sessions.size() != 2 or not sessions[1].get_admission_state().ready or tickets[1].size() >= 4:
		return
	var bytes := "SPIN".to_ascii_buffer()
	bytes.resize(12)
	bytes.encode_u32(4, action)
	bytes.encode_u32(8, frames)
	var ticket := sessions[1].enqueue_packet(bytes, COMMAND_CHANNEL)
	if smoke:
		print("ACTION_ENQUEUE action=", action, " result=", ticket.error)
	if ticket.error == OK:
		tickets[1].append(ticket)

func _apply_action(action: int) -> void:
	match action:
		0:
			var result := physics.queue_body_state(90, 0, Vector3(0, 1.2, -10), Quaternion.IDENTITY, Vector3(0, 2, 27), Vector3.ZERO)
			require(result == OK, "Cannon launch error=" + str(result))
		1:
			for id in ids:
				var state := physics.get_body_state(id)
				var direction: Vector3 = state.position - Vector3(0, 0, 3)
				direction.y = 0
				require(physics.queue_impulse(id, 0, direction.normalized() * 3.0 + Vector3.UP * 6.0) == OK, "Shockwave impulse")
		2:
			physics.clear_pending_commands()
			require(physics.restore_snapshot(baseline) == OK, "Baseline restore")
		3:
			physics.clear_pending_commands()
			require(physics.restore_snapshot(previous_checkpoint) == OK, "Trusted local rewind")
			require(physics.get_state_hash() == previous_hash, "Rewind state hash")
			rewinds += 1

func _process(delta: float) -> void:
	for id in targets:
		var node: MeshInstance3D = meshes[1][id]
		node.transform = node.transform.interpolate_with(targets[id], 1.0 - exp(-delta * 18.0))
	if sessions.size() == 2:
		var ready: bool = sessions[0].get_admission_state().ready and sessions[1].get_admission_state().ready
		status.text = "CONNECTED / AUTHENTICATED UDP" if ready else "CONNECTING TWO PEERS"
		if not failure.is_empty():
			status.text = "STOPPED: " + failure
		for button in actions:
			button.disabled = not ready or not failure.is_empty()
		var wire: Dictionary = sessions[0].get_statistics()
		metrics.text = "%d BODIES    /    60 Hz PHYSICS    /    TICK %d\n%d SNAPSHOTS RECEIVED    /    %d APPLIED    /    ACK %.0f ms\n%d NETWORK COMMANDS    /    %.1f KiB SENT    /    %d LOCAL REWINDS" % [ids.size(), physics.get_tick(), snapshots_received, snapshots_applied, last_ack_ms, commands_applied, float(wire.get("charged_wire_bytes", 0)) / 1024.0, rewinds]

func _finish_smoke() -> void:
	print("SMOKE_DIAGNOSTICS received=", snapshots_received, " applied=", snapshots_applied, " commands=", commands_applied, " rewinds=", rewinds, " gravity=", gravity_observed, " admission=", sessions[0].get_admission_state().ready, "/", sessions[1].get_admission_state().ready, " sent_actions=", smoke_sent)
	if not require(snapshots_received > 30 and snapshots_applied > 30 and commands_applied == 4 and rewinds == 1 and gravity_observed, "Gameplay/network smoke thresholds"):
		return
	if not capture_path.is_empty():
		await RenderingServer.frame_post_draw
		require(get_viewport().get_texture().get_image().save_png(capture_path) == OK, "Rendered capture")
	print("PHYSICS_SUPERPOS_SHOWCASE_PASS bodies=", ids.size(), " received=", snapshots_received, " applied=", snapshots_applied, " commands=", commands_applied, " rewinds=", rewinds)
	get_tree().quit(0)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_RIGHT:
		yaw -= event.relative.x * 0.005
		height = clampf(height + event.relative.y * 0.04, 6, 30)
		_update_cameras()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			distance = maxf(18, distance - 1.5)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			distance = minf(45, distance + 1.5)
		_update_cameras()

func _update_cameras() -> void:
	for camera in cameras:
		camera.position = Vector3(sin(yaw) * distance, height, -cos(yaw) * distance)
		camera.look_at(Vector3(0, 2, 2))

func _material(color: Color, metallic: float = 0.0) -> StandardMaterial3D:
	var result := StandardMaterial3D.new()
	result.albedo_color = color
	result.metallic = metallic
	result.roughness = 0.38
	return result

func _visual(parent: Node3D, pos: Vector3, size: Vector3, color: Color) -> MeshInstance3D:
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	mesh.mesh = box
	mesh.material_override = _material(color)
	parent.add_child(mesh)
	mesh.position = pos
	return mesh

var arenas: Array[Node3D] = []

func _add_body_visual(id: int, pos: Vector3, sphere: bool, color: Color, radius: float) -> void:
	for index in range(2):
		var node := MeshInstance3D.new()
		if sphere:
			var shape := SphereMesh.new()
			shape.radius = radius
			shape.height = radius * 2
			node.mesh = shape
		else:
			var shape := BoxMesh.new()
			shape.size = Vector3.ONE * radius * 2
			node.mesh = shape
		node.material_override = _material(color, 0.12)
		arenas[index].add_child(node)
		node.position = pos
		meshes[index][id] = node

func _label(text: String, size: int, color: Color = Color("#e6edf8")) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label

func _build_interface() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var background := ColorRect.new()
	background.color = Color("#080f1b")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas.add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for edge in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + edge, 24)
	background.add_child(margin)
	var layout := VBoxContainer.new()
	layout.add_theme_constant_override("separation", 14)
	margin.add_child(layout)
	var header := HBoxContainer.new()
	layout.add_child(header)
	var title := _label("EGP  /  PHYSICS PLAYGROUND", 30)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	status = _label("CONNECTING TWO PEERS", 14, Color("#67e6c3"))
	header.add_child(status)
	layout.add_child(_label("Break the stacks. Watch every move arrive on the other side.", 17, Color("#91a4bd")))
	var views := HBoxContainer.new()
	views.size_flags_vertical = Control.SIZE_EXPAND_FILL
	views.add_theme_constant_override("separation", 18)
	layout.add_child(views)
	for index in range(2):
		var panel := VBoxContainer.new()
		panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		views.add_child(panel)
		panel.add_child(_label("01  /  BOX3D AUTHORITY" if index == 0 else "02  /  SUPERPOS PEER", 18, Color("#67e6c3") if index == 0 else Color("#83b8ff")))
		var container := SubViewportContainer.new()
		container.stretch = true
		container.size_flags_vertical = Control.SIZE_EXPAND_FILL
		container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		panel.add_child(container)
		var viewport := SubViewport.new()
		viewport.size = Vector2i(650, 520)
		viewport.own_world_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		container.add_child(viewport)
		var arena := Node3D.new()
		viewport.add_child(arena)
		arenas.append(arena)
		var env := WorldEnvironment.new()
		env.environment = Environment.new()
		env.environment.background_mode = Environment.BG_COLOR
		env.environment.background_color = Color("#101d30")
		env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.environment.ambient_light_color = Color("#a9c2e6")
		env.environment.ambient_light_energy = 0.65
		env.environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
		arena.add_child(env)
		var sun := DirectionalLight3D.new()
		sun.rotation_degrees = Vector3(-50, -25, 0)
		sun.light_energy = 1.8
		sun.shadow_enabled = true
		arena.add_child(sun)
		var camera := Camera3D.new()
		camera.fov = 48
		camera.current = true
		arena.add_child(camera)
		cameras.append(camera)
		_visual(arena, Vector3(0, -0.3, 0), Vector3(20, 0.6, 24), Color("#24354b"))
		for line in range(-10, 11, 2):
			_visual(arena, Vector3(line, 0.004, 0), Vector3(0.025, 0.008, 24), Color("#3f5570"))
		for line in range(-12, 13, 2):
			_visual(arena, Vector3(0, 0.006, line), Vector3(20, 0.008, 0.025), Color("#3f5570"))
		for side in [-1, 1]:
			_visual(arena, Vector3(side * 9.8, 0.08, 0), Vector3(0.12, 0.16, 24), Color("#ffb65c"))
		_visual(arena, Vector3(0, 0.025, -10), Vector3(3, 0.05, 2.8), Color("#435678"))
		panel.add_child(_label("Fixed solver + authoritative commands" if index == 0 else "Authenticated poses + interpolated view", 13, Color("#91a4bd")))
	var controls := HBoxContainer.new()
	controls.add_theme_constant_override("separation", 12)
	layout.add_child(controls)
	var names := ["FIRE CANNON", "SHOCKWAVE", "RESET ARENA", "REWIND PHYSICS"]
	for action in range(4):
		var button := Button.new()
		button.text = names[action]
		button.custom_minimum_size = Vector2(180, 46)
		button.add_theme_font_size_override("font_size", 15)
		button.pressed.connect(_send_action.bind(action))
		controls.add_child(button)
		actions.append(button)
	metrics = _label("", 14, Color("#91a4bd"))
	layout.add_child(metrics)
	layout.add_child(_label("RIGHT-DRAG TO ORBIT  /  SCROLL TO ZOOM     ?     TWO LIVE LOOPBACK ASSOCIATIONS", 12, Color("#647d9b")))

func _exit_tree() -> void:
	for peer in peers:
		peer.close()
