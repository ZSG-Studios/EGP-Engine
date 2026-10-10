extends Node

const SCHEMA := 53101
const INPUT := 0
const STATE := 1
const EXHIBIT := 2
# Independent reliable lanes let several fresh frames be in flight instead of one per round trip.
const INPUT_LANES := [0,6,7]
const STATE_LANES := [1,3,4,5]
# The human's slot (after the bots); set from the launch configuration.
var HUMAN := 100
# Superpos sends at most one paced datagram per session pump, and every message, receipt,
# probe and ACK is its own datagram. Rates and lanes were swept on the local 101-stream lab:
# two state lanes plus one input lane at 15 Hz is the fastest stall-free human setting;
# more in flight saturates the pump budget and stalls the whole association for seconds.
const HUMAN_STATE_PERIOD := 0.066
const HUMAN_INPUT_PERIOD := 0.066
const HUMAN_EXHIBIT_PERIOD := 0.25
const BOT_PERIOD := 0.15
const HUMAN_LANES := 2
const HUMAN_INPUT_LANES := 1
const BOT_LANES := 1
const STATE_HEADER := 32
# 32-byte header + 23 x 36-byte records = 860 bytes: one native 894-byte fragment.
const STATE_ENTITIES := 23
# Bot brains avoid neighbours within a few metres; their frames carry the nearest 10.
const BOT_ENTITIES := 10
const INPUT_HISTORY := 120
const INPUT_RECORD := 6
const INPUT_BATCH := 24
const PLAYER_BASE := 1000
const PROP_BASE := 2000
const SIMULATION := preload("res://simulation_world.gd")
const EXHIBIT_VIEW := preload("res://exhibit_view.gd")
const CHARACTER_VIEW := preload("res://character_view.gd")
const PLAYGROUND := preload("res://playground.gd")
const GAMEPLAY := preload("res://gameplay.gd")
# Deterministic mode: the human client runs the same EGPBox3DWorld from the relayed
# Superpos lockstep command stream. Lane 0 carries redundant unreliable inputs, lane 9
# unreliable command batches, lane 10 the reliable join keyframe.
const COMMAND_LANE := 9
const KEYFRAME_LANE := 10
# Commands use plain unreliable delivery: a latest-wins lane would let a newer batch
# replace an unsent one and open a gap in the incremental command stream.
const DETERMINISTIC_MODES := [3,0,0,0,0,0,0,0,0,3,0]
const KEYFRAME_CHUNK := 60000
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
var controls_label: Label
# F3: full network/simulation details; F1: full controls.
var debug_detail := false
var show_controls := false
# Backing panel, resized to the debug text after each refresh.
var hud_panel: ColorRect
var title: Label
var yaw := 0.0
var zoom := 20.0
var jump_requested := false
var pulse_requested := false
var live_report: Dictionary = {}
# Server: one single-port UDP socket carries every client association.
var udp_listener: SuperposUdpListener
var capture_saved := false
var human_profile := 0
var human_offline_until := 0.0
var prop_states: Dictionary = {}
var simulation: RefCounted
var exhibit: Node3D
var human_stance := 0
var respawn_requested := false
var interact_requested := false
var animation_counts: Dictionary = {}
var frame_times: Array[float] = []
var process_times: Array[float] = []
var gpu_times: Array[float] = []
var step_intervals: Array[float] = []
var last_step_usec := 0
var last_frame_usec := 0
var network_times: Array[float] = []
var application_times: Array[float] = []
var control_times: Array[float] = []
var replication_times: Array[float] = []
var publish_times: Array[float] = []
var publish_usec := 0
var diagnostics: Array = []
var movable: Array = []
var sim_epoch := -1.0
var section_ms := {"tick":[],"render":[],"animate":[],"animated":[],"engine_process":[],"engine_physics":[]}
var gameplay: RefCounted
var keyframe_cache := {}
var telemetry_cache := {}
var live_last_time := 0.0
var live_last_frame := 0
var input_recording := PackedByteArray()
var input_recorded := false
# Starved ticks hold stance and facing but never invent movement or actions; a
# catch-up tick keeps the one-shot actions (jump, pulse, respawn, interact) of
# every input it consumed.
var HOLD_MASK := PackedByteArray([0xFF,0xFC,0x01,0xFF,0xFF])
var MERGE_MASK := PackedByteArray([0,0x03,0x0C,0,0])
# Server: clients whose join (or rejoin) keyframe is not finished.
var joining: Array = []
var pump_profile := [0,0,0]
var frame_physics_usec := 0
# Start of the current rendered frame (deterministic catch-up budget).
var det_frame_started := 0
var det_frame_fresh := true
var frame_physics_steps := 0
var last_process_usec := 0
# Bot processes share one deterministic world, advanced from one bot's own stream,
# only so their brains can see positions; every bot still runs its own stream.
var world_feed := -1
var bot_positions := {}
var deterministic := false
var lockstep_server: Object
var lockstep_client: Object
var det := {"loaded":false,"chunks":{},"chunk_id":-1,"tick":0,"target":3,"playing":false,"starved":0,"calm":0,"prev":{},"curr":{},"last_processed":-1,"keyframes":0,"advanced":0,"lag":[],"buffered":[],"step_ms":[],"render_ids":PackedInt64Array(),"render_keys":[]}


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
	HUMAN = int(config.get("human_id", int(config.total) - 1))
	GAMEPLAY.configure_players(int(config.total))
	deterministic = bool(config.get("deterministic", true)) and ClassDB.class_exists("SuperposLockstepServer")
	if deterministic and role == "human":
		lockstep_client = ClassDB.instantiate("SuperposLockstepClient")
		check(lockstep_client.configure(GAMEPLAY.INPUT_BYTES, config.total, 512) == OK, "Lockstep client")
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
		var c := {"id":id, "spec":spec, "session":null, "handle":0, "generation":1, "ready":false, "ever_ready":false, "last_ready":false, "recoveries":0, "transport_errors":0, "last_error":0, "ticket":{}, "sent":0, "applied":0, "received":0, "sequence":0, "accepted_sequence":0, "input_sequence":0, "last_input":-100.0, "last_state":-100.0, "direction":Vector3.ZERO, "position":_spawn(id), "predicted":_spawn(id), "neighbors":{}, "goal":id % 8, "score":0, "cooldown":0.0, "ack_ms":0.0, "acks":[], "errors":[], "distance":0.0, "last_position":_spawn(id), "last_send":-100.0, "period":(_tuning("human_state_period",HUMAN_STATE_PERIOD) if role=="server" else _tuning("human_input_period",HUMAN_INPUT_PERIOD)) if id==HUMAN else _tuning("bot_period",BOT_PERIOD), "flags":0, "max_pending":0, "rejected":0, "stale":0}
		if deterministic and role == "bots":
			# Each bot is an independent deterministic client: its own input history,
			# command decoder and keyframe join, exactly like a real player.
			c.lockstep = ClassDB.instantiate("SuperposLockstepClient")
			check(c.lockstep.configure(GAMEPLAY.INPUT_BYTES, config.total, 256) == OK, "Bot lockstep client")
			c.kf = {"id":-1,"chunks":{},"loaded":false,"keyframes":0,"advanced":0}
		connections.append(c)
		_open(c)
	if role == "human":
		_build_view()
	print("LAB_STARTED role=", role, " independent_sessions=", connections.size())

func _spawn(id: int) -> Vector3:
	return GAMEPLAY.spawn(id)

func _goal(index: int) -> Vector3:
	return [Vector3(-24,0,-24),Vector3(-13,0,-11),Vector3(24,0,-24),Vector3(13,0,-11),Vector3(24,0,24),Vector3(13,0,11),Vector3(-24,0,24),Vector3(-13,0,11)][index % 8]

func _create_physics() -> void:
	# The authoritative world and every gameplay rule live in gameplay.gd, shared
	# bit-for-bit with deterministic clients.
	gameplay=GAMEPLAY.new()
	check(gameplay.create(config.total,int(_tuning("physics_workers",1))),"Deterministic gameplay world")
	physics=gameplay.world
	simulation=gameplay.simulation
	if deterministic:
		lockstep_server=ClassDB.instantiate("SuperposLockstepServer")
		check(lockstep_server.configure(GAMEPLAY.INPUT_BYTES,config.total,{"command_history_ticks":int(_tuning("command_history_ticks",2048)),"stream_resync_backlog_bytes":int(_tuning("resync_backlog_bytes",262144))})==OK,"Lockstep server")

func _open(c: Dictionary) -> void:
	if not c.has("sprinting"):
		c.sprinting=false
		c.pose_until=-100.0
		c.motions={}
		c.facing=0.0
		c.stance=0
		c.collider_height=0.55
		c.exhibit_received=0
		c.last_exhibit=-1
		c.activities={}
		c.packet_cycle=0
		c.tickets=[]
		c.last_exhibit_send=-100.0
		c.server_time=0.0
		c.own_velocity=Vector3.ZERO
		c.snapshots=[]
		c.state_ages=[]
		c.state_intervals=[]
		c.last_delivery=-1.0
		c.state_lane_cursor=0
		c.interest=[]
		c.next_interest=-100.0
		c.interact_until=-100.0
		c.lags=[]
		c.samples={}
		c.present_rotation={}
		c.clock_base=0.0
		c.render_delay=0.15
		# Server-side human input playout.
		c.received_tick=-1
		c.processed_tick=0
		c.next_tick=-1
		c.input_queue={}
		c.playing=false
		c.buffer_target=12
		c.starvations=0
		c.calm_since=0.0
		c.arrival_gaps=[]
		c.last_arrival_frame=0
		c.buffer_depths=[]
		# Client-side human prediction and reconciliation.
		c.ctick=0
		c.history={}
		c.pending_inputs=[]
		c.server_received_tick=-1
		c.server_processed_tick=-1
		c.visual_offset=Vector3.ZERO
		c.prev_predicted=c.predicted
		c.render_y=c.predicted.y
		c.corrections=0
		c.reconciled=false
	if c.generation>1:
		# A fresh epoch is a new association: restart its protocol state cleanly so
		# the client joins by a new keyframe instead of waiting on the old stream.
		c.tickets=[]
		c.unreliable=[]
		if role=="server":
			c.keyframe_tick=0
			c.kf_next=0
			c.kf_count=0
			c.resync=false
		elif c.has("kf"):
			c.kf.chunks={}
			c.kf.id=-1
			c.kf.loaded=false
		elif role=="human":
			det.chunks={}
			det.chunk_id=-1
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
	var key := Marshalls.base64_to_raw(spec.key)
	var transport := {}
	if deterministic:
		# Real-time floor: the lockstep stream needs ~15 kB/s; wireless loss must not starve it.
		# The floor follows the stream: every client receives all players' commands,
		# about 2.4 B per player per tick with framing and redundancy (measured
		# 420-590 B/tick per client at 256 players), clamped to 16-64 KiB/s.
		var stream_floor := clampi(int(config.total)*2*60*6/5,16384,65536)
		transport = {"channel_modes":DETERMINISTIC_MODES,"minimum_rate":int(_tuning("minimum_rate",stream_floor))}
	var connection_id := _connection_id(key,c.generation)
	# Port plan (all below the OS ephemeral range, which starts at 49152): server
	# port_base, clients port_base-3000+id, packet proxy front -2000+id, back -1000+id.
	if not bool(_tuning("single_port",1)):
		# A/B reference: one connected UDP association per client port pair.
		var local_port: int = config.port_base + (c.id if server else c.id-3000)
		var remote_port: int = config.port_base + (c.id-1000 if server else c.id-2000)
		var legacy := [server,config.server_ip if server else "127.0.0.1",local_port,config.client_ip if server else "127.0.0.1",remote_port,53100+c.id,60000+c.id,key,transport]
		check(session.callv("configure_udp",legacy) == OK,"UDP association provision")
	elif server:
		# Every client reaches the server's one port; the connection ID routes its datagrams
		# to this association and DTLS authenticates it with the admission key.
		if udp_listener == null:
			udp_listener = SuperposUdpListener.new()
			check(udp_listener.bind(config.server_ip,config.port_base,{"maximum_associations":int(config.total)+16,"receive_buffer_bytes":8388608}) == OK,"Single-port UDP listener")
		check(session.configure_udp_listener(udp_listener,connection_id,53100+c.id,60000+c.id,key,transport) == OK,"UDP association provision")
	else:
		transport.connection_id = connection_id
		var args := [false,"127.0.0.1",config.port_base-3000+c.id,"127.0.0.1",config.port_base-2000+c.id,53100+c.id,60000+c.id,key,transport]
		check(session.callv("configure_udp",args) == OK,"UDP association provision")
	c.unreliable=[]
	c.session = session
	if role=="server" and deterministic and lockstep_server!=null:
		# The native batch service serves this slot through its new association.
		lockstep_server.bind_session(c.id,session)
		# Every client receives its command stream at 60 Hz.
		lockstep_server.set_stream_interval(c.id,1)
	c.tickets = []
	c.ready = false
	c.pumped=false
	c.last_ready = false
	c.last_error = 0
	c.last_send = now
	c.last_state = now
	c.accepted_sequence = 0

