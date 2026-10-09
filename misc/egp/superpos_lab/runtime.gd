extends SceneTree

var checks := 0

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		push_error(message)
		quit(1)
		assert(condition, message)

func bits(text: String) -> int:
	var parsed := SuperposUInt64.from_decimal(text)
	check(parsed.error == OK, "u64 decimal parse")
	return parsed.value

func tick_value(session: SuperposSession) -> int:
	var observed := session.read_tick()
	check(observed.error == OK and observed.has("value"), "checked owner tick is available")
	return observed.value

func binding_value(session: SuperposSession) -> int:
	var observed := session.read_binding_generation()
	check(observed.error == OK and observed.has("value"), "checked lifecycle counter is available")
	return observed.value

func packed(handles: Array[int], revisions: Array[int], states: Array[PackedByteArray]) -> PackedByteArray:
	var result := "SPGO".to_ascii_buffer()
	result.resize(12)
	result.encode_u32(4, 1)
	result.encode_u32(8, handles.size())
	for i in range(handles.size()):
		result.append_array(SuperposUInt64.to_bytes(handles[i]))
		result.append_array(SuperposUInt64.to_bytes(revisions[i]))
		var length := PackedByteArray()
		length.resize(4)
		length.encode_u32(0, states[i].size())
		result.append_array(length)
		result.append_array(states[i])
	return result

func reject_wrong_thread(session: SuperposSession, handle: int) -> bool:
	var tick := session.read_tick()
	var binding := session.read_binding_generation()
	var authority := session.read_authority_epoch()
	var good: bool = authority.error == ERR_BUSY and not authority.has("value")
	good = good and tick.error == ERR_BUSY and not tick.has("value") and binding.error == ERR_BUSY and not binding.has("value")
	good = good and session.get_state() == "WrongThread" and session.get_last_error() == ERR_BUSY
	good = good and session.advance_tick() == ERR_BUSY and session.destroy_object(handle) == ERR_BUSY
	good = good and session.publish_packed(PackedByteArray()) == ERR_BUSY
	good = good and session.transfer_ownership(handle, 0, 1) == ERR_BUSY
	good = good and session.spawn_object(1, 0, PackedByteArray()) == 0
	session.close()
	return good

func reject_worker_creation(schema: SuperposSchema) -> bool:
	var isolated := SuperposSession.new()
	return isolated.configure([schema], 4) == ERR_UNAVAILABLE and isolated.get_state() == "Closed"

