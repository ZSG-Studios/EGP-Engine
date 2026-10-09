extends Node

const SCHEMA := 53101
const INPUT := 0
const STATE := 1
const PLAYER_BASE := 1000
const PROP_BASE := 2000
const COLORS := [Color("55ead0"), Color("72baff"), Color("b39aff"), Color("ffbc68"), Color("f87999")]
var config: Dictionary
var role := ""
var connections: Array[Dictionary] = []
var physics: EGPBox3DWorld
var schema: SuperposSchema
var now := 0.0
var frame := 0
var failed := ""
var physics_ms: Array[float] = []
var draw_timer := 0.0
var camera: Camera3D
var visuals: Dictionary = {}
var visual_targets: Dictionary = {}
var hud: Label
var title: Label
var yaw := 0.0
var zoom := 20.0
var jump_requested := false
var pulse_requested := false
var live_report: Dictionary = {}
var capture_saved := false
var human_profile := 0
var human_offline_until := 0.0

func check(ok: bool, reason: String) -> bool:
	if not ok and failed.is_empty():
		failed = reason
		push_error(reason)
		get_tree().quit(1)
	return ok

func _ready() -> void:
	# Windows pipes return arbitrary chunks, not complete lines. Read through EOF.
	var encoded := ""
	for _line in range(256):
		var chunk := OS.read_string_from_stdin(4096)
		if chunk.is_empty():
			break
		encoded+=chunk
	encoded=encoded.replace("\n","").replace("\r","").trim_suffix("END")
	var parsed = JSON.parse_string(Marshalls.base64_to_utf8(encoded))
	if not check(parsed is Dictionary, "Bootstrap must arrive through private stdin"):
		return
	config = parsed
	role = config.role
	schema = SuperposSchema.new()
	schema.schema_id = SCHEMA
	var field := SuperposField.new()
	field.field_id = 1
	field.field_name = &"interest_frame"
	field.codec_id = 8
	field.max_bytes = 1024
	schema.fields = [field]
	if role == "server":
		_create_physics()
	for spec in config.peers:
		var id: int = spec.id
		var c := {"id":id, "spec":spec, "session":null, "handle":0, "generation":1, "ready":false, "ever_ready":false, "last_ready":false, "recoveries":0, "transport_errors":0, "last_error":0, "ticket":{}, "sent":0, "applied":0, "received":0, "sequence":0, "accepted_sequence":0, "input_sequence":0, "last_input":-100.0, "last_state":-100.0, "direction":Vector3.ZERO, "position":_spawn(id), "predicted":_spawn(id), "neighbors":{}, "goal":id % 4, "score":0, "cooldown":0.0, "ack_ms":0.0, "acks":[], "errors":[], "distance":0.0, "last_position":_spawn(id), "last_send":-100.0, "period":0.1, "flags":0, "max_pending":0, "rejected":0}
		connections.append(c)
		_open(c)
	if role == "human":
		_build_view()
	print("LAB_STARTED role=", role, " independent_sessions=", connections.size())

func _spawn(id: int) -> Vector3:
	if id == 100:
		return Vector3(0, 1.0, -26)
	return Vector3((id % 10 - 4.5) * 5.1, 1.0, (floori(float(id) / 10) - 4.5) * 5.1)

func _goal(index: int) -> Vector3:
	return [Vector3(-21,0,-21), Vector3(21,0,-21), Vector3(21,0,21), Vector3(-21,0,21)][index % 4]

func _create_physics() -> void:
	physics = EGPBox3DWorld.new()
	check(physics.configure(60, 4, 1) == OK, "Server Box3D configure")
	check(physics.queue_create_box(1,0,Vector3(0,-0.5,0),Vector3(35,0.5,35),0) == OK,"Floor")
	for id in range(config.total):
		check(physics.queue_create_sphere(PLAYER_BASE+id,0,_spawn(id),0.45,2,1.5) == OK,"Player collider")
	for id in range(40):
		var p := Vector3((id % 8-3.5)*3.1,0.6,(floori(float(id)/8)-2)*3.1)
		check(physics.queue_create_box(PROP_BASE+id,0,p,Vector3.ONE*0.55,2,0.6) == OK,"Physics obstacle")
	check(physics.apply_queued_commands() == OK,"Initial server world")