func _publish(c: Dictionary, bytes: PackedByteArray) -> bool:
	var started := Time.get_ticks_usec()
	var published := _publish_canonical(c,bytes)
	publish_usec+=Time.get_ticks_usec()-started
	return published

func _publish_canonical(c: Dictionary, bytes: PackedByteArray) -> bool:
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
	var physics_begin := Time.get_ticks_usec()
	if det_frame_fresh:
		# First physics step of this engine frame: the catch-up budget starts here.
		det_frame_fresh=false
		det_frame_started=physics_begin
	_physics_step(delta)
	frame_physics_usec+=Time.get_ticks_usec()-physics_begin
	frame_physics_steps+=1

func _physics_step(delta: float) -> void:
	if not failed.is_empty():
		return
	now = Time.get_unix_time_from_system() - float(config.start_unix)
	frame += 1
	var step_usec := Time.get_ticks_usec()
	if last_step_usec>0 and now>5:
		step_intervals.append(float(step_usec-last_step_usec)/1000.0)
		if step_usec-last_step_usec>50000 and diagnostics.size()<200:
			diagnostics.append([snappedf(now,0.01),"tick_gap",float(step_usec-last_step_usec)/1000.0])
		if step_intervals.size()>3600:
			step_intervals.pop_front()
	last_step_usec=step_usec
	var epochs: Dictionary = {}
	if frame % 30 == 0:
		var control := FileAccess.open(config.control,FileAccess.READ)
		if control:
			var decoded = JSON.parse_string(control.get_as_text())
			if decoded is Dictionary:
				epochs = decoded
	var network_started := Time.get_ticks_usec()
	for c in connections:
		# The test supervisor synchronizes retries; old datagrams stay in old epochs.
		var desired := int(epochs.get(str(c.id),c.generation))
		if desired > c.generation:
			c.generation = desired
			_open(c)
		# Every association (bots and the human) pumps at the 60 Hz tick rate.
		c.pumped=true
		if not c.pumped:
			continue
		var pump_started := Time.get_ticks_usec()
		var result: int = c.session.advance_tick()
		var admission_started := Time.get_ticks_usec()
		c.ready = c.session.is_network_ready()
		pump_profile[0]+=admission_started-pump_started
		pump_profile[1]+=Time.get_ticks_usec()-admission_started
		pump_profile[2]+=1
		if result != OK and result != ERR_BUSY and result != ERR_UNCONFIGURED and result != c.last_error:
			c.transport_errors += 1
			c.last_error = result
			if not c.has("error_codes"):
				c.error_codes={}
			c.error_codes[str(result)]=int(c.error_codes.get(str(result),0))+1
		if c.ready:
			if not c.ever_ready:
				c.ever_ready = true
			elif not c.last_ready:
				c.recoveries += 1
			if not c.last_ready and role=="server" and deterministic and not joining.has(c):
				joining.append(c)
			_retire(c, STATE if role == "server" else INPUT)
		c.last_ready = c.ready
	if role=="server" and frame%600==0 and pump_profile[2]>0 and diagnostics.size()<400:
		diagnostics.append([snappedf(now,0.01),"pump_us",float(pump_profile[0])/pump_profile[2],float(pump_profile[1])/pump_profile[2],pump_profile[2]])
		pump_profile=[0,0,0]
	if role=="server" and now>5:
		network_times.append(float(Time.get_ticks_usec()-network_started)/1000.0)
		if network_times.size()>3600:
			network_times.pop_front()
	if role == "human":
		_client_step(delta)
		_section("tick",Time.get_ticks_usec()-step_usec)
	elif role == "server":
		var application_started := Time.get_ticks_usec()
		_server_step()
		if now>5:
			application_times.append(float(Time.get_ticks_usec()-application_started)/1000.0)
			if application_times.size()>3600:
				application_times.pop_front()
	elif role != "human":
		_client_step(delta)
	if frame % 30 == 0:
		_write_telemetry(frame % 1800 == 0)
	if now >= float(config.duration) + 3:
		_write_telemetry(true)
		print("LAB_FINISHED role=",role," sessions=",connections.size())
		get_tree().quit(0)

func _lanes(c: Dictionary, kind: int) -> Array:
	if kind==EXHIBIT:
		return [EXHIBIT]
	var lanes: Array = INPUT_LANES if kind==INPUT else STATE_LANES
	# Bots keep one frame in flight; the human client pipelines state on two lanes.
	if c.id!=HUMAN:
		return lanes.slice(0,int(_tuning("bot_lanes",BOT_LANES)))
	var count := _tuning("human_lanes",HUMAN_LANES)
	return lanes.slice(0,int(_tuning("human_input_lanes",HUMAN_INPUT_LANES) if kind==INPUT else count))

func _tuning(key: String, fallback: float) -> float:
	# Optional run_lab --tuning overrides for transport budget sweeps.
	var tuning: Dictionary = config.get("tuning",{})
	return float(tuning.get(key,fallback))

func _acked(result: int, what: String) -> void:
	# Busy: receipts are backed up behind a paced carrier. The message stays at the
	# lane head and is re-read (idempotently) next tick.
	if result!=OK and result!=ERR_BUSY:
		check(false,"%s error=%d" % [what,result])

func _pending(c: Dictionary, channel: int) -> int:
	var count := 0
	for ticket in c.tickets:
		if int(ticket.channel)==channel:
			count+=1
	return count

func _free_lane(c: Dictionary, kind: int) -> int:
	for lane in _lanes(c,kind):
		if _pending(c,lane)==0:
			return lane
	return -1

func _in_flight(c: Dictionary, kind: int) -> int:
	var count := 0
	for lane in _lanes(c,kind):
		count+=_pending(c,lane)
	return count

func _retire(c: Dictionary, _channel: int) -> void:
	for i in range(c.tickets.size()-1,-1,-1):
		var ticket: Dictionary=c.tickets[i]
		var channel: int=ticket.channel
		var outcome: Dictionary=c.session.get_packet_outcome(ticket.message,ticket.binding_generation,channel)
		if outcome.error==OK and outcome.outcome=="Applied":
			check(c.session.retire_packet(ticket.message,ticket.binding_generation,channel)==OK,"Explicit sender retirement")
			if channel!=EXHIBIT:
				var delay := float(Time.get_ticks_msec()-int(ticket.at))
				if c.id==HUMAN and delay>400 and diagnostics.size()<200:
					diagnostics.append([snappedf(now,0.01),"ack",channel,delay])
				c.ack_ms=delay if c.ack_ms==0 else lerpf(c.ack_ms,delay,0.2)
				c.acks.append(delay)
				if c.acks.size()>500:
					c.acks.pop_front()
			c.applied+=1
			c.tickets.remove_at(i)

func _exhibit_period(c: Dictionary) -> float:
	return _tuning("human_exhibit_period",HUMAN_EXHIBIT_PERIOD) if c.id==HUMAN else 0.75

func _enqueue(c: Dictionary, bytes: PackedByteArray, kind: int) -> void:
	var elapsed: float=now-(c.last_exhibit_send if kind==EXHIBIT else c.last_send)
	var lane := _free_lane(c,kind)
	if not c.ready or lane<0 or elapsed<(_exhibit_period(c) if kind==EXHIBIT else c.period):
		return
	var ticket: Dictionary=c.session.enqueue_packet(bytes,lane)
	if ticket.error==OK:
		ticket.at=Time.get_ticks_msec()
		c.tickets.append(ticket)
		c.sent+=1
		if kind==EXHIBIT:
			c.last_exhibit_send=now
		else:
			# Fixed-rate cadence; lane availability alone provides backpressure.
			c.last_send=maxf(c.last_send+c.period,now-c.period)
			c.max_pending=maxi(c.max_pending,_in_flight(c,kind))

func _server_step_deterministic() -> void:
	var control_started := Time.get_ticks_usec()
	lockstep_server.ingest_inputs(INPUT,8)
	var tick: int=physics.get_tick()+1
	var inputs: Array=lockstep_server.step_commands(tick,HOLD_MASK,MERGE_MASK)
	check(inputs.size()==config.total,"Lockstep tick")
	if bool(_tuning("record_inputs",0)) and now>0 and not input_recorded:
		# Encoding research: every slot's relayed input, one row per tick.
		for slot_input in inputs:
			input_recording.append_array(slot_input)
		if now>=float(config.duration) and not input_recording.is_empty():
			var recorded := FileAccess.open(config.telemetry.get_base_dir().path_join("inputs-%d.bin" % config.total),FileAccess.WRITE)
			if recorded:
				recorded.store_buffer(input_recording)
			input_recording=PackedByteArray()
			input_recorded=true
	if tick%10==0:
		# Sampled activity census (qualification evidence), not a per-tick loop.
		for c in connections:
			var flags: int=GAMEPLAY.decode_flags(inputs[c.id])
			var stance := 0
			for candidate in [256,128,64,16,8,32]:
				if flags&candidate:
					stance=candidate
					break
			c.activities[str(stance)]=int(c.activities.get(str(stance),0))+1
	var started := Time.get_ticks_usec()
	_sample(control_times,started-control_started)
	gameplay.step(tick,inputs)
	physics_ms.append(float(Time.get_ticks_usec()-started)/1000.0)
	if physics_ms.size()>2000:
		physics_ms.pop_front()
	var replication_started := Time.get_ticks_usec()
	if tick%6==0:
		# Travelled distance at 10 Hz from one batched native read.
		var bodies: PackedFloat32Array=physics.get_body_states(gameplay.player_ids)
		for c in connections:
			var o: int=c.id*13
			c.position=Vector3(bodies[o],bodies[o+1],bodies[o+2])
			c.distance+=c.position.distance_to(c.last_position)
			c.last_position=c.position
	_send_lockstep()
	_sample(replication_times,Time.get_ticks_usec()-replication_started)

func _server_step() -> void:
	if deterministic:
		_server_step_deterministic()
		return
	var acknowledgements: Array[Dictionary] = []
	var control_started := Time.get_ticks_usec()
	var inputs := []
	inputs.resize(config.total)
	for c in connections:
		if c.ready and c.pumped:
			# read_packet peeks the lane head until it is acknowledged: one message per lane per tick.
			for lane in _lanes(c,INPUT):
				var packet: Dictionary = c.session.read_packet(lane)
				if packet.error == OK:
					_accept_input(c,packet.payload)
					acknowledgements.append({"c":c,"packet":packet,"lane":lane})
		inputs[c.id]=_tick_input(c)
	# One deterministic tick: record every slot's input in the lockstep command stream,
	# then run the shared gameplay step exactly as deterministic clients will.
	var tick: int=physics.get_tick()+1
	if deterministic:
		check(lockstep_server.begin_tick(tick)==OK,"Lockstep tick")
		# Each slot's consumed client tick rides privately to that slot's client.
		var processed := {}
		for c in connections:
			processed[c.id]=int(c.get("processed_tick",0))
		for slot in range(config.total):
			check(lockstep_server.set_command(slot,inputs[slot],int(processed.get(slot,0)))==OK,"Lockstep command")
		check(lockstep_server.end_tick()==OK,"Lockstep seal")
	var started := Time.get_ticks_usec()
	_sample(control_times,started-control_started)
	gameplay.step(tick,inputs)
	physics_ms.append(float(Time.get_ticks_usec()-started)/1000.0)
	if physics_ms.size()>2000:
		physics_ms.pop_front()
	for c in connections:
		var actor: Dictionary=gameplay.actors[c.id]
		c.stance=actor.stance
		c.collider_height=actor.collider_height
		c.score=actor.score
		c.goal=actor.goal
	var replication_started := Time.get_ticks_usec()
	for item in acknowledgements:
		var c: Dictionary = item.c
		var packet: Dictionary = item.packet
		_acked(c.session.acknowledge_packet(packet.message,packet.binding_generation,item.lane),"Controls acknowledged after native physics step")
	var states: Dictionary = {}
	for c in connections:
		var state := physics.get_body_state(PLAYER_BASE+c.id)
		c.position = state.position
		c.distance += c.position.distance_to(c.last_position)
		c.last_position = c.position
		states[c.id] = state
	for prop in range(40):
		states[PROP_BASE+prop] = physics.get_body_state(PROP_BASE+prop)
		prop_states[PROP_BASE+prop]=states[PROP_BASE+prop]
	for prop in range(22):
		states[SIMULATION.FLOAT_BASE+prop]=physics.get_body_state(SIMULATION.FLOAT_BASE+prop)
	for i in range(PLAYGROUND.dynamic_bodies().size()):
		states[PLAYGROUND.BASE+i]=physics.get_body_state(PLAYGROUND.BASE+i)
		prop_states[PLAYGROUND.BASE+i]=states[PLAYGROUND.BASE+i]
	# Stamp frames with exact simulation time, so server frame hitches never become jitter.
	if sim_epoch<0:
		sim_epoch=now-float(physics.get_tick())/60.0
	var sim_time: float=sim_epoch+float(physics.get_tick())/60.0
	var records := {}
	var ids: Array = states.keys()
	for c in connections:
		var local_world: bool=deterministic
		if c.ready and not local_world and _free_lane(c,STATE)>=0 and now-c.last_send>=c.period:
			if c.id==HUMAN:
				c.interest=_priority_interest(c,states,ids)
			elif c.interest.is_empty() or now>=c.next_interest:
				# Native integer sort of packed distance|id keys; staggered refresh avoids spikes.
				var keys := PackedInt64Array()
				for id in ids:
					if id!=c.id:
						keys.append(int(states[id].position.distance_squared_to(c.position)*256.0)*8192+int(id))
				keys.sort()
				c.interest = [c.id]
				for i in range(mini(BOT_ENTITIES-1,keys.size())):
					c.interest.append(keys[i]%8192)
				c.next_interest=now+(0.25 if c.id==HUMAN else 0.4+0.02*(c.id%10))
			var interest: Array=c.interest
			var bytes := "S104".to_ascii_buffer()
			bytes.resize(STATE_HEADER)
			c.sequence += 1
			bytes.encode_u32(4,c.sequence)
			bytes.encode_u32(8,physics.get_tick())
			# Humans reconcile against the exact client input tick the server last simulated.
			bytes.encode_u32(12,c.processed_tick if c.id==HUMAN else c.input_sequence)
			bytes.encode_u32(16,c.score*8+c.goal)
			bytes.encode_u32(20,interest.size())
			bytes.encode_float(24,sim_time)
			bytes.encode_u32(28,maxi(c.received_tick,0) if c.id==HUMAN else 0)
			for id in interest:
				if not records.has(id):
					records[id]=_body_record(id,states[id])
				bytes.append_array(records[id])
			if _publish(c,bytes):
				_enqueue(c,bytes,STATE)
		if not deterministic and c.ready and _pending(c,EXHIBIT)==0 and now-c.last_exhibit_send>=_exhibit_period(c):
			c.packet_cycle+=1
			c.sequence+=1
			var exhibit_packet: PackedByteArray=simulation.compact_packet(c.sequence,physics.get_tick(),sim_time)
			if _publish(c,exhibit_packet):
				_enqueue(c,exhibit_packet,EXHIBIT)
	_send_lockstep()
	_sample(replication_times,Time.get_ticks_usec()-replication_started)
	_sample(publish_times,publish_usec)
	publish_usec=0