func _initialize() -> void:
	var edges := ["0", "1", "9007199254740991", "9007199254740992", "9007199254740993",
		"9223372036854775807", "9223372036854775808", "9223372036854775809", "18446744073709551615"]
	for text in edges:
		var value := bits(text)
		check(SuperposUInt64.to_decimal(value) == text, "decimal round trip")
		check(SuperposUInt64.from_bytes(SuperposUInt64.to_bytes(value)).value == value, "byte round trip")
	check(SuperposUInt64.compare(bits("9223372036854775807"), bits("9223372036854775808")) < 0, "unsigned high-bit ordering")
	check(SuperposUInt64.checked_add(bits("9223372036854775807"), 1).value == bits("9223372036854775808"), "monotonic high-bit transition")
	check(SuperposUInt64.checked_add(bits("18446744073709551615"), 1).error != OK, "overflow rejected")
	check(SuperposUInt64.from_decimal("1\\u00002").error != OK, "non-decimal escape rejected")
	var field := SuperposField.new()
	field.field_id = bits("18446744073709551615")
	var schema := SuperposSchema.new()
	schema.schema_id = bits("9223372036854775808")
	schema.fields = [field]
	check(schema.bake().error == OK, "native schema bake")
	check(schema.get_fingerprint().length() == 64, "schema fingerprint")
	var session := SuperposSession.new()
	var fresh_statistics := session.get_statistics()
	check(fresh_statistics.error == ERR_UNCONFIGURED and not fresh_statistics.has("objects"), "fresh statistics report unavailable")
	check(session.configure([schema], 4, bits("9223372036854775808"), 0, 4096) == OK, "core world configure")
	var authority := session.read_authority_epoch()
	check(authority.error == OK and authority.has("value") and authority.value == bits("9223372036854775808"), "checked full-width authority epoch")
	var admission := session.get_admission_state()
	check(admission.error == ERR_UNAVAILABLE and not admission.ready and admission.simulation == "unqualified", "unqualified native transport fails closed")
	check(admission.schemas_sha256.length() == 64, "actual frozen registry SHA256 exposed")
	var state := SuperposUInt64.to_bytes(1)
	var first := session.spawn_object(schema.schema_id, bits("18446744073709551615"), state)
	var second := session.spawn_object(schema.schema_id, 0, state)
	check(first != 0 and second != 0, "native core spawn")
	check(session.read_object(first).owner == -1, "full-width owner survives")
	var worker := Thread.new()
	check(worker.start(reject_wrong_thread.bind(session, first)) == OK, "worker started")
	check(worker.wait_to_finish() == true, "wrong-thread mutation rejected")
	if session.has_method("configure_udp"):
		var creation_worker := Thread.new()
		check(creation_worker.start(reject_worker_creation.bind(schema)) == OK, "worker factory fixture started")
		check(creation_worker.wait_to_finish() == true, "EGP factory admits engine main owner only")
	check(session.get_state() == "Configured" and session.get_last_error() == OK and tick_value(session) == 0, "wrong-thread calls preserve owner state")
	# Editing author resources cannot mutate the immutable configured core schema.
	field.max_bytes = 4
	check(session.read_object(first).canonical.size() == 8, "configured schema frozen")
	check(session.get_admission_state().schemas_sha256 == admission.schemas_sha256, "frozen registry unaffected by author edits")
	check(session.advance_tick() == OK and tick_value(session) == 1, "native tick")
	var update := SuperposUInt64.to_bytes(2)
	var revision: int = session.read_object(first).revision
	check(session.publish_packed(packed([first, second], [revision, 99], [update, update])) != OK, "stale group rejected")
	check(session.read_object(first).canonical == state, "rejected group leaves first unchanged")
	check(session.publish_packed(packed([first, second], [revision, revision], [update, update])) == OK, "atomic canonical group")
	check(session.read_object(first).canonical == update and session.read_object(second).canonical == update, "group replacement")
	var trailing := packed([first], [session.read_object(first).revision], [state])
	trailing.append(0)
	check(session.publish_packed(trailing) != OK and session.read_object(first).canonical == update, "trailing bytes rejected")
	check(session.destroy_object(first) == OK, "destroy generation")
	var replacement := session.spawn_object(schema.schema_id, 0, state)
	check(replacement != first and session.read_object(first).error != OK, "stale handle cannot alias reused slot")
	var generation := binding_value(session)
	session.close()
	check(session.get_state() == "Closed" and SuperposUInt64.compare(binding_value(session), generation) > 0, "close invalidates binding")
	var closed_authority := session.read_authority_epoch()
	check(closed_authority.error == ERR_UNCONFIGURED and not closed_authority.has("value"), "closed authority has no missing-value sentinel")
	var closed_statistics := session.get_statistics()
	check(closed_statistics.error == ERR_UNCONFIGURED and not closed_statistics.has("objects"), "closed statistics report unavailable")
	field.max_bytes = 8
	check(session.configure([schema], 4, bits("9223372036854775808"), 0, 4096) == OK, "native resource reconfigured")
	var fresh := session.spawn_object(schema.schema_id, 0, state)
	check(fresh != replacement and session.read_object(replacement).error != OK, "closed-world handle cannot alias new world")
	session.close()
	# Preserve the qualified pre-transport bootstrap fixture during the API/SDK
	# two-stage build; the new factory must also qualify clock ownership.
	if session.has_method("configure_udp"):
		var node := SuperposWorld.new()
		node.schemas = [schema]
		node.max_objects = 4
		node.automatic_ticks = true
		check(node.configure() == OK, "automatic owner created before scene attachment")
		var retained := node.get_session()
		check(retained.advance_tick() == ERR_BUSY and node.advance_tick() == ERR_BUSY, "automatic owner rejects both manual stepping routes")
		node.automatic_ticks = false
		check(retained.advance_tick() == OK, "explicit manual mode relinquishes physics clock ownership")
		node.automatic_ticks = true
		node.free()
		var permanent_retirement := OS.get_cmdline_user_args().has("--superpos-permanent-retirement")
		check(retained.get_state() == ("Retired" if permanent_retirement else "Closed"), "node destruction follows the selected native lifecycle contract")
		if permanent_retirement:
			var retired_tick := retained.read_tick()
			var retired_binding := retained.read_binding_generation()
			check(retired_tick.error == ERR_UNCONFIGURED and not retired_tick.has("value"), "retired tick has no value")
			check(retired_binding.error == ERR_UNCONFIGURED and not retired_binding.has("value"), "retired binding has no value")
		var reentrant := SuperposSession.new()
		check(reentrant.configure([schema], 4) == OK, "reentrant callback world configured")
		var callback_close := [OK]
		reentrant.simulation_tick.connect(func(_tick: int): callback_close[0] = reentrant.close_checked(), CONNECT_ONE_SHOT)
		check(reentrant.advance_tick() == OK and callback_close[0] == ERR_BUSY and reentrant.get_state() == "Configured", "tick callback close refuses without partial mutation")
		check(reentrant.close_checked() == OK and reentrant.get_state() == "Closed", "owner closes after callback boundary")
	print("SUPERPOS_EGP_RUNTIME_OK checks=", checks)
	quit(0)