func _open(c: Dictionary) -> void:
	if not c.has("wire_base"):
		c.wire_base=0
	if c.session != null:
		c.wire_base+=int(c.session.get_statistics().get("charged_wire_bytes",0))
		check(c.session.close_checked() == OK,"Close previous association")
	var session := SuperposSession.new()
	check(session.configure([schema],1,c.generation,0,1048576) == OK,"Canonical session configure")
	var empty := PackedByteArray([0,0,0,0])
	empty.resize(1024)
	c.handle = session.spawn_object(SCHEMA,0,empty)
	check(c.handle != 0,"Canonical interest object")
	var spec: Dictionary = c.spec
	var server := role == "server"
	var local_port: int = config.port_base + (0 if server else 1000) + c.id
	var remote_port: int = config.port_base + (3000 if server else 2000) + c.id
	var local_ip: String = config.server_ip if server else "127.0.0.1"
	var remote_ip: String = config.client_ip if server else "127.0.0.1"
	var key := Marshalls.base64_to_raw(spec.key)
	check(session.configure_udp(server,local_ip,local_port,remote_ip,remote_port,53100+c.id,60000+c.id,key) == OK,"UDP association provision")
	c.session = session
	c.ticket = {}
	c.ready = false
	c.last_ready = false
	c.last_error = 0
	c.last_send = now
	c.last_state = now
	c.accepted_sequence = 0

func _publish(c: Dictionary, bytes: PackedByteArray) -> bool:
	var current: Dictionary = c.session.read_object(c.handle)
	if not check(current.error == OK,"Canonical interest read"):
		return false
	var canonical := PackedByteArray()
	canonical.resize(4)
	canonical.encode_u32(0,bytes.size())
	canonical.append_array(bytes)
	canonical.resize(1024)
	var packed := "SPGO".to_ascii_buffer()
	packed.resize(12)
	packed.encode_u32(4,1)
	packed.encode_u32(8,1)
	packed.append_array(SuperposUInt64.to_bytes(c.handle))
	packed.append_array(SuperposUInt64.to_bytes(current.revision))
	var length := PackedByteArray()
	length.resize(4)
	length.encode_u32(0,canonical.size())
	packed.append_array(length)
	packed.append_array(canonical)
	return check(c.session.publish_packed(packed) == OK,"Atomic canonical interest publication")

func _physics_process(delta: float) -> void:
	if not failed.is_empty():
		return
	now = Time.get_unix_time_from_system() - float(config.start_unix)
	frame += 1
	var epochs: Dictionary = {}
	if frame % 30 == 0:
		var control := FileAccess.open(config.control,FileAccess.READ)
		if control:
			var decoded = JSON.parse_string(control.get_as_text())
			if decoded is Dictionary:
				epochs = decoded
	for c in connections:
		# The test supervisor synchronizes retries; old datagrams stay in old epochs.
		var desired := int(epochs.get(str(c.id),c.generation))
		if desired > c.generation:
			c.generation = desired
			_open(c)
		var result: int = c.session.advance_tick()
		var admission: Dictionary = c.session.get_admission_state()
		c.ready = admission.ready
		if result != OK and result != ERR_BUSY and result != ERR_UNCONFIGURED and result != c.last_error:
			c.transport_errors += 1
			c.last_error = result
		if c.ready:
			if not c.ever_ready:
				c.ever_ready = true
			elif not c.last_ready:
				c.recoveries += 1
			_retire(c, STATE if role == "server" else INPUT)
		c.last_ready = c.ready
	if role == "server":
		_server_step()
	else:
		_client_step(delta)
	if frame % 30 == 0:
		_write_telemetry()
	if now >= float(config.duration) + 3:
		_write_telemetry()
		print("LAB_FINISHED role=",role," sessions=",connections.size())
		get_tree().quit(0)

func _retire(c: Dictionary, channel: int) -> void:
	if c.ticket.is_empty():
		return
	var ticket: Dictionary = c.ticket
	var outcome: Dictionary = c.session.get_packet_outcome(ticket.message,ticket.binding_generation,channel)
	if outcome.error == OK and outcome.outcome == "Applied":
		check(c.session.retire_packet(ticket.message,ticket.binding_generation,channel) == OK,"Explicit sender retirement")
		var delay := float(Time.get_ticks_msec()-int(ticket.at))
		c.ack_ms = delay if c.ack_ms == 0 else lerpf(c.ack_ms,delay,0.2)
		c.acks.append(delay)
		if c.acks.size() > 500:
			c.acks.pop_front()
		c.applied += 1
		c.ticket = {}
		# Independent measured-RTT pacing; no queue of obsolete movement intents.
		c.period = clampf(c.ack_ms / 1000.0 * 1.1,0.1 if role == "server" else 0.05,0.6)