func _priority_interest(c: Dictionary, states: Dictionary, ids: Array) -> Array:
	# Priority accumulator (Gaffer state sync / LiteEntitySystem style): every entity gains
	# priority each frame by distance and motion; the frame carries the highest, which reset.
	# All 163 entities stream continuously, near ones near frame rate, far ones a few Hz.
	var elapsed: float=clampf(now-c.get("priority_time",now),0.0,0.5)
	c.priority_time=now
	if not c.has("priority"):
		c.priority={}
	var keys := PackedInt64Array()
	for id in ids:
		if id==c.id:
			continue
		var state: Dictionary=states[id]
		var weight: float=1.0+24.0/(state.position.distance_to(c.position)+2.0)
		var speed: float=state.linear_velocity.length()
		if id>=config.total and speed<0.1:
			weight*=0.15
		weight*=1.0+minf(speed,12.0)*0.5
		var score: float=c.priority.get(id,1000.0)+weight*elapsed
		c.priority[id]=score
		keys.append((1000000000-mini(int(score*1000.0),999999999))*8192+int(id))
	keys.sort()
	var interest := [c.id]
	for i in range(mini(STATE_ENTITIES-1,keys.size())):
		var id: int=keys[i]%8192
		interest.append(id)
		c.priority[id]=0.0
	return interest

func _section(name: String, usec: int) -> void:
	if now>5:
		section_ms[name].append(float(usec)/1000.0)
		if section_ms[name].size()>600:
			section_ms[name].pop_front()

func _sample(values: Array[float], usec: int) -> void:
	if now>5:
		values.append(float(usec)/1000.0)
		if values.size()>3600:
			values.pop_front()

func _accept_input(c: Dictionary, bytes: PackedByteArray) -> void:
	if deterministic:
		if true:
			if lockstep_server.accept_inputs(c.id,bytes)==OK:
				c.received+=1
			else:
				c.rejected+=1
			return
		_accept_human_inputs(c,bytes)
		return
	if bytes.size()!=24 or bytes.slice(0,4).get_string_from_ascii()!="C102":
		c.rejected+=1
		return
	var incoming := bytes.decode_u32(4)
	var x := bytes.decode_float(8)
	var z := bytes.decode_float(12)
	var flags := bytes.decode_u32(16)
	if not is_finite(x) or not is_finite(z) or absf(x)>1.001 or absf(z)>1.001 or flags>4095 or not is_finite(bytes.decode_float(20)) or absf(bytes.decode_float(20))>PI+0.001:
		c.rejected+=1
		return
	if incoming<=c.input_sequence:
		# Parallel lanes may reorder frames: keep newer movement, but never lose one-shot actions.
		c.stale+=1
		c.pending_oneshot=int(c.get("pending_oneshot",0))|(flags&GAMEPLAY.ONE_SHOT)
		return
	c.input_sequence=incoming
	_apply_controls(c,x,z,flags,bytes.decode_float(20))
	c.received+=1

func _apply_controls(c: Dictionary, x: float, z: float, flags: int, facing: float) -> void:
	# Latest intent only; the deterministic step derives stance and applies physics.
	c.direction=Vector3(x,0,z).limit_length(1.0)
	c.input_flags=flags
	c.pending_oneshot=int(c.get("pending_oneshot",0))|(flags&GAMEPLAY.ONE_SHOT)
	c.sprinting=(flags&4)!=0
	c.facing=facing
	var stance := 0
	for candidate in [256,128,64,16,8,32]:
		if flags&candidate:
			stance=candidate
			break
	c.activities[str(stance)]=int(c.activities.get(str(stance),0))+1
	c.last_input=now

func _tick_input(c: Dictionary) -> PackedByteArray:
	if deterministic:
		# Every client's own per-tick inputs, played out by the native jitter buffer. A
		# starved tick holds stance but never invents movement; catch-up merges actions.
		var items: Array=lockstep_server.consume_inputs(c.id)
		if items.is_empty():
			return c.get("hold_input",GAMEPLAY.encode_input(Vector3.ZERO,0,0.0))
		var latest: PackedByteArray=items.back().input.duplicate()
		var one_shot := 0
		for item in items:
			one_shot|=GAMEPLAY.decode_flags(item.input)&GAMEPLAY.ONE_SHOT
		latest=GAMEPLAY.encode_input(GAMEPLAY.decode_direction(latest),GAMEPLAY.decode_flags(latest)|one_shot,GAMEPLAY.decode_facing(latest))
		var hold: PackedByteArray=GAMEPLAY.encode_input(Vector3.ZERO,GAMEPLAY.decode_flags(latest)&GAMEPLAY.PERSISTENT,GAMEPLAY.decode_facing(latest))
		c.hold_input=hold
		c.processed_tick=int(items.back().tick)
		c.last_input=now
		var stance := 0
		for candidate in [256,128,64,16,8,32]:
			if GAMEPLAY.decode_flags(latest)&candidate:
				stance=candidate
				break
		c.activities[str(stance)]=int(c.activities.get(str(stance),0))+1
		return latest
	# Bots (and the legacy human path): the server's per-tick decision from the latest
	# intent, recorded in the command stream so clients replay exactly this.
	var direction: Vector3=c.direction if now-c.last_input<1.25 else Vector3.ZERO
	var flags: int=(int(c.get("input_flags",0))&GAMEPLAY.PERSISTENT)|int(c.get("pending_oneshot",0))
	c.pending_oneshot=0
	return GAMEPLAY.encode_input(direction,flags,c.facing)

func _retire_unreliable(c: Dictionary) -> void:
	# Unreliable messages are never applied-acknowledged when lost: retire each sender
	# slot once the carrier accepted it, so lanes and shared reservations stay free.
	var kept: Array=[]
	for ticket in c.get("unreliable",[]):
		if c.session.retire_packet(ticket.message,ticket.binding_generation,ticket.channel)==OK:
			c.applied+=1
		elif Time.get_ticks_msec()-int(ticket.at)<30000:
			# Keep retrying past the core's own expiry: a slot dropped from tracking
			# before it was retired would leak the lane for good.
			kept.append(ticket)
	c.unreliable=kept

func _unreliable_waiting(c: Dictionary, lane: int) -> int:
	# Recent messages the carrier has not taken yet (an older one may have been
	# expired by the core; it no longer counts as backpressure).
	var count := 0
	var cutoff: int=Time.get_ticks_msec()-1000
	for ticket in c.get("unreliable",[]):
		if int(ticket.channel)==lane and int(ticket.at)>cutoff:
			count+=1
	return count

func _send_unreliable(c: Dictionary, bytes: PackedByteArray, lane: int) -> void:
	var ticket: Dictionary=c.session.enqueue_packet(bytes,lane)
	if ticket.error==OK:
		ticket.at=Time.get_ticks_msec()
		c.unreliable.append(ticket)
		c.sent+=1
	else:
		if not c.has("enqueue_failures"):
			c.enqueue_failures={}
		var key: String="%d:%d" % [lane,int(ticket.error)]
		c.enqueue_failures[key]=int(c.enqueue_failures.get(key,0))+1

func _send_lockstep() -> void:
	if not deterministic:
		return
	var still_joining: Array = []
	for c in joining:
		if not c.ready:
			continue
		if not c.pumped:
			still_joining.append(c)
			continue
		_retire_unreliable(c)
		if int(c.get("keyframe_tick",0))==0 or c.get("resync",false):
			_send_keyframe(c)
			still_joining.append(c)
		elif _pending(c,KEYFRAME_LANE)>0 or int(c.get("kf_next",0))<int(c.get("kf_count",0)):
			# One chunk in flight at a time; the stream resumes when the keyframe lands.
			if _pending(c,KEYFRAME_LANE)==0:
				_send_keyframe_chunk(c)
			still_joining.append(c)
		else:
			lockstep_server.set_stream_enabled(c.id,true)
	joining=still_joining
	if not bool(_tuning("native_publish",1)):
		# A/B reference: the per-client script path.
		for c in connections:
			if c.ready and c.pumped and not joining.has(c) and int(c.get("keyframe_tick",0))>0:
				_send_lockstep_to(c)
		return
	var published: Dictionary=lockstep_server.publish_commands(COMMAND_LANE,physics.get_tick())
	for slot in published.stale:
		# History no longer covers this client: one keyframe resync at most every 2 s.
		var c: Dictionary=connections[slot]
		if now-float(c.get("resync_at",-100.0))>2.0:
			c.resync=true
			c.resync_at=now
		if not joining.has(c):
			joining.append(c)

func _send_lockstep_to(c: Dictionary) -> void:
	_retire_unreliable(c)
	var status: Dictionary=lockstep_server.get_playout_status(c.id)
	if int(c.get("keyframe_tick",0))==0 or c.get("resync",false):
		_send_keyframe(c)
		return
	if _pending(c,KEYFRAME_LANE)>0 or int(c.get("kf_next",0))<int(c.get("kf_count",0)):
		# The join keyframe owns the link until it lands, one chunk in flight at a
		# time; retained command history covers the wait.
		if _pending(c,KEYFRAME_LANE)==0:
			_send_keyframe_chunk(c)
		return
	# Flow control: while earlier batches still wait for the carrier, build none;
	# the next batch simply carries more fresh ticks.
	if _unreliable_waiting(c,COMMAND_LANE)>=2:
		return
	# The native per-client sender (fresh-first windows, rewind after 2 x sRTT,
	# redundancy only while caught up) runs in the Superpos core.
	if now-float(c.get("srtt_at",-1.0))>0.25:
		c.srtt_at=now
		c.srtt=float(c.session.get_statistics().get("smoothed_rtt_us",100000))/1000000.0
	var batch: Dictionary=lockstep_server.pack_stream(c.id,int(float(c.get("srtt",0.1))*1000000.0),860)
	if batch.error==ERR_DOES_NOT_EXIST:
		# History no longer covers this client: one keyframe resync at most every 2 s.
		if now-float(c.get("resync_at",-100.0))>2.0:
			c.resync=true
			c.resync_at=now
		return
	if batch.error!=OK:
		return
	var payload := PackedByteArray()
	payload.resize(8)
	payload.encode_u32(0,maxi(int(status.get("received_tick",0)),0))
	payload.encode_u32(4,physics.get_tick())
	payload.append_array(batch.payload)
	c.command_bytes=int(c.get("command_bytes",0))+payload.size()
	_send_unreliable(c,payload,COMMAND_LANE)

