extends SceneTree

var world: SuperposWorld
var session: SuperposSession
var server := false
var family := "ipv4"
var scenario := "accepted"
var started := Time.get_ticks_msec()
var completed := 0
var ticket := {}
var schema: SuperposSchema
var canonical := PackedByteArray()
var checks := 0
var failed_peer_ping := 0
var failed_peer_accepted_probes := 0
var resize_requested := false
var resize_armed := false
var resize_rejected := false
# Set before quit(): the world is freed and must not be touched again.
var finished := false

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		push_error(message)
		quit(1)
		assert(condition, message)

func _initialize() -> void:
	var arguments := OS.get_cmdline_user_args()
	var probe_argument := arguments.find("--superpos-native-reload-probe")
	if probe_argument >= 0:
		arguments.remove_at(probe_argument)
	check(arguments.size() == 4, "four fixture arguments")
	server = arguments[0] == "server"
	family = arguments[2]
	scenario = arguments[3]
	var port := int(arguments[1])
	var field := SuperposField.new()
	field.field_id = 2 if scenario == "schema_mismatch" and not server else 1
	schema = SuperposSchema.new()
	schema.schema_id = 73
	schema.fields = [field]
	world = SuperposWorld.new()
	world.schemas = [schema]
	world.max_objects = 4
	world.authority_epoch = SuperposUInt64.from_decimal("9223372036854775808").value
	world.automatic_ticks = true
	check(world.configure() == OK, "native owner World configured")
	session = world.get_session()
	check(session.advance_tick() == ERR_BUSY and world.advance_tick() == ERR_BUSY, "manual stepping rejected while physics phase owns clock")
	var key := PackedByteArray()
	key.resize(32)
	for i in range(32):
		key[i] = 33 + i
	if scenario == "key_mismatch" and not server:
		key[0] = 99
	var zero_key := PackedByteArray()
	zero_key.resize(32)
	var ip := "::1" if family == "ipv6" else "127.0.0.1"
	var identity: int = SuperposUInt64.from_decimal("18446744073709551614").value
	var session_id: int = SuperposUInt64.from_decimal("18446744073709551615").value
	check(session.configure_udp(server, ip, 0, ip, port + 1, session_id, identity, key) == ERR_INVALID_PARAMETER, "invalid endpoint rejects before networking")
	check(session.configure_udp(server, "localhost", port, ip, port + 1, session_id, identity, key) == ERR_INVALID_PARAMETER, "numeric endpoint contract enforced")
	check(session.configure_udp(server, ip, port, ip, port + 1, session_id, identity, zero_key) == ERR_UNAUTHORIZED, "zero admission key rejected")
	var configured := session.configure_udp(server, ip, port if server else port + 1, ip, port + 1 if server else port, session_id, identity, key)
	check(configured == OK, "native borrowed-PSA authenticated UDP configured")
	check(session.get_state() == "NetworkConnecting" and not session.get_admission_state().ready, "connection is not admission readiness")
	check(session.get_admission_state().simulation_sha256.length() == 64, "actual codec SHA256 declared")
	check(session.enqueue_packet(PackedByteArray([1])).error != OK, "send rejected before capability admission")
	canonical = SuperposUInt64.to_bytes(-1)
	if server:
		check(session.spawn_object(schema.schema_id, 0, canonical) != 0, "server canonical object created")
	root.add_child(world)