func _enqueue(c: Dictionary, bytes: PackedByteArray, channel: int) -> void:
	if not c.ready or not c.ticket.is_empty() or now-c.last_send < c.period:
		return
	var ticket: Dictionary = c.session.enqueue_packet(bytes,channel)
	if ticket.error == OK:
		ticket.at = Time.get_ticks_msec()
		c.ticket = ticket
		c.sent += 1
		c.last_send = now
		c.max_pending = maxi(c.max_pending,1)

func _server_step() -> void:
	var acknowledgements: Array[Dictionary] = []
	for c in connections:
		if c.ready:
			var packet: Dictionary = c.session.read_packet(INPUT)
			if packet.error == OK:
				var bytes: PackedByteArray = packet.payload
				_accept_input(c,bytes)
				acknowledgements.append({"c":c,"packet":packet})
		var state := physics.get_body_state(PLAYER_BASE+c.id)
		var velocity: Vector3 = state.linear_velocity
		var direction: Vector3 = c.direction if now-c.last_input < 1.25 else Vector3.ZERO
		var p: Vector3 = state.position
		if absf(p.x)>31 or absf(p.z)>31:
			direction = -Vector3(p.x,0,p.z).normalized()
		check(physics.queue_linear_velocity(PLAYER_BASE+c.id,0,Vector3(direction.x*6,velocity.y,direction.z*6)) == OK,"Server bounded movement")
		if c.flags & 1 and p.y<0.7 and now>c.cooldown:
			check(physics.queue_impulse(PLAYER_BASE+c.id,1,Vector3.UP*2.3) == OK,"Server jump")
			c.cooldown = now+0.75
		if c.flags & 2 and now>c.cooldown:
			for prop in range(40):
				var body := physics.get_body_state(PROP_BASE+prop)
				var away: Vector3 = body.position-p
				if away.length()<7:
					check(physics.queue_impulse(PROP_BASE+prop,c.id+1,away.normalized()*1.0+Vector3.UP*0.8) == OK,"Authoritative prop shockwave")
			c.cooldown = now+2.0
		c.flags = 0
	var started := Time.get_ticks_usec()
	check(physics.step_tick(physics.get_tick()+1) == OK,"Dedicated Box3D fixed step")
	physics_ms.append(float(Time.get_ticks_usec()-started)/1000.0)
	if physics_ms.size()>2000:
		physics_ms.pop_front()
	for item in acknowledgements:
		var c: Dictionary = item.c
		var packet: Dictionary = item.packet
		check(c.session.acknowledge_packet(packet.message,packet.binding_generation,INPUT) == OK,"Controls acknowledged after native physics step")
	var states: Dictionary = {}
	for c in connections:
		var state := physics.get_body_state(PLAYER_BASE+c.id)
		c.position = state.position
		c.distance += c.position.distance_to(c.last_position)
		c.last_position = c.position
		if Vector2(c.position.x-_goal(c.goal).x,c.position.z-_goal(c.goal).z).length()<2:
			c.score += 1
			c.goal = (c.goal+1) % 4
		states[c.id] = state
	for prop in range(40):
		states[PROP_BASE+prop] = physics.get_body_state(PROP_BASE+prop)
	for c in connections:
		if c.ready and c.ticket.is_empty() and now-c.last_send>=c.period:
			var candidates: Array = states.keys()
			candidates.erase(c.id)
			candidates.sort_custom(func(a,b): return states[a].position.distance_squared_to(c.position)<states[b].position.distance_squared_to(c.position))
			var interest: Array = [c.id]
			interest.append_array(candidates.slice(0,31))
			var bytes := "S100".to_ascii_buffer()
			bytes.resize(24)
			c.sequence += 1
			bytes.encode_u32(4,c.sequence)
			bytes.encode_u32(8,physics.get_tick())
			bytes.encode_u32(12,c.input_sequence)
			bytes.encode_u32(16,c.score*4+c.goal)
			bytes.encode_u32(20,interest.size())
			for id in interest:
				var record := PackedByteArray()
				record.resize(24)
				var state: Dictionary = states[id]
				record.encode_u32(0,id)
				var p: Vector3 = state.position
				var v: Vector3 = state.linear_velocity
				for n in range(5):
					record.encode_float(4+n*4,[p.x,p.y,p.z,v.x,v.z][n])
				bytes.append_array(record)
			if _publish(c,bytes):
				_enqueue(c,bytes,STATE)