func _varuints(bytes: PackedByteArray, offset: int, count: int) -> Array:
	var values: Array=[]
	var position: int=offset
	for _value in range(count):
		var value := 0
		var shift := 0
		while position<bytes.size():
			var byte: int=bytes[position]
			position+=1
			value|=(byte&127)<<shift
			shift+=7
			if byte<128:
				break
		values.append(value)
	return values

# Admission hands each association its routing ID with its key: derived from the private
# stream key and the association generation, it is nonzero, unpredictable without the key
# and fresh on every re-admission. Both ends derive the same value.
func _connection_id(key: PackedByteArray, generation: int) -> int:
	var hashing := HashingContext.new()
	hashing.start(HashingContext.HASH_SHA256)
	hashing.update(key)
	var counter := PackedByteArray()
	counter.resize(8)
	counter.encode_u64(0,generation)
	hashing.update(counter)
	var id := hashing.finish().decode_u64(0) & 0x7FFFFFFFFFFFFFFF
	return id if id != 0 else 1

func _send_keyframe(c: Dictionary) -> void:
	if _pending(c,KEYFRAME_LANE)>0:
		return
	# World snapshot + gameplay actors + the lockstep table, all at the newest tick.
	# Joining clients in the same tick share one capture.
	if int(keyframe_cache.get("tick",-1))!=physics.get_tick():
		var table: PackedByteArray=lockstep_server.pack_keyframe()
		var captured := PackedByteArray()
		captured.resize(8)
		captured.encode_u32(0,physics.get_tick())
		captured.encode_u32(4,table.size())
		captured.append_array(table)
		var capture_started := Time.get_ticks_usec()
		captured.append_array(gameplay.capture())
		keyframe_cache={"tick":physics.get_tick(),"body":captured}
		if diagnostics.size()<400:
			diagnostics.append([snappedf(now,0.01),"keyframe_capture",float(Time.get_ticks_usec()-capture_started)/1000.0,captured.size()])
	c.kf_body=keyframe_cache.body
	c.keyframe_id=int(c.get("keyframe_id",0))+1
	c.kf_count=ceili(float(c.kf_body.size())/KEYFRAME_CHUNK)
	c.kf_next=0
	_send_keyframe_chunk(c)
	c.keyframe_tick=physics.get_tick()
	lockstep_server.set_stream_enabled(c.id,false)
	lockstep_server.reset_stream(c.id,c.keyframe_tick)
	c.resync=false
	c.keyframes_sent=int(c.get("keyframes_sent",0))+1

func _send_keyframe_chunk(c: Dictionary) -> void:
	# One reliable chunk in flight per joiner: a slow link drains it at its own pace
	# instead of queueing the whole keyframe behind itself.
	var body: PackedByteArray=c.kf_body
	var index: int=int(c.kf_next)
	var chunk := "KF".to_ascii_buffer()
	chunk.resize(12)
	chunk.encode_u8(2,index)
	chunk.encode_u8(3,int(c.kf_count))
	chunk.encode_u32(4,c.keyframe_id)
	chunk.encode_u32(8,body.size())
	chunk.append_array(body.slice(index*KEYFRAME_CHUNK,mini(body.size(),(index+1)*KEYFRAME_CHUNK)))
	var ticket: Dictionary=c.session.enqueue_packet(chunk,KEYFRAME_LANE)
	if ticket.error==OK:
		ticket.at=Time.get_ticks_msec()
		c.tickets.append(ticket)
		c.kf_next=index+1

func _accept_human_inputs(c: Dictionary, bytes: PackedByteArray) -> void:
	# C103: every client tick since the server's last received tick, sent redundantly
	# until acknowledged (LiteEntitySystem-style input history), so no tick is ever lost.
	if bytes.size()<16 or bytes.slice(0,4).get_string_from_ascii()!="C103":
		c.rejected+=1
		return
	var first := bytes.decode_u32(8)
	var count := bytes.decode_u16(12)
	if count<1 or count>INPUT_HISTORY or bytes.size()!=16+count*INPUT_RECORD:
		c.rejected+=1
		return
	var arrived := false
	for i in range(count):
		var tick: int=first+i
		if tick<=c.received_tick:
			continue
		var offset := 16+i*INPUT_RECORD
		var flags := bytes.decode_u16(offset+2)
		if flags>4095:
			c.rejected+=1
			return
		c.input_queue[tick]=[float(bytes.decode_s8(offset))/127.0,float(bytes.decode_s8(offset+1))/127.0,flags,float(bytes.decode_u16(offset+4))/65535.0*TAU-PI]
		c.received_tick=tick
		arrived=true
	if c.next_tick<0 and arrived:
		c.next_tick=first
	if arrived:
		if c.last_arrival_frame>0:
			c.arrival_gaps.append(frame-c.last_arrival_frame)
			if c.arrival_gaps.size()>40:
				c.arrival_gaps.pop_front()
		c.last_arrival_frame=frame
	c.received+=1

func _consume_human_input(c: Dictionary) -> void:
	# Adaptive playout buffer: inputs arrive in bursts (one reliable lane, ack-paced), but
	# the server simulates exactly one client tick per server tick, like a jitter buffer.
	var depth: int=c.received_tick-c.next_tick+1 if c.next_tick>=0 else 0
	if not c.playing:
		if depth<c.buffer_target:
			c.direction=Vector3.ZERO
			c.flags=0
			return
		c.playing=true
	if depth<=0:
		c.playing=false
		c.starvations+=1
		c.buffer_target=mini(c.buffer_target+3,45)
		c.calm_since=now
		c.direction=Vector3.ZERO
		c.flags=0
		return
	if now-c.calm_since>4.0 and c.buffer_target>4:
		c.buffer_target-=1
		c.calm_since=now
	# Bound added latency: run one extra client tick when far ahead of the target.
	var steps := 2 if depth>c.buffer_target+12 else 1
	for _step in range(steps):
		while c.next_tick<=c.received_tick and not c.input_queue.has(c.next_tick):
			c.next_tick+=1
		if c.next_tick>c.received_tick:
			break
		var input: Array=c.input_queue[c.next_tick]
		c.input_queue.erase(c.next_tick)
		var one_shot: int=c.flags&(1|2|1024|2048) if _step>0 else 0
		_apply_controls(c,input[0],input[1],int(input[2])|one_shot,input[3])
		c.processed_tick=c.next_tick
		c.next_tick+=1
	c.buffer_depths.append(depth)
	if c.buffer_depths.size()>600:
		c.buffer_depths.pop_front()

func _movable_ids() -> Array:
	if movable.is_empty():
		for prop in range(40):
			movable.append(PROP_BASE+prop)
		for i in range(PLAYGROUND.dynamic_bodies().size()):
			movable.append(PLAYGROUND.BASE+i)
	return movable

func _grounded(position: Vector3, velocity: Vector3) -> bool:
	# A Box3D ray from the standing capsule's centre to just past its bottom.
	if absf(velocity.y)>1.0:
		return false
	var hits: Dictionary=physics.cast_rays(PackedVector3Array([position]),PackedVector3Array([Vector3(0,-(0.9+GAMEPLAY.GROUND_REACH),0)]))
	return hits.hit[0]==1

func _client_step(delta: float) -> void:
	if role=="bots" and deterministic:
		_bot_world_step()
	for c in connections:
		if role=="bots" and deterministic:
			_bot_stream(c)
		if role=="human" and deterministic and c.ready and c.pumped:
			_retire_unreliable(c)
			_read_keyframe(c)
			_read_commands(c)
		if c.ready and c.pumped:
			for lane in _lanes(c,STATE):
				var packet: Dictionary=c.session.read_packet(lane)
				if packet.error==OK:
					if not _accept_state(c,packet.payload):
						return
					_acked(c.session.acknowledge_packet(packet.message,packet.binding_generation,lane),"Validated state application receipt")
		if c.ready and c.pumped:
			var extra: Dictionary=c.session.read_packet(EXHIBIT)
			if extra.error==OK:
				if not _accept_exhibit(c,extra.payload):
					return
				_acked(c.session.acknowledge_packet(extra.message,extra.binding_generation,EXHIBIT),"Validated exhibit receipt")
		var direction := Vector3.ZERO
		var flags := 0
		if role == "human":
			direction = Vector3(float(Input.is_physical_key_pressed(KEY_D))-float(Input.is_physical_key_pressed(KEY_A)),0,float(Input.is_physical_key_pressed(KEY_S))-float(Input.is_physical_key_pressed(KEY_W)))
			direction = direction.rotated(Vector3.UP,yaw).limit_length(1.0)
			flags = (1 if jump_requested else 0) | (2 if pulse_requested else 0) | human_stance | (1024 if respawn_requested else 0) | (2048 if interact_requested else 0)
			c.facing=wrapf(yaw+PI,-PI,PI)
			if Input.is_physical_key_pressed(KEY_CTRL):
				flags|=8
			if Input.is_physical_key_pressed(KEY_E):
				flags|=32|2048
			if Input.is_physical_key_pressed(KEY_SHIFT):
				flags |= 4
		else:
			# Each brain sees its own delayed AOI, never the server's diagnostic world.
			var goal := _goal(c.goal)
			var position: Vector3 = c.position
			# Steering is decided every 60 Hz tick, like a client sampling input each frame.
			direction = Vector3(goal.x-position.x,0,goal.z-position.z).normalized()
			c.facing=atan2(direction.x,direction.z)
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
			if c.id%3==0 and now-c.last_state<0.6:
				flags |= 4
			# Independent rotating behaviours exercise replicated actions and transitions.
			var phase := int(now+c.id*1.7)%48
			if phase>=28 and phase<32:
				flags|=8
			elif phase>=32 and phase<36:
				flags|=16
			elif phase>=36 and phase<39:
				flags|=32|2048
			elif phase>=39 and phase<42:
				flags|=64
			elif phase>=42 and phase<44:
				flags|=256
			elif phase==44:
				flags|=1024
			if position.distance_to(goal)<6:
				flags|=2048
		if role == "bots" and deterministic:
			_bot_input(c,direction,flags)
			continue
		if role == "human":
			_human_tick(c,direction,flags)
			if deterministic:
				_advance_deterministic(c)
			continue
		c.stance=flags&(8|16|32|64|128|256)
		if c.stance&(64|128|256):
			direction=Vector3.ZERO
		c.direction = direction
		c.sprinting=(flags&4)!=0
		var predicted_speed := 2.8 if SIMULATION.in_water(c.position) and c.position.y<2.5 else _speed(c.stance,c.sprinting)
		c.predicted += direction*predicted_speed*delta
		if c.received>0:
			# The server applies today's input about half an ack later, so lead by the full round trip.
			var age := clampf(now-c.server_time+c.ack_ms*0.0005,0,0.6)
			var estimate: Vector3=c.position+c.own_velocity*age
			var predicted: Vector3=c.predicted
			var error := predicted.distance_to(estimate)
			# Small drift converges gently; large divergence (shockwave, respawn) converges quickly.
			c.predicted=estimate if error>6 else c.predicted.lerp(estimate,1-exp(-delta*(3.0 if error<1.0 else 9.0)))
		if c.ready and _free_lane(c,INPUT)>=0 and now-c.last_send>=c.period:
			c.sequence += 1
			var bytes := "C102".to_ascii_buffer()
			bytes.resize(24)
			bytes.encode_u32(4,c.sequence)
			bytes.encode_float(8,direction.x)
			bytes.encode_float(12,direction.z)
			bytes.encode_u32(16,flags)
			bytes.encode_float(20,c.facing)
			_enqueue(c,bytes,INPUT)

func _client_stance(c: Dictionary, flags: int) -> int:
	# Mirrors the server's stance priority; death persists until respawn.
	if c.stance&256 and not flags&1024:
		return 256
	for stance in [256,128,64,16,8,32]:
		if flags&stance:
			return stance
	return 0