func _process(_delta: float) -> bool:
	# Exported dispatch loops attach this script and defer _initialize, so a frame
	# can arrive before the session exists; quit() also lands after the frame.
	if finished or session == null:
		return false
	var elapsed := Time.get_ticks_msec() - started
	if completed:
		if scenario == "peer_closed" and not server:
			check(elapsed < 22000, "bounded failed-peer deadline")
			if session.get_state() == "NetworkFailed":
				check(not session.get_admission_state().ready and not session.get_statistics().network_ready, "native transport failure revokes admission readiness")
				check(session.enqueue_packet(PackedByteArray([1])).error != OK, "failed native transport rejects new packets")
				print("SUPERPOS_EGP_UDP_FAILED_PEER family=", family, " role=client checks=", checks,
					" accepted_probes=", failed_peer_accepted_probes, " failure_elapsed_ms=", elapsed)
				world.close()
				world.queue_free()
				finished = true
				quit(0)
			# The Session has eight bounded message reservations. One probe per
			# four seconds stays below that capacity for the entire unchanged
			# 22-second deadline while a closed UDP peer produces transient ICMP.
			elif Time.get_ticks_msec() - failed_peer_ping > 4000:
				failed_peer_ping = Time.get_ticks_msec()
				var probe := session.enqueue_packet(PackedByteArray([1]), 0)
				check(probe.error == OK, "bounded failed-peer carrier probe accepted before native failure error=" + str(probe.error) + " state=" + session.get_state() + " elapsed_ms=" + str(elapsed))
				if probe.error == OK:
					failed_peer_accepted_probes += 1
			return false
		if Time.get_ticks_msec() - completed > (1500 if server else 1000):
			world.close()
			world.queue_free()
			finished = true
			quit(0)
		return false
	if scenario in ["schema_mismatch", "key_mismatch"] and session.get_state() == "NetworkFailed":
		check(not session.get_admission_state().ready, "incompatible/unauthenticated peer never ready")
		print("SUPERPOS_EGP_UDP_REJECTED scenario=", scenario, " role=", "server" if server else "client", " checks=", checks)
		world.close()
		world.queue_free()
		finished = true
		quit(0)
		return false
	check(elapsed < 22000, "bounded native network deadline")
	if session.get_state() == "NetworkFailed":
		check(false, "native transport failure: " + str(session.get_admission_state()))
	if not session.get_admission_state().ready:
		return false
	var admission := session.get_admission_state()
	check(admission.recovery == "None" and admission.history_ticks == 0 and admission.capabilities == 0, "canonical-only manifest does not claim physics or recovery")
	check(admission.maximum_encoded_bytes == 65536, "negotiated message bound")
	if server:
		if ticket.is_empty():
			var payload := PackedByteArray()
			payload.resize(65536)
			for i in range(payload.size()):
				payload[i] = i % 251
			for i in range(8):
				payload[i] = canonical[i]
			ticket = session.enqueue_packet(payload, 31)
			check(ticket.error == OK and ticket.outcome == "Accepted", "64 KiB accepted on logical channel 31")
			check(ticket.has("connection_epoch") and not ticket.has("authority_epoch"), "delivery ticket names its connection epoch")
			check(session.get_packet_outcome(ticket.message, ticket.binding_generation + 1, 31).error == ERR_INVALID_PARAMETER, "stale ticket binding rejected")
		var outcome := session.get_packet_outcome(ticket.message, ticket.binding_generation, 31)
		if outcome.error == OK and outcome.outcome == "Applied":
			check(session.retire_packet(ticket.message, ticket.binding_generation, 31) == OK, "applied ticket retired")
			check(session.get_statistics().charged_wire_bytes > 65536, "encrypted transport wire bytes accounted")
			world.automatic_ticks = false
			completed = Time.get_ticks_msec()
			print("SUPERPOS_EGP_UDP_OK family=", family, " role=server checks=", checks)
	else:
		if scenario == "resize_retry" and not resize_armed:
			if not resize_requested:
				var file := FileAccess.open("res://.superpos-reload-request.json", FileAccess.WRITE)
				file.store_string(JSON.stringify({"action": "allocation_fault", "session_id": SuperposUInt64.to_decimal(session.get_instance_id())}))
				file.close()
				resize_requested = true
			if not FileAccess.file_exists("res://.superpos-reload-result.json"):
				return false
			var result: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://.superpos-reload-result.json"))
			check(result.error == OK, "native one-use packet resize fault armed")
			resize_armed = true
		var packet := session.read_packet(31)
		if scenario == "resize_retry" and packet.error == ERR_INVALID_PARAMETER:
			check(not resize_rejected and not packet.has("payload") and not packet.has("message"), "actual failed resize publishes no application packet")
			resize_rejected = true
			packet = session.read_packet(31)
			check(packet.error == OK, "same retained message can be retried after failed resize")
		if packet.error == OK:
			if scenario == "resize_retry":
				check(resize_rejected, "real resize failure occurred before application ack")
			check(packet.payload.size() == 65536, "complete message copied into bounded native array")
			check(packet.has("connection_epoch") and packet.has("authority_epoch"), "received packet separates connection and world epoch")
			for i in range(8, packet.payload.size()):
				check(packet.payload[i] == i % 251, "complete payload integrity")
			check(packet.payload.slice(0, 8) == canonical, "full-width canonical state retained")
			var handle := session.spawn_object(schema.schema_id, 0, packet.payload.slice(0, 8))
			check(handle != 0 and session.read_object(handle).canonical == canonical, "canonical native state applied after authenticated receive")
			check(session.acknowledge_packet(packet.message, packet.binding_generation + 1, 31) == ERR_INVALID_PARAMETER, "stale application acknowledgement rejected")
			check(session.acknowledge_packet(packet.message, packet.binding_generation, 31) == OK, "application explicitly acknowledges Applied")
			completed = Time.get_ticks_msec()
			print("SUPERPOS_EGP_UDP_OK family=", family, " role=client checks=", checks)
	return false