func _accept_input(c: Dictionary, bytes: PackedByteArray) -> void:
	if bytes.size()!=20 or bytes.slice(0,4).get_string_from_ascii()!="C100":
		c.rejected+=1
		return
	var incoming := bytes.decode_u32(4)
	var x := bytes.decode_float(8)
	var z := bytes.decode_float(12)
	var flags := bytes.decode_u32(16)
	if incoming<=c.input_sequence or not is_finite(x) or not is_finite(z) or absf(x)>1.001 or absf(z)>1.001 or flags>3:
		c.rejected+=1
		return
	c.input_sequence=incoming
	c.direction=Vector3(x,0,z).limit_length(1.0)
	c.flags=flags
	c.last_input=now
	c.received+=1

func _client_step(delta: float) -> void:
	for c in connections:
		if c.ready:
			var packet: Dictionary = c.session.read_packet(STATE)
			if packet.error == OK:
				var bytes: PackedByteArray = packet.payload
				if not check(bytes.size()>=48 and bytes.slice(0,4).get_string_from_ascii()=="S100","State framing"):
					return
				var count := bytes.decode_u32(20)
				var seq := bytes.decode_u32(4)
				if not check(count>=1 and count<=32 and bytes.size()==24+count*24 and seq>c.accepted_sequence and bytes.decode_u32(24)==c.id,"Bounded interest / own identity / sequence"):
					return
				var neighbors: Dictionary = {}
				for n in range(count):
					var offset := 24+n*24
					var id := bytes.decode_u32(offset)
					for f in range(5):
						if not check(is_finite(bytes.decode_float(offset+4+f*4)),"Finite server state"):
							return
					if not check(not neighbors.has(id) and (id<config.total or (id>=PROP_BASE and id<PROP_BASE+40)),"Interest identity uniqueness"):
						return
					neighbors[id] = Vector3(bytes.decode_float(offset+4),bytes.decode_float(offset+8),bytes.decode_float(offset+12))
				if not _publish(c,bytes):
					return
				var p: Vector3 = neighbors[c.id]
				c.errors.append(c.predicted.distance_to(p))
				if c.errors.size()>500:
					c.errors.pop_front()
				c.position = p
				c.predicted = c.predicted.lerp(p,0.7)
				c.neighbors = neighbors
				c.accepted_sequence = seq
				c.last_state = now
				c.goal = bytes.decode_u32(16) % 4
				c.score = floori(float(bytes.decode_u32(16))/4.0)
				c.received += 1
				check(c.session.acknowledge_packet(packet.message,packet.binding_generation,STATE) == OK,"Validated state application receipt")
		var direction := Vector3.ZERO
		var flags := 0
		if role == "human":
			direction = Vector3(float(Input.is_physical_key_pressed(KEY_D))-float(Input.is_physical_key_pressed(KEY_A)),0,float(Input.is_physical_key_pressed(KEY_S))-float(Input.is_physical_key_pressed(KEY_W)))
			direction = direction.rotated(Vector3.UP,yaw).limit_length(1.0)
			flags = (1 if jump_requested else 0) | (2 if pulse_requested else 0)
		else:
			# Each brain sees its own delayed AOI, never the server's diagnostic world.
			var goal := _goal(c.goal)
			var position: Vector3 = c.position
			direction = Vector3(goal.x-position.x,0,goal.z-position.z).normalized()
			var avoidance := Vector3.ZERO
			for id in c.neighbors:
				if id == c.id:
					continue
				var away: Vector3 = position-c.neighbors[id]
				away.y = 0
				if away.length_squared()<6.25:
					avoidance += away.normalized()/maxf(away.length(),0.3)
			direction = (direction+avoidance*0.65).limit_length(1.0)
			if now-c.last_state>1.25:
				direction *= 0.25
			if frame % (180+c.id%67)==0:
				flags = 1
			if c.id%9==0 and frame%(360+c.id)==0:
				flags |= 2
		c.direction = direction
		c.predicted += direction*6*delta
		if c.ready and c.ticket.is_empty() and now-c.last_send>=c.period:
			c.sequence += 1
			var bytes := "C100".to_ascii_buffer()
			bytes.resize(20)
			bytes.encode_u32(4,c.sequence)
			bytes.encode_float(8,direction.x)
			bytes.encode_float(12,direction.z)
			bytes.encode_u32(16,flags)
			_enqueue(c,bytes,INPUT)
			if role=="human":
				jump_requested=false
				pulse_requested=false