func _human_tick(c: Dictionary, direction: Vector3, flags: int) -> void:
	# One quantized input per client tick, exactly what the server will replay.
	var x8 := clampi(roundi(direction.x*127.0),-127,127)
	var z8 := clampi(roundi(direction.z*127.0),-127,127)
	var recorded := true
	if deterministic:
		var input: PackedByteArray=GAMEPLAY.encode_input(Vector3(x8/127.0,0,z8/127.0),flags,c.facing)
		var result: int=lockstep_client.record_input(c.ctick+1,input)
		if result==ERR_OUT_OF_MEMORY:
			# An outage outlasted the input history: keep playing, the server skips the gap.
			lockstep_client.discard_oldest_inputs(32)
			result=lockstep_client.record_input(c.ctick+1,input)
		recorded=result==OK
	if recorded:
		c.ctick+=1
	var heading := roundi((wrapf(c.facing,-PI,PI)+PI)/TAU*65535.0)
	if not deterministic:
		c.pending_inputs.append([c.ctick,x8,z8,flags,heading])
		while c.pending_inputs.size()>INPUT_HISTORY:
			c.pending_inputs.pop_front()
	# One-shot actions live in this tick's input and are resent until the server has it.
	jump_requested=false
	pulse_requested=false
	respawn_requested=false
	interact_requested=false
	c.stance=_client_stance(c,flags)
	# Predict with exactly the quantized movement the server will simulate.
	var move: Vector3=GAMEPLAY.decode_direction(GAMEPLAY.encode_input(Vector3(x8/127.0,0,z8/127.0),flags,c.facing)) if deterministic else Vector3(x8/127.0,0,z8/127.0).limit_length(1.0)
	if c.stance&(64|128|256):
		move=Vector3.ZERO
	c.direction=move
	c.sprinting=(flags&4)!=0
	var wet: bool=SIMULATION.in_water(c.predicted) and c.render_y<2.5
	var speed := 2.8 if wet and not c.stance&(64|128|256) else _speed(c.stance,c.sprinting)
	c.prev_predicted=c.predicted
	if c.reconciled:
		c.predicted+=move*speed/60.0
		if recorded:
			c.history[c.ctick]=Vector2(c.predicted.x,c.predicted.z)
			c.history.erase(c.ctick-240)
	if deterministic:
		if c.ready:
			var batch: PackedByteArray=lockstep_client.pack_inputs(60,860)
			if batch.size()>0:
				_send_unreliable(c,batch,INPUT)
		return
	if c.ready and _free_lane(c,INPUT)>=0 and now-c.last_send>=c.period:
		var unsent: Array=c.pending_inputs.filter(func(input): return input[0]>c.server_received_tick)
		if unsent.is_empty():
			return
		# Small datagrams pace faster through the transport; later ticks follow next send.
		unsent=unsent.slice(0,INPUT_BATCH)
		var bytes := "C103".to_ascii_buffer()
		bytes.resize(16)
		c.sequence+=1
		bytes.encode_u32(4,c.sequence)
		bytes.encode_u32(8,unsent[0][0])
		bytes.encode_u16(12,unsent.size())
		for input in unsent:
			var record := PackedByteArray()
			record.resize(INPUT_RECORD)
			record.encode_s8(0,input[1])
			record.encode_s8(1,input[2])
			record.encode_u16(2,input[3])
			record.encode_u16(4,input[4])
			bytes.append_array(record)
		_enqueue(c,bytes,INPUT)

func _bot_stream(c: Dictionary) -> void:
	if not c.ready or not c.pumped:
		return
	_retire_unreliable(c)
	var lockstep: Object=c.lockstep
	var packet: Dictionary=c.session.read_packet(KEYFRAME_LANE)
	if packet.error==OK:
		_acked(c.session.acknowledge_packet(packet.message,packet.binding_generation,KEYFRAME_LANE),"Bot keyframe receipt")
		var bytes: PackedByteArray=packet.payload
		if bytes.size()>=12 and bytes.slice(0,2).get_string_from_ascii()=="KF":
			if int(bytes.decode_u32(4))!=int(c.kf.id):
				c.kf.id=bytes.decode_u32(4)
				c.kf.chunks={}
			c.kf.chunks[bytes.decode_u8(2)]=bytes.slice(12)
			if c.kf.chunks.size()==bytes.decode_u8(3):
				var body := PackedByteArray()
				for index in range(bytes.decode_u8(3)):
					body.append_array(c.kf.chunks[index])
				c.kf.chunks={}
				var table_size: int=body.decode_u32(4)
				if check(lockstep.load_keyframe(body.slice(8,8+table_size))==OK,"Bot keyframe join"):
					c.kf.loaded=true
					c.kf.keyframes+=1
					# The first bot (or a rejoining feed) seeds this process's shared world.
					if world_feed<0 or world_feed==c.id:
						var restored=GAMEPLAY.new()
						if check(restored.restore(body.slice(8+table_size),int(_tuning("physics_workers",1))),"Bot shared world restore"):
							if gameplay!=null:
								gameplay.close()
							gameplay=restored
							physics=gameplay.world
							world_feed=c.id
	# Drain every queued command batch, as a real client would each frame.
	for _message in range(16):
		packet=c.session.read_packet(COMMAND_LANE)
		if packet.error!=OK:
			break
		var acked: int=c.session.acknowledge_packet(packet.message,packet.binding_generation,COMMAND_LANE)
		_acked(acked,"Bot command receipt")
		var bytes: PackedByteArray=packet.payload
		if bytes.size()>=8:
			lockstep.acknowledge_inputs(bytes.decode_u32(0))
			c.received+=1
			c.last_state=now
			if c.kf.loaded:
				lockstep.accept_commands(bytes.slice(8))
		if acked!=OK:
			break
	# Non-feed bots consume their own stream (decode and apply the command tables).
	if c.id!=world_feed:
		while lockstep.advance_command()!=0:
			c.kf.advanced+=1

func _bot_world_step() -> void:
	if world_feed<0 or gameplay==null:
		return
	var feed: Dictionary={}
	for c in connections:
		if c.id==world_feed:
			feed=c
	var lockstep: Object=feed.lockstep
	var buffered: int=int(lockstep.get_status().get("buffered_commands",0))
	var steps: int=mini(buffered,1 if buffered<=4 else 3)
	for _step in range(steps):
		var tick: int=lockstep.advance_command()
		if tick==0:
			break
		var inputs: Array=lockstep.get_inputs()
		gameplay.step(tick,inputs)
		feed.kf.advanced+=1
	if steps>0:
		# Fresh exact positions for every bot brain in this process.
		var states: PackedFloat32Array=physics.get_body_states(gameplay.player_ids)
		for id in range(config.total):
			bot_positions[id]=Vector3(states[id*13],states[id*13+1],states[id*13+2])
		for c in connections:
			c.position=bot_positions.get(c.id,c.position)
			var neighbours := {}
			for id in bot_positions:
				if id!=c.id and bot_positions[id].distance_squared_to(c.position)<9.0:
					neighbours[id]=bot_positions[id]
			c.neighbors=neighbours

func _bot_input(c: Dictionary, direction: Vector3, flags: int) -> void:
	# Real-client input path: one quantized input per tick, sent redundantly.
	if c.stance&(64|128|256):
		direction=Vector3.ZERO
	c.direction=direction
	var input: PackedByteArray=GAMEPLAY.encode_input(direction,flags,c.facing)
	var recorded: int=c.lockstep.record_input(int(c.ctick)+1,input)
	if recorded==ERR_OUT_OF_MEMORY:
		# No acknowledgement for a whole history (an outage): keep playing and let the
		# server skip the oldest inputs it never received.
		c.lockstep.discard_oldest_inputs(32)
		recorded=c.lockstep.record_input(int(c.ctick)+1,input)
	if recorded==OK:
		c.ctick+=1
	# Inputs go out every 60 Hz pump, each batch resending the unacknowledged window.
	if c.ready and c.pumped and _unreliable_waiting(c,INPUT)<2:
		var batch: PackedByteArray=c.lockstep.pack_inputs(30,860)
		if batch.size()>0:
			_send_unreliable(c,batch,INPUT)

func _read_keyframe(c: Dictionary) -> void:
	var packet: Dictionary=c.session.read_packet(KEYFRAME_LANE)
	if packet.error!=OK:
		return
	_acked(c.session.acknowledge_packet(packet.message,packet.binding_generation,KEYFRAME_LANE),"Keyframe receipt")
	var bytes: PackedByteArray=packet.payload
	if bytes.size()<12 or bytes.slice(0,2).get_string_from_ascii()!="KF":
		return
	var id: int=bytes.decode_u32(4)
	if id!=det.chunk_id:
		det.chunk_id=id
		det.chunks={}
	det.chunks[bytes.decode_u8(2)]=bytes.slice(12)
	if det.chunks.size()<bytes.decode_u8(3):
		return
	var body := PackedByteArray()
	for index in range(bytes.decode_u8(3)):
		body.append_array(det.chunks[index])
	det.chunks={}
	if not check(body.size()==bytes.decode_u32(8) and body.size()>8,"Keyframe assembly"):
		return
	var table_size: int=body.decode_u32(4)
	var restored=GAMEPLAY.new()
	if not check(restored.restore(body.slice(8+table_size),int(_tuning("physics_workers",1))) and lockstep_client.load_keyframe(body.slice(8,8+table_size))==OK,"Deterministic keyframe join"):
		return
	if gameplay!=null:
		gameplay.close()
	gameplay=restored
	physics=gameplay.world
	det.loaded=true
	det.tick=body.decode_u32(0)
	det.playing=false
	det.keyframes+=1
	det.last_processed=-1
	_capture_render_state()
	det.prev=det.curr.duplicate()
	c.reconciled=false

func _read_commands(c: Dictionary) -> void:
	# Drain every queued batch: one read per frame lets bunched arrivals overflow the
	# unreliable receiver and turns jitter into gaps.
	for _message in range(16):
		var packet: Dictionary=c.session.read_packet(COMMAND_LANE)
		if packet.error!=OK:
			return
		var acked: int=c.session.acknowledge_packet(packet.message,packet.binding_generation,COMMAND_LANE)
		_acked(acked,"Command receipt")
		var bytes: PackedByteArray=packet.payload
		if bytes.size()>=8:
			det.rx_bytes=int(det.get("rx_bytes",0))+bytes.size()
			lockstep_client.acknowledge_inputs(bytes.decode_u32(0))
			c.received+=1
			c.last_state=now
			if det.loaded:
				lockstep_client.accept_commands(bytes.slice(8))
				det.lag.append(bytes.decode_u32(4)-int(det.tick))
				if det.lag.size()>300:
					det.lag.pop_front()
		if acked!=OK:
			return

func _capture_render_state() -> void:
	# One native read for every rendered body; flags derived once per simulation tick.
	if det.render_ids.is_empty():
		for id in visuals:
			det.render_keys.append(id)
			det.render_ids.append(GAMEPLAY.PLAYER_BASE+id if id<config.total else id)
	var bodies: PackedFloat32Array=physics.get_body_states(det.render_ids)
	var state: Dictionary={}
	for i in range(det.render_keys.size()):
		var id: int=det.render_keys[i]
		var o: int=i*13
		var position := Vector3(bodies[o],bodies[o+1],bodies[o+2])
		var velocity := Vector3(bodies[o+7],bodies[o+8],bodies[o+9])
		state[id]=[position,Quaternion(bodies[o+3],bodies[o+4],bodies[o+5],bodies[o+6]),velocity,gameplay.motion_flags(id,int(det.tick),position,velocity) if id<config.total else 0]
	det.curr=state

func _advance_deterministic(c: Dictionary) -> void:
	if not det.loaded:
		return
	# Playout over buffered command ticks: a small adaptive buffer absorbs jitter,
	# catch-up runs extra ticks, starvation pauses instead of extrapolating physics.
	var buffered: int=int(lockstep_client.get_status().get("buffered_commands",0))
	if frame%120==0 and diagnostics.size()<300:
		diagnostics.append([snappedf(now,0.01),"det_state",buffered,det.target,int(det.tick),det.lag.back() if not det.lag.is_empty() else -1])
	if frame%600==0 and GAMEPLAY.profile[3]>0 and diagnostics.size()<300:
		var n: float=float(GAMEPLAY.profile[3])
		diagnostics.append([snappedf(now,0.01),"step_split_us",GAMEPLAY.profile[0]/n,GAMEPLAY.profile[1]/n,GAMEPLAY.profile[2]/n])
		GAMEPLAY.profile=[0,0,0,0]
	det.buffered.append(buffered)
	if det.buffered.size()>600:
		det.buffered.pop_front()
	var steps := 0
	if buffered==0:
		if det.playing:
			det.playing=false
			det.starved+=1
			det.target=mini(det.target+2,30)
			det.calm=0
	else:
		if not det.playing and buffered>=det.target:
			det.playing=true
		if det.playing:
			steps=1 if buffered<=det.target+3 else 2 if buffered<=det.target+20 else 3
			det.calm+=1
			if det.calm>=240 and det.target>2:
				det.target-=1
				det.calm=0
	if steps==0:
		det.prev=det.curr
	for step_index in range(steps):
		if step_index>0 and Time.get_ticks_usec()-det_frame_started>int(_tuning("catch_up_budget_usec",10000)):
			break
		var tick: int=lockstep_client.advance_command()
		if tick==0:
			break
		var inputs: Array=lockstep_client.get_inputs()
		det.prev=det.curr
		var step_started := Time.get_ticks_usec()
		gameplay.step(tick,inputs)
		det.step_ms.append(float(Time.get_ticks_usec()-step_started)/1000.0)
		if det.step_ms.size()>600:
			det.step_ms.pop_front()
		det.tick=tick
		det.advanced+=1
		_capture_render_state()
		# Reconcile the predicted avatar against the exact world at the input it consumed.
		# The server relays which of our own input ticks it consumed at this tick.
		var processed: int=lockstep_client.get_processed_tick()
		var own: Vector3=det.curr[HUMAN][0]
		c.position=own
		if processed>0 and processed!=det.last_processed:
			det.last_processed=processed
			_reconcile(c,own,processed,0)

func _reconcile(c: Dictionary, server: Vector3, processed: int, received: int) -> void:
	c.server_received_tick=maxi(c.server_received_tick,received)
	if not c.reconciled:
		# First authoritative state: adopt it and start predicting from here.
		c.predicted=server
		c.prev_predicted=server
		c.render_y=server.y
		c.history.clear()
		c.reconciled=true
		c.server_processed_tick=processed
		return
	if processed<=c.server_processed_tick or not c.history.has(processed):
		return
	c.server_processed_tick=processed
	var error: Vector2=Vector2(server.x,server.z)-c.history[processed]
	c.errors.append(error.length())
	if c.errors.size()>500:
		c.errors.pop_front()
	if error.length()<0.01:
		return
	# Shift the predicted timeline: inputs after the server tick replay from the
	# authoritative position, exactly as a rollback would for kinematic movement.
	for tick in c.history:
		if tick>processed:
			c.history[tick]+=error
	var shift := Vector3(error.x,0,error.y)
	c.predicted+=shift
	c.prev_predicted+=shift
	if error.length()>6.0:
		c.visual_offset=Vector3.ZERO
	else:
		# Keep the rendered avatar where it was and bleed the correction out smoothly.
		c.visual_offset-=shift
		if error.length()>0.25:
			c.corrections+=1

func _write_telemetry(full: bool) -> void:
	# Percentile sorts and simulation probes are recomputed only on full writes
	# (every 30 s and at the end); the 0.5 s readiness writes reuse them, so
	# telemetry never stalls a frame.
	var telemetry_started := Time.get_ticks_usec()
	var rows: Array = []
	for c in connections:
		if not full:
			# Readiness only between full writes: the supervisor's live view needs
			# nothing else, and 101 statistics queries plus a large JSON dump would
			# cost the server a tick.
			rows.append({"id":c.id,"ready":c.ready,"terminal":c.session.get_state()=="NetworkFailed","ever_ready":c.ever_ready,"generation":c.generation,"recoveries":c.recoveries})
			continue
		var stats: Dictionary = c.session.get_statistics()
		if role=="server" and deterministic:
			var stream: Dictionary=lockstep_server.get_stream_status(c.id)
			c.received=int(lockstep_server.get_playout_status(c.id).get("received_tick",0))
			c.applied=int(stream.get("batches",0))
			c.command_bytes=int(stream.get("bytes",0))
		rows.append({"id":c.id,"ready":c.ready,"terminal":c.session.get_state()=="NetworkFailed","ever_ready":c.ever_ready,"generation":c.generation,"recoveries":c.recoveries,"transport_errors":c.transport_errors,"error_codes":c.get("error_codes",{}),"srtt_ms":float(stats.get("smoothed_rtt_us",0))/1000.0,"retry_ticks":int(stats.get("retry_ticks",0)),"cwnd":int(stats.get("congestion_window",0)),"flight":int(stats.get("bytes_in_flight",0)),"unreliable_pending":c.get("unreliable",[]).size(),"enqueue_failures":c.get("enqueue_failures",{}),"network_ready":bool(stats.get("network_ready",false)),"sent":c.sent,"applied":c.applied,"received":c.received,"ack_ms":c.ack_ms,"ack_p95":_pc(c,full,"r1",c.acks,0.95),"ack_p50":_pc(c,full,"r2",c.acks,0.5),"state_age_p50_ms":_pc(c,full,"r3",c.state_ages,0.5),"state_interval_p50_ms":_pc(c,full,"r4",c.state_intervals,0.5),"error_p95":_pc(c,full,"r5",c.errors,0.95),"score":c.score,"position":[c.position.x,c.position.y,c.position.z],"distance":c.distance,"max_pending":c.max_pending,"pending_tickets":c.tickets.size(),"ticket_lanes":c.tickets.map(func(t):return int(t.channel)),"oldest_ticket_ms":0 if c.tickets.is_empty() else Time.get_ticks_msec()-int(c.tickets[0].at),"send_period":c.period,"wire_bytes":c.wire_base+int(stats.get("charged_wire_bytes",0)),"rejected":c.rejected,"stale":c.stale,"corrections":c.corrections,"deterministic":det.loaded if role=="human" else deterministic,"det_tick":det.tick,"det_target":det.target,"det_starved":det.starved,"det_advanced":det.advanced,"keyframes":det.keyframes if role=="human" else int(c.kf.keyframes) if c.has("kf") else int(c.get("keyframes_sent",0)),"bot_advanced":int(c.kf.advanced) if c.has("kf") else 0,"bot_loaded":bool(c.kf.loaded) if c.has("kf") else false,"command_bytes":int(c.get("command_bytes",0)),"command_rtt_ms":float(c.get("srtt",0.0))*1000.0,"command_resends":int(lockstep_server.get_stream_status(c.id).get("rewinds",0)) if role=="server" and deterministic else 0,"decoder":(lockstep_client.get_status() if role=="human" and lockstep_client!=null else c.lockstep.get_status() if c.has("lockstep") else {}),"server_ack":int(lockstep_server.get_playout_status(c.id).get("command_acknowledged",0)) if role=="server" and deterministic else 0,"sent_newest":int(lockstep_server.get_stream_status(c.id).get("sent_tick",0)) if role=="server" and deterministic else 0,"server_tick":physics.get_tick() if physics!=null else 0,"det_lag_p95":_pc(c,full,"r6",det.lag,0.95),"det_lag_p50":_pc(c,full,"r7",det.lag,0.5),"det_buffered_p50":_pc(c,full,"r8",det.buffered,0.5),"det_buffered_p95":_pc(c,full,"r9",det.buffered,0.95),"det_step_ms_p95":_pc(c,full,"r10",det.step_ms,0.95),"playout":lockstep_server.get_playout_status(c.id) if role=="server" and deterministic and c.id==HUMAN else {},"known_entities":c.neighbors.size(),"starvations":c.starvations,"buffer_target":c.buffer_target,"buffer_p50":_pc(c,full,"r11",c.buffer_depths,0.5),"processed_tick":c.processed_tick,"received_tick":c.received_tick,"render_delay_ms":c.render_delay*1000,"exhibit_received":c.exhibit_received,"activities":c.activities,"state_age_p95_ms":_pc(c,full,"r12",c.state_ages,0.95),"state_interval_p95_ms":_pc(c,full,"r13",c.state_intervals,0.95)})
	var report := {"diagnostics":diagnostics,"role":role,"start_unix":config.start_unix,"now":now,"failed":failed,"pid":OS.get_process_id(),"rows":rows,"physics_p95_ms":_pg(full,"g14",physics_ms,0.95),"frames":frame,"performance":{"frame_p50_ms":_pg(full,"g15",frame_times,0.5),"frame_p95_ms":_pg(full,"g16",frame_times,0.95),"frame_p99_ms":_pg(full,"g17",frame_times,0.99),"process_p95_ms":_pg(full,"g18",process_times,0.95),"gpu_p95_ms":_pg(full,"g19",gpu_times,0.95),"physics_interval_p95_ms":_pg(full,"g20",step_intervals,0.95),"samples":frame_times.size(),"server_tick_rate":float(frame)/maxf(now+10,1),"network_p95_ms":_pg(full,"g21",network_times,0.95),"application_p95_ms":_pg(full,"g22",application_times,0.95),"control_p95_ms":_pg(full,"g23",control_times,0.95),"replication_p95_ms":_pg(full,"g24",replication_times,0.95),"publish_p95_ms":_pg(full,"g25",publish_times,0.95),"application_max_ms":_pg(full,"g26",application_times,1.0),"tick_p50_ms":_pg(full,"g27",section_ms.tick,0.5),"tick_p95_ms":_pg(full,"g28",section_ms.tick,0.95),"render_p50_ms":_pg(full,"g29",section_ms.render,0.5),"render_p95_ms":_pg(full,"g30",section_ms.render,0.95),"animate_p50_ms":_pg(full,"g31",section_ms.animate,0.5),"animate_p95_ms":_pg(full,"g32",section_ms.animate,0.95),"animated_p50":_pg(full,"g33",section_ms.animated,0.5),"engine_process_p50_ms":_pg(full,"g34",section_ms.engine_process,0.5),"engine_process_p95_ms":_pg(full,"g35",section_ms.engine_process,0.95),"engine_physics_p50_ms":_pg(full,"g36",section_ms.engine_physics,0.5),"engine_physics_p95_ms":_pg(full,"g37",section_ms.engine_physics,0.95),"engine_physics_window_max_ms":Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS)*1000}}
	report.live=_live_block()
	if role=="server" and deterministic:
		report.command_totals=lockstep_server.get_command_totals()
	if role=="server" and (full or not telemetry_cache.has("props")):
		var props: Array = []
		for id in range(40):
			var p: Vector3 = physics.get_body_state(PROP_BASE+id).position
			props.append([p.x,p.y,p.z])
		telemetry_cache.props=props
		telemetry_cache.simulation=simulation.telemetry(physics)
	if role=="server":
		report.props=telemetry_cache.props
		report.simulation=telemetry_cache.simulation
	var file := FileAccess.open(config.telemetry+".tmp",FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(report))
		file.close()
		DirAccess.rename_absolute(config.telemetry+".tmp",config.telemetry)
	var cost: float=float(Time.get_ticks_usec()-telemetry_started)/1000.0
	if cost>4.0 and diagnostics.size()<400:
		diagnostics.append([snappedf(now,0.01),"telemetry_ms",cost,full])

func _recent(values: Array, count: int) -> Array:
	# [mean, max] of the newest samples without sorting.
	var n: int=mini(count,values.size())
	if n==0:
		return [0.0,0.0]
	var total := 0.0
	var peak := 0.0
	for i in range(values.size()-n,values.size()):
		total+=float(values[i])
		peak=maxf(peak,float(values[i]))
	return [snappedf(total/n,0.01),snappedf(peak,0.01)]

func _live_block() -> Dictionary:
	# Live state of the new systems for the supervisor, the HUD and receipts.
	var elapsed: float=maxf(now-live_last_time,0.001)
	var live := {"now":snappedf(now,0.01),"tick_rate":snappedf(float(frame-live_last_frame)/elapsed,0.1)}
	live_last_time=now
	live_last_frame=frame
	if role=="server":
		live.interval_ms=_recent(step_intervals,120)
		live.application_ms=_recent(application_times,120)
		live.network_ms=_recent(network_times,120)
		live.physics_ms=_recent(physics_ms,120)
		live.replication_ms=_recent(replication_times,120)
		live.control_ms=_recent(control_times,120)
		if deterministic:
			live.streams=lockstep_server.get_streams_summary()
			live.command_totals=lockstep_server.get_command_totals()
			live.joining=joining.size()
	elif role=="human":
		live.frame_ms=_recent(frame_times,120)
		live.animate_ms=_recent(section_ms.animate,120)
		live.render_ms=_recent(section_ms.render,120)
		if not connections.is_empty():
			live.det=_det_live(connections[0])
	else:
		var loaded := 0
		var keyframes := 0
		var newest := 0
		for c in connections:
			if c.has("kf"):
				loaded+=int(c.kf.loaded)
				keyframes+=int(c.kf.keyframes)
				newest=maxi(newest,int(c.lockstep.get_status().get("newest_command_tick",0)) if c.ready else 0)
		live.bots=connections.size()
		live.loaded=loaded
		live.keyframes=keyframes
		live.newest_command_tick=newest
		live.world_tick=physics.get_tick() if physics!=null else 0
		live.interval_ms=_recent(step_intervals,120)
	return live

func _det_live(c: Dictionary) -> Dictionary:
	# The playable client's lockstep, prediction and transport state.
	var stats: Dictionary=c.session.get_statistics() if c.session!=null else {}
	var status: Dictionary=lockstep_client.get_status() if lockstep_client!=null else {}
	var elapsed: float=maxf(now-float(det.get("rx_at",now-1.0)),0.001)
	var rx_rate: float=float(int(det.get("rx_bytes",0))-int(det.get("rx_mark",0)))/elapsed/1000.0
	det.rx_mark=int(det.get("rx_bytes",0))
	det.rx_at=now
	var lag: int=int(det.lag.back()) if not det.lag.is_empty() else 0
	return {"loaded":det.loaded,"tick":int(det.tick),"lag_ticks":lag,"lag_ms":snappedf(lag*1000.0/60.0,0.1),
		"buffered":int(status.get("buffered_commands",0)),"parked":int(status.get("deferred_commands",0)),
		"target":det.target,"starved":det.starved,"keyframes":det.keyframes,
		"inputs_pending":int(status.get("unacknowledged_inputs",0)),"corrections":c.corrections,
		"error_cm":snappedf(float(c.errors.back())*100.0,0.1) if not c.errors.is_empty() else 0.0,
		"step_ms":_recent(det.step_ms,60),"srtt_ms":snappedf(float(stats.get("smoothed_rtt_us",0))/1000.0,0.1),
		"rto_ms":snappedf(float(stats.get("retransmit_timeout_us",0))/1000.0,0.1),"retry_ticks":int(stats.get("retry_ticks",0)),
		"cwnd_kb":snappedf(float(stats.get("congestion_window",0))/1024.0,0.1),"in_flight_kb":snappedf(float(stats.get("bytes_in_flight",0))/1024.0,0.1),
		"commands_kBps":snappedf(rx_rate,0.01),"ready":c.ready,"generation":c.generation}