func _write_telemetry() -> void:
	var rows: Array = []
	for c in connections:
		var stats: Dictionary = c.session.get_statistics()
		rows.append({"id":c.id,"ready":c.ready,"terminal":c.session.get_state()=="NetworkFailed","ever_ready":c.ever_ready,"generation":c.generation,"recoveries":c.recoveries,"transport_errors":c.transport_errors,"sent":c.sent,"applied":c.applied,"received":c.received,"ack_ms":c.ack_ms,"ack_p95":_percentile(c.acks,0.95),"error_p95":_percentile(c.errors,0.95),"score":c.score,"position":[c.position.x,c.position.y,c.position.z],"distance":c.distance,"max_pending":c.max_pending,"send_period":c.period,"wire_bytes":c.wire_base+int(stats.get("charged_wire_bytes",0)),"rejected":c.rejected})
	var report := {"role":role,"start_unix":config.start_unix,"now":now,"failed":failed,"pid":OS.get_process_id(),"rows":rows,"physics_p95_ms":_percentile(physics_ms,0.95),"frames":frame}
	if role=="server":
		var props: Array = []
		for id in range(40):
			var p: Vector3 = physics.get_body_state(PROP_BASE+id).position
			props.append([p.x,p.y,p.z])
		report.props = props
	var file := FileAccess.open(config.telemetry+".tmp",FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(report))
		file.close()
		DirAccess.rename_absolute(config.telemetry+".tmp",config.telemetry)

func _percentile(values: Array, percentile: float) -> float:
	if values.is_empty():
		return 0
	var sorted := values.duplicate()
	sorted.sort()
	return float(sorted[mini(sorted.size()-1,floori(float(sorted.size()-1)*percentile))])

func _build_view() -> void:
	var world := Node3D.new()
	add_child(world)
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode=Environment.BG_COLOR
	settings.background_color=Color("111e30")
	settings.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color=Color("adc7ec")
	settings.ambient_light_energy=0.6
	environment.environment=settings
	world.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees=Vector3(-60,-30,0)
	light.shadow_enabled=true
	light.light_energy=1.3
	world.add_child(light)
	_box(world,Vector3(0,-0.1,0),Vector3(70,0.2,70),Color("192c42"))
	for x in range(-35,36,5):
		_box(world,Vector3(x,0.015,0),Vector3(0.025,0.025,70),Color("34536d"))
		_box(world,Vector3(0,0.015,x),Vector3(70,0.025,0.025),Color("34536d"))
	for id in range(4):
		_box(world,_goal(id)+Vector3.UP*0.1,Vector3(4,0.2,4),COLORS[id])
		_box(world,_goal(id)+Vector3.UP*2.5,Vector3(0.2,5,0.2),COLORS[id])
	camera=Camera3D.new()
	camera.fov=65
	world.add_child(camera)
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var panel := ColorRect.new()
	panel.color=Color(0.025,0.035,0.06,0.92)
	panel.position=Vector2(20,20)
	panel.size=Vector2(710,185)
	canvas.add_child(panel)
	title=Label.new()
	title.position=Vector2(40,32)
	title.add_theme_font_size_override("font_size",25)
	title.text="EGP / 100 BOTS + YOU / REMOTE SERVER"
	canvas.add_child(title)
	hud=Label.new()
	hud.position=Vector2(40,75)
	hud.add_theme_font_size_override("font_size",17)
	canvas.add_child(hud)
	var controls := Label.new()
	controls.position=Vector2(25,850)
	controls.add_theme_font_size_override("font_size",19)
	controls.text="WASD MOVE   /   SPACE JUMP   /   F PHYSICS SHOCKWAVE   /   RIGHT-DRAG CAMERA   /   WHEEL ZOOM"
	canvas.add_child(controls)
	var network_controls := Label.new()
	network_controls.position=Vector2(25,815)
	network_controls.add_theme_font_size_override("font_size",17)
	network_controls.text="YOUR NETWORK: 1 FIBRE / 2 BROADBAND / 3 WIFI / 4 MOBILE / 5 POOR   |   O: 6-SECOND OUTAGE"
	canvas.add_child(network_controls)
	for id in range(config.total):
		var mesh := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius=0.45
		sphere.height=0.9
		mesh.mesh=sphere
		mesh.material_override=_material(Color.WHITE if id==100 else COLORS[mini(4,floori(float(id)/20.0))])
		world.add_child(mesh)
		mesh.visible=false
		visuals[id]=mesh
	for id in range(40):
		visuals[PROP_BASE+id]=_box(world,Vector3.ZERO,Vector3.ONE*1.1,Color("ffc170"))
		visuals[PROP_BASE+id].visible=false