func _pc(c: Dictionary, full: bool, key: String, values: Array, percentile: float) -> float:
	if not c.has("pcache"):
		c.pcache={}
	if full or not c.pcache.has(key):
		c.pcache[key]=_percentile(values,percentile)
	return float(c.pcache[key])

func _pg(full: bool, key: String, values: Array, percentile: float) -> float:
	if full or not telemetry_cache.has(key):
		telemetry_cache[key]=_percentile(values,percentile)
	return float(telemetry_cache[key])

func _percentile(values: Array, percentile: float) -> float:
	if values.is_empty():
		return 0
	var sorted := values.duplicate()
	sorted.sort()
	return float(sorted[mini(sorted.size()-1,floori(float(sorted.size()-1)*percentile))])

func _build_view() -> void:
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(),true)
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
	for id in range(8):
		_box(world,_goal(id)+Vector3.UP*0.1,Vector3(4,0.2,4),COLORS[id%5])
		_box(world,_goal(id)+Vector3.UP*2.5,Vector3(0.2,5,0.2),COLORS[id%5])
	exhibit=EXHIBIT_VIEW.new()
	world.add_child(exhibit)
	camera=Camera3D.new()
	camera.fov=65
	world.add_child(camera)
	var canvas := CanvasLayer.new()
	add_child(canvas)
	hud_panel = ColorRect.new()
	var panel: ColorRect = hud_panel
	panel.color=Color(0.025,0.035,0.06,0.92)
	panel.position=Vector2(20,20)
	panel.size=Vector2(710,185)
	canvas.add_child(panel)
	title=Label.new()
	title.position=Vector2(40,30)
	title.add_theme_font_size_override("font_size",21)
	title.text="SUPERPOS LOCKSTEP  /  %d PLAYERS  /  60 Hz" % int(config.total)
	canvas.add_child(title)
	hud=Label.new()
	hud.position=Vector2(40,66)
	hud.add_theme_font_size_override("font_size",16)
	canvas.add_child(hud)
	# Controls stay out of the way: one hint line, the full list on F1.
	controls_label=Label.new()
	controls_label.add_theme_font_size_override("font_size",15)
	controls_label.anchor_top=1.0
	controls_label.anchor_bottom=1.0
	controls_label.position=Vector2(25,get_viewport().get_visible_rect().size.y-34)
	controls_label.add_theme_color_override("font_shadow_color",Color(0,0,0,0.8))
	canvas.add_child(controls_label)
	debug_detail=bool(_tuning("hud_detail",0))
	_update_controls()
	for id in range(config.total):
		var character: Node3D = CHARACTER_VIEW.new()
		world.add_child(character)
		check(character.configure(Color("e8f7ff") if id==HUMAN else COLORS[mini(4,id*5/maxi(int(config.get("bots",100)),1))],id%2==1),"UAL animated mannequin setup")
		# Spread animation LOD phases so reduced-rate characters never all update in one frame.
		character.lod_phase=id+1
		character.visible=false
		visuals[id]=character
	for id in range(40):
		visuals[PROP_BASE+id]=_box(world,Vector3.ZERO,Vector3.ONE*1.1,Color("ffc170"))
		visuals[PROP_BASE+id].visible=false
	for id in range(22):
		var float_body := id<6
		var size := Vector3.ONE if float_body else Vector3(0.24,1.6,1)
		var color: Color = [Color("62efd1"),Color("f6d67d"),Color("f584a1")][id%3] if float_body else Color("83baff")
		visuals[SIMULATION.FLOAT_BASE+id]=_box(world,Vector3.ZERO,size,color)
		visuals[SIMULATION.FLOAT_BASE+id].visible=false
	var dynamic := PLAYGROUND.dynamic_bodies()
	for i in range(dynamic.size()):
		var body := MeshInstance3D.new()
		body.mesh=PLAYGROUND.mesh_for(dynamic[i])
		body.material_override=_material(dynamic[i][4])
		world.add_child(body)
		body.visible=false
		visuals[PLAYGROUND.BASE+i]=body
	for fixed in PLAYGROUND.static_bodies():
		_box(world,fixed[0],fixed[1]*2,fixed[2])

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
	var frame_usec := Time.get_ticks_usec()
	if last_frame_usec>0 and now>5:
		frame_times.append(float(frame_usec-last_frame_usec)/1000.0)
		process_times.append(Performance.get_monitor(Performance.TIME_PROCESS)*1000.0)
		gpu_times.append(RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()))
		if frame_times.size()>3600:
			frame_times.pop_front()
			process_times.pop_front()
			gpu_times.pop_front()
	# Slow-frame breakdown: what the previous frame spent its time on.
	if last_frame_usec>0 and now>5 and frame_usec-last_frame_usec>25000 and diagnostics.size()<300:
		diagnostics.append([snappedf(now,0.01),"slow_frame",float(frame_usec-last_frame_usec)/1000.0,frame_physics_steps,float(frame_physics_usec)/1000.0,float(CHARACTER_VIEW.animate_usec)/1000.0,CHARACTER_VIEW.animated,float(last_process_usec)/1000.0])
	frame_physics_usec=0
	frame_physics_steps=0
	det_frame_fresh=true
	last_frame_usec=frame_usec
	var c: Dictionary = connections[0]
	# Own avatar: physics-tick prediction interpolated to the render frame, plus a correction
	# offset that decays instead of dragging the avatar back (no rubber band on reconcile).
	c.visual_offset*=exp(-delta*7.0)
	c.render_y=lerpf(c.render_y,c.position.y,1-exp(-delta*12))
	var own: Vector3=c.prev_predicted.lerp(c.predicted,Engine.get_physics_interpolation_fraction())+c.visual_offset
	own.y=c.render_y
	exhibit.target_time=now-c.clock_base-c.render_delay
	var render_started := Time.get_ticks_usec()
	if det.loaded:
		_render_deterministic(c,own,delta)
	_section("render",Time.get_ticks_usec()-render_started)
	_section("engine_process",int(Performance.get_monitor(Performance.TIME_PROCESS)*1000000.0))
	_section("engine_physics",int(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS)*1000000.0))
	_section("animate",CHARACTER_VIEW.animate_usec)
	section_ms.animated.append(float(CHARACTER_VIEW.animated))
	if section_ms.animated.size()>600:
		section_ms.animated.pop_front()
	CHARACTER_VIEW.animate_usec=0
	CHARACTER_VIEW.animated=0
	for id in ([] if det.loaded else visuals.keys()):
		var mesh: Node3D = visuals[id]
		mesh.visible=c.neighbors.has(id)
		if mesh.visible:
			if id==c.id:
				mesh.position=own
			else:
				mesh.position=mesh.position.lerp(_presentation(c,id),1-exp(-delta*25))
			if id>=config.total:
				mesh.quaternion=mesh.quaternion.slerp(c.present_rotation.get(id,c.motions[id].rotation),1-exp(-delta*30))
			if id<config.total:
				var motion: Dictionary = c.motions.get(id,{"velocity":Vector3.ZERO,"flags":1})
				var velocity: Vector3 = c.direction*_speed(c.stance,c.sprinting) if id==c.id and c.direction.length_squared()>0.01 else motion.velocity
				mesh.present(velocity,int(motion.flags),c.facing if id==c.id else float(motion.get("facing",0)))
				# Full-rate animation near the camera, reduced far away, minimal off-screen.
				var distance: float=camera.global_position.distance_to(mesh.position)
				mesh.lod=1 if id==c.id or distance<20 else 8 if not camera.is_position_in_frustum(mesh.position) else 2 if distance<38 else 4
				if mesh.lod_phase==0:
					mesh.lod_phase=id
	camera.position=own+Vector3(sin(yaw)*zoom,zoom*0.65,cos(yaw)*zoom)
	camera.look_at(own)
	draw_timer+=delta
	if draw_timer>0.5:
		# HUD and monitor refresh at 2 Hz: string formatting and JSON parsing stay off
		# the per-frame path.
		draw_timer=0
		var file:=FileAccess.open(config.monitor,FileAccess.READ)
		if file:
			var report=JSON.parse_string(file.get_as_text())
			if report is Dictionary:
				live_report=report
		hud.text=_hud_text(c)
		hud_panel.size=Vector2(maxf(title.get_combined_minimum_size().x,hud.get_combined_minimum_size().x)+40.0,hud.position.y-hud_panel.position.y+hud.get_combined_minimum_size().y+16.0)
	if now>=12 and not capture_saved:
		capture_saved=true
		_capture_view()
	last_process_usec=Time.get_ticks_usec()-frame_usec

func _update_controls() -> void:
	if controls_label==null:
		return
	controls_label.text=("WASD move  SHIFT run  SPACE jump  F impulse  E push  CTRL crouch  C crawl  G/T sit  K/R death/respawn  V body  |  1-5 your network  O outage  |  F3 details  F1 hide" if show_controls
		else "F1 controls   F3 network details")
	controls_label.position.y=get_viewport().get_visible_rect().size.y-34

func _hud_text(c: Dictionary) -> String:
	var d: Dictionary=_det_live(c)
	var server: Dictionary=live_report.get("server",{})
	var frame_ms: Array=_recent(frame_times,120)
	var profile: String=["FIBRE","BROADBAND","WIFI","MOBILE","POOR"][human_profile]
	var lines: Array[String] = []
	var state: String="CONNECTED" if c.ready and det.loaded else "JOINING" if c.ready else "CONNECTING"
	lines.append("%s   ping %.0f ms   lag %.0f ms   %.1f kB/s" % [state,d.srtt_ms,d.lag_ms,d.commands_kBps])
	lines.append("%.0f FPS (%.1f ms)   sim %.1f ms   server %.0f Hz / %.1f ms" % [1000.0/maxf(frame_ms[0],0.001),frame_ms[0],d.step_ms[0],float(server.get("tick_rate",0)),float(server.get("app_ms",0))])
	lines.append("bots ready %d / %d   prediction %s" % [int(live_report.get("bots_ready",0)),int(config.get("bots",0)),"OK" if d.corrections==0 else "%d corrections" % d.corrections])
	lines.append("test %s   your network %s" % [str(live_report.get("phase","WARMUP")).to_lower(),profile.to_lower()])
	if not debug_detail:
		return "
".join(lines)
	lines.append("")
	lines.append("LOCKSTEP  tick %d   buffer %d (target %d)   parked %d   keyframes %d   starved %d" % [d.tick,d.buffered,d.target,d.parked,d.keyframes,d.starved])
	lines.append("PREDICTION  error %.1f cm   unacked inputs %d   sim max %.1f ms" % [d.error_cm,d.inputs_pending,d.step_ms[1]])
	lines.append("TRANSPORT  rto %.0f ms (%d ticks)   window %.1f KB   in flight %.1f KB" % [d.rto_ms,d.retry_ticks,d.cwnd_kb,d.in_flight_kb])
	lines.append("FRAME  max %.1f ms   animate %.1f ms   render %.1f ms" % [frame_ms[1],_recent(section_ms.animate,60)[0],_recent(section_ms.render,60)[0]])
	if not server.is_empty():
		lines.append("SERVER  app max %.1f ms   net %.1f   physics %.1f   publish %.1f   %d B/tick" % [float(server.get("app_max_ms",0)),float(server.get("network_ms",0)),float(server.get("physics_ms",0)),float(server.get("replication_ms",0)),int(server.get("bytes_per_tick",0))])
	for k in live_report.get("cohorts",[]):
		lines.append("  %-9s %3d/%-3d ready   lag %4.0f / %5.0f ms   %4.1f kB/s" % [k.get("name","?"),int(k.get("ready",0)),int(k.get("count",0)),float(k.get("lag_ms_p50",0)),float(k.get("lag_ms_max",0)),float(k.get("kBps",0))])
	return "
".join(lines)

func _render_deterministic(c: Dictionary, own: Vector3, delta: float) -> void:
	# Every body comes from the locally simulated deterministic world, interpolated
	# between the two newest ticks: exact physics at full 60 Hz, no network snapshots.
	var fraction: float=Engine.get_physics_interpolation_fraction()
	for id in visuals:
		var mesh: Node3D=visuals[id]
		if not det.curr.has(id):
			continue
		var now_state: Array=det.curr[id]
		var before: Array=det.prev.get(id,now_state)
		mesh.visible=true
		if id==c.id:
			mesh.position=own
		else:
			mesh.position=before[0].lerp(now_state[0],fraction)
		if id>=config.total:
			mesh.quaternion=before[1].slerp(now_state[1],fraction)
		else:
			var actor: Dictionary=gameplay.actors[id]
			var velocity: Vector3=c.direction*_speed(c.stance,c.sprinting) if id==c.id and c.direction.length_squared()>0.01 else now_state[2]
			mesh.present(velocity,int(now_state[3]),c.facing if id==c.id else float(actor.facing))
			var distance: float=camera.global_position.distance_to(mesh.position)
			# Crowd animation LOD: full rate up close, 20 Hz mid, 10 Hz far, 5 Hz off-screen.
			mesh.lod=(1 if id==c.id or distance<14 else 12 if not camera.is_position_in_frustum(mesh.position) else 3 if distance<32 else 6)*int(_tuning("animation_lod_scale",1))
			if mesh.lod_phase==0:
				mesh.lod_phase=id
	var points: PackedVector3Array=gameplay.simulation.cloth_points()
	var poses: Array=gameplay.simulation.joint_poses()
	var rotations: Array[Quaternion]=[]
	rotations.assign(poses[1])
	exhibit.set_live(points,poses[0],rotations)
	c.score=int(gameplay.actors[HUMAN].score)

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
		if event.physical_keycode==KEY_C:
			human_stance=0 if human_stance==16 else 16
		if event.physical_keycode==KEY_G:
			human_stance=0 if human_stance==64 else 64
		if event.physical_keycode==KEY_T:
			human_stance=0 if human_stance==128 else 128
		if event.physical_keycode==KEY_K:
			human_stance=256
		if event.physical_keycode==KEY_R:
			human_stance=0
			respawn_requested=true
		if event.physical_keycode==KEY_V:
			var character: Node3D = visuals[connections[0].id]
			check(character.configure(Color("e8f7ff"),not character.female),"Swap mannequin")
		if event.physical_keycode>=KEY_1 and event.physical_keycode<=KEY_5:
			human_profile=event.physical_keycode-KEY_1
			_write_human_network()
		if event.physical_keycode==KEY_O:
			human_offline_until=Time.get_unix_time_from_system()+6.0
			_write_human_network()
		if event.physical_keycode==KEY_F3:
			debug_detail=not debug_detail
			draw_timer=1.0
		if event.physical_keycode==KEY_F1:
			show_controls=not show_controls
			_update_controls()
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
	if simulation!=null:
		simulation.close()
	for c in connections:
		if c.session!=null:
			c.session.close_checked()

func _speed(stance: int, sprinting: bool) -> float:
	if stance&(64|128|256):
		return 0
	if stance&16:
		return 1.2
	if stance&8:
		return 2.0
	if stance&32:
		return 1.1
	return 8.5 if sprinting else 4.5

func _mass(half_height: float) -> float:
	return 0.85*(PI*0.35*0.35*(half_height*2)+4.0/3.0*PI*pow(0.35,3))

func _accept_exhibit(c: Dictionary, bytes: PackedByteArray) -> bool:
	if bytes.size()>=20 and bytes.slice(0,4).get_string_from_ascii()=="X103":
		return _accept_compact_exhibit(c,bytes)
	var kind := bytes.decode_u32(12)
	var count := bytes.decode_u32(16)
	var seq := bytes.decode_u32(4)
	if not check(kind<=1 and count==(64 if kind==0 else 6) and bytes.size()==20+count*(12 if kind==0 else 28) and seq>c.last_exhibit,"Exhibit framing and monotonic sequence"):
		return false
	var points := PackedVector3Array()
	var rotations: Array[Quaternion] = []
	for i in range(count):
		var offset := 20+i*(12 if kind==0 else 28)
		for f in range(3 if kind==0 else 7):
			if not check(is_finite(bytes.decode_float(offset+4*f)) and absf(bytes.decode_float(offset+4*f))<100,"Finite bounded native exhibit state"):
				return false
		points.append(Vector3(bytes.decode_float(offset),bytes.decode_float(offset+4),bytes.decode_float(offset+8)))
		if kind==1:
			var q := Quaternion(bytes.decode_float(offset+12),bytes.decode_float(offset+16),bytes.decode_float(offset+20),bytes.decode_float(offset+24))
			if not check(absf(q.length()-1)<0.01,"Normalized joint pose"):
				return false
			rotations.append(q)
	if not _publish(c,bytes):
		return false
	c.last_exhibit=seq
	c.exhibit_received+=1
	if role=="human":
		exhibit.apply_frame(kind,points,rotations)
	return true

func _accept_compact_exhibit(c: Dictionary, bytes: PackedByteArray) -> bool:
	var seq := bytes.decode_u32(4)
	var time := bytes.decode_float(12)
	var points_count := bytes.decode_u16(16)
	var joints_count := bytes.decode_u16(18)
	if not check(points_count==64 and joints_count==6 and bytes.size()==20+64*6+6*14 and seq>c.last_exhibit and is_finite(time),"Compact exhibit framing and monotonic sequence"):
		return false
	var points := PackedVector3Array()
	for i in range(64):
		var offset := 20+i*6
		points.append(SIMULATION.CLOTH+Vector3(bytes.decode_s16(offset),bytes.decode_s16(offset+2),bytes.decode_s16(offset+4))/1000.0)
	var positions := PackedVector3Array()
	var rotations: Array[Quaternion] = []
	for i in range(6):
		var offset := 20+64*6+i*14
		positions.append(SIMULATION.JOINTS+Vector3(bytes.decode_s16(offset),bytes.decode_s16(offset+2),bytes.decode_s16(offset+4))/1000.0)
		var q := Quaternion(bytes.decode_s16(offset+6)/32767.0,bytes.decode_s16(offset+8)/32767.0,bytes.decode_s16(offset+10)/32767.0,bytes.decode_s16(offset+12)/32767.0)
		if not check(absf(q.length()-1)<0.01,"Normalized joint pose"):
			return false
		rotations.append(q.normalized())
	if not _publish(c,bytes):
		return false
	c.last_exhibit=seq
	c.exhibit_received+=1
	if role=="human" and not det.loaded:
		exhibit.push_frame(time,points,positions,rotations)
	return true

func _presentation(c: Dictionary, id: int) -> Vector3:
	# Interpolate in server time on this entity's own timeline. The buffer tracks measured
	# arrival jitter; entities updated less often fall back to bounded velocity extrapolation.
	var target_time: float = now-c.clock_base-c.render_delay
	var timeline: Array=c.samples.get(id,[])
	if timeline.is_empty():
		return c.neighbors[id]
	var before: Array=[]
	var after: Array=[]
	for sample in timeline:
		if sample[0]<=target_time:
			before=sample
		else:
			after=sample
			break
	if not before.is_empty() and not after.is_empty():
		# Cubic Hermite on the horizontal plane (velocity-aware curves), linear vertical.
		var span: float=maxf(after[0]-before[0],0.001)
		var t: float=clampf((target_time-before[0])/span,0,1)
		var linear: Vector3=before[1].lerp(after[1],t)
		var curve: Vector3=before[1].cubic_interpolate(after[1],before[1]-before[2]*span,after[1]+after[2]*span,t)
		c.present_rotation[id]=before[3].slerp(after[3],t)
		return Vector3(curve.x,linear.y,curve.z) if span<0.5 else linear
	if not before.is_empty():
		c.present_rotation[id]=before[3]
		return before[1]+before[2]*clampf(target_time-before[0],0,0.5)
	return after[1]

func _state_lane(c: Dictionary) -> int:
	return STATE if _pending(c,STATE)==0 else -1

func _accept_state(c: Dictionary, bytes: PackedByteArray) -> bool:
	if bytes.size()>=8 and bytes.decode_u32(4)<=c.accepted_sequence:
		return true
	if not check(bytes.size()>=64 and bytes.slice(0,4).get_string_from_ascii()=="S104","State framing"):
		return false
	var count := bytes.decode_u32(20)
	var seq := bytes.decode_u32(4)
	if not check(count>=1 and count<=STATE_ENTITIES and bytes.size()==STATE_HEADER+count*36 and seq>c.accepted_sequence and bytes.decode_u32(STATE_HEADER)==c.id,"Bounded interest / own identity / sequence"):
		return false
	var neighbors: Dictionary = {}
	var motions: Dictionary = {}
	for n in range(count):
		var offset := STATE_HEADER+n*36
		var id := bytes.decode_u32(offset)
		for f in range(5):
			if not check(is_finite(bytes.decode_float(offset+4+f*4)),"Finite server state"):
				return false
		if not check(not neighbors.has(id) and (id<config.total or (id>=PROP_BASE and id<PROP_BASE+40) or (id>=SIMULATION.FLOAT_BASE and id<SIMULATION.FLOAT_BASE+22) or PLAYGROUND.is_dynamic(id)),"Interest identity uniqueness"):
			return false
		neighbors[id] = Vector3(bytes.decode_float(offset+4),bytes.decode_float(offset+8),bytes.decode_float(offset+12))
		var flags := bytes.decode_u32(offset+24)
		if not check(flags<67108864,"Bounded locomotion flags"):
			return false
		var rotation := Quaternion(float(bytes.decode_s16(offset+28))/32767,float(bytes.decode_s16(offset+30))/32767,float(bytes.decode_s16(offset+32))/32767,float(bytes.decode_s16(offset+34))/32767)
		if not check(absf(rotation.length()-1)<0.01,"Finite normalized body rotation"):
			return false
		motions[id]={"rotation":rotation.normalized(),"velocity":Vector3(bytes.decode_float(offset+16),0,bytes.decode_float(offset+20)),"flags":flags&1023,"facing":float(flags>>10)/65535.0*TAU-PI}
	if not _publish(c,bytes):
		return false
	var p: Vector3 = neighbors[c.id]
	if role=="human" and not det.loaded:
		_reconcile(c,p,bytes.decode_u32(12),bytes.decode_u32(28))
	elif role=="human":
		pass
	else:
		c.errors.append(c.predicted.distance_to(p))
	if c.errors.size()>500:
		c.errors.pop_front()
	c.position = p
	var stamp := bytes.decode_float(24)
	if not check(is_finite(stamp) and stamp>=-15 and stamp<now+2,"Server presentation timestamp"):
		return false
	c.server_time=stamp
	c.own_velocity=motions[c.id].velocity
	if role=="human":
		# now-stamp is one-way delay plus any clock offset; its floor is the offset estimate.
		c.lags.append(now-stamp)
		if c.lags.size()>90:
			c.lags.pop_front()
		var sorted: Array=c.lags.duplicate()
		sorted.sort()
		c.clock_base=sorted[0]
		var spread: float=sorted[floori((sorted.size()-1)*0.95)]-sorted[0]
		c.render_delay=lerpf(c.render_delay,clampf(spread+c.period*1.5,0.05,0.45),0.1)
	if now>5:
		c.state_ages.append(maxf(0,now-stamp)*1000)
		if c.last_delivery>5:
			c.state_intervals.append((now-c.last_delivery)*1000)
	if c.id==HUMAN and c.last_delivery>0 and now-c.last_delivery>0.4 and diagnostics.size()<200:
		diagnostics.append([snappedf(now,0.01),"state_gap",snappedf((now-c.last_delivery)*1000,1),snappedf((now-stamp)*1000,1)])
	c.last_delivery=now
	if c.state_ages.size()>500:
		c.state_ages.pop_front()
		c.state_intervals.pop_front()
	if role=="human":
		# Persistent world: entities stay loaded; each keeps its own timeline of samples.
		for id in neighbors:
			var timeline: Array=c.samples.get(id,[])
			timeline.append([stamp,neighbors[id],motions[id].velocity,motions[id].rotation])
			if timeline.size()>12:
				timeline.pop_front()
			c.samples[id]=timeline
			c.neighbors[id]=neighbors[id]
			c.motions[id]=motions[id]
	else:
		c.neighbors = neighbors
		c.motions = motions
	c.accepted_sequence = seq
	c.last_state = now
	c.goal = bytes.decode_u32(16) % 8
	c.score = floori(float(bytes.decode_u32(16))/8.0)
	c.received += 1
	return true

func _body_record(id: int,state: Dictionary) -> PackedByteArray:
	var record := PackedByteArray()
	record.resize(36)
	record.encode_u32(0,id)
	var p: Vector3 = state.position
	var v: Vector3 = state.linear_velocity
	for n in range(5):
		record.encode_float(4+n*4,[p.x,p.y,p.z,v.x,v.z][n])
	var flags := 0
	if id<config.total:
		var actor: Dictionary=gameplay.actors[id]
		flags=(1 if _grounded(p,v) else 0) | (2 if physics.get_tick()<int(actor.pose_until) else 0) | int(actor.stance) | (4 if actor.sprinting else 0) | (512 if SIMULATION.in_water(p) and p.y<2.5 else 0)
		var heading := int(round((wrapf(actor.facing,-PI,PI)+PI)/TAU*65535))
		flags |= heading<<10
	record.encode_u32(24,flags)
	var q: Quaternion=state.rotation
	for axis in range(4):
		record.encode_s16(28+axis*2,int(round(clampf([q.x,q.y,q.z,q.w][axis],-1,1)*32767)))
	return record