func _material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color=color
	material.roughness=0.6
	return material

func _box(parent: Node3D,p: Vector3,size: Vector3,color: Color) -> MeshInstance3D:
	var mesh := MeshInstance3D.new()
	var shape := BoxMesh.new()
	shape.size=size
	mesh.mesh=shape
	mesh.material_override=_material(color)
	parent.add_child(mesh)
	mesh.position=p
	return mesh

func _process(delta: float) -> void:
	if role!="human" or connections.is_empty():
		return
	var c: Dictionary = connections[0]
	for id in visuals:
		var mesh: MeshInstance3D = visuals[id]
		mesh.visible=c.neighbors.has(id)
		if mesh.visible:
			mesh.position=mesh.position.lerp(c.predicted if id==c.id else c.neighbors[id],1-exp(-delta*14))
	camera.position=c.predicted+Vector3(sin(yaw)*zoom,zoom*0.65,cos(yaw)*zoom)
	camera.look_at(c.predicted)
	draw_timer+=delta
	if draw_timer>0.5:
		draw_timer=0
		var file:=FileAccess.open(config.monitor,FileAccess.READ)
		if file:
			var report=JSON.parse_string(file.get_as_text())
			if report is Dictionary:
				live_report=report
	hud.text="%s   /   %.0f ms APPLICATION ACK   /   %d DELIVERIES\nNEARBY ENTITIES %d   /   SERVER: build PC over WireGuard\nBOT ADMISSION %d / 100   /   TEST PHASE: %s\nYOUR PROFILE: %s   /   %s" % ["AUTHENTICATED" if c.ready else "CONNECTING",c.ack_ms,c.score,c.neighbors.size(),int(live_report.get("bots_ready",0)),live_report.get("phase","WARMUP"),["FIBRE","BROADBAND","WIFI","MOBILE","POOR"][human_profile],live_report.get("cohort_summary","FIVE INDEPENDENT BOT COHORTS")]
	if now>=12 and not capture_saved:
		capture_saved=true
		_capture_view()

func _capture_view() -> void:
	await RenderingServer.frame_post_draw
	var path: String = config.telemetry.get_base_dir()+"/playable-client.png"
	check(get_viewport().get_texture().get_image().save_png(path)==OK,"Playable view capture")

func _unhandled_input(event: InputEvent) -> void:
	if role!="human":
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode==KEY_SPACE:
			jump_requested=true
		if event.physical_keycode==KEY_F:
			pulse_requested=true
		if event.physical_keycode>=KEY_1 and event.physical_keycode<=KEY_5:
			human_profile=event.physical_keycode-KEY_1
			_write_human_network()
		if event.physical_keycode==KEY_O:
			human_offline_until=Time.get_unix_time_from_system()+6.0
			_write_human_network()
	if event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_RIGHT:
		yaw-=event.relative.x*0.006
	if event is InputEventMouseButton and event.pressed:
		if event.button_index==MOUSE_BUTTON_WHEEL_UP:
			zoom=maxf(9,zoom-2)
		elif event.button_index==MOUSE_BUTTON_WHEEL_DOWN:
			zoom=minf(42,zoom+2)

func _write_human_network() -> void:
	var path: String = config.telemetry.get_base_dir()+"/human-network.json"
	var file := FileAccess.open(path+".tmp",FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({"profile":human_profile,"offline_until":human_offline_until}))
		file.close()
		DirAccess.rename_absolute(path+".tmp",path)

func _exit_tree() -> void:
	for c in connections:
		if c.session!=null:
			c.session.close_checked()
