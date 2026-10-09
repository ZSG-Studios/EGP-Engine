extends SceneTree

# Portable restore of a running scene's Box2D space into a fresh space.
# The scene's World2D space holds rigid and static nodes plus server-built
# joints of every type, a gravity-replacing monitored area, a collision
# exception, a contact-monitoring body and a force-integrated body. A boundary
# capture is restored through the physics server with mapped node ids and
# rebound callbacks; both spaces then step together and must keep identical
# complete-state digests, callback logs and reported contacts on every tick.
# Persistent identities and tamper/truncation/unmapped-id rejection are checked.

const TICKS := 120

var failures := 0


class Recorder:
	extends RefCounted
	var body_events: Array = []
	var area_events: Array = []
	var integrations := 0

	func on_body(status: int, _rid: RID, _instance: int, body_shape: int, area_shape: int) -> void:
		body_events.append([status, body_shape, area_shape])

	func on_area(status: int, _rid: RID, _instance: int, area_shape: int, self_shape: int) -> void:
		area_events.append([status, area_shape, self_shape])

	func integrate(state: PhysicsDirectBodyState2D, scale: int) -> void:
		integrations += 1
		state.apply_central_force(Vector2(30.0 * scale, -60.0 * scale))


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		print("FAIL ", what)


func _initialize() -> void:
	call_deferred("run")


func make_shape(index: int) -> Shape2D:
	match index % 3:
		0:
			var circle := CircleShape2D.new()
			circle.radius = 14.0
			return circle
		1:
			var box := RectangleShape2D.new()
			box.size = Vector2(28, 28)
			return box
		_:
			var capsule := CapsuleShape2D.new()
			capsule.radius = 10.0
			capsule.height = 36.0
			return capsule


func add_rigid(index: int, position: Vector2) -> RigidBody2D:
	var body := RigidBody2D.new()
	var collision := CollisionShape2D.new()
	collision.shape = make_shape(index)
	body.add_child(collision)
	body.position = position
	body.rotation = 0.1 * index
	root.add_child(body)
	return body


func make_joint(server, make: Callable) -> RID:
	var joint: RID = server.joint_create()
	make.call(joint)
	return joint


func run() -> void:
	var server := PhysicsServer2D
	var floor_body := StaticBody2D.new()
	var floor_shape := CollisionShape2D.new()
	var floor_rect := RectangleShape2D.new()
	floor_rect.size = Vector2(1200, 40)
	floor_shape.shape = floor_rect
	floor_body.add_child(floor_shape)
	var ramp := CollisionShape2D.new()
	var segment := SegmentShape2D.new()
	segment.a = Vector2(-300, -120)
	segment.b = Vector2(-120, -20)
	ramp.shape = segment
	floor_body.add_child(ramp)
	floor_body.position = Vector2(0, 400)
	root.add_child(floor_body)
	var bodies: Array[RigidBody2D] = []
	for index in 18:
		bodies.append(add_rigid(index, Vector2((index % 6) * 40 - 100, 300 - (index / 6) * 45)))
	var space: RID = root.get_world_2d().space
	var rid := func(index: int) -> RID: return bodies[index].get_rid()

	# Every joint type through the server.
	var joints: Array[RID] = []
	joints.append(make_joint(server, func(j): server.joint_make_pin(j, Vector2(-80, 300), rid.call(0), rid.call(1))))
	joints.append(make_joint(server, func(j): server.joint_make_pin(j, Vector2(-20, 300), rid.call(2), RID())))
	joints.append(make_joint(server, func(j): server.joint_make_groove(j, Vector2(-60, -150), Vector2(60, -150), Vector2(0, 0), floor_body.get_rid(), rid.call(3))))
	joints.append(make_joint(server, func(j): server.joint_make_damped_spring(j, Vector2(0, 0), Vector2(0, 0), rid.call(4), rid.call(5))))
	server.damped_spring_joint_set_param(joints[3], PhysicsServer2D.DAMPED_SPRING_STIFFNESS, 35.0)
	server.damped_spring_joint_set_param(joints[3], PhysicsServer2D.DAMPED_SPRING_REST_LENGTH, 30.0)
	server.pin_joint_set_flag(joints[0], PhysicsServer2D.PIN_JOINT_FLAG_ANGULAR_LIMIT_ENABLED, true)
	server.pin_joint_set_param(joints[0], PhysicsServer2D.PIN_JOINT_LIMIT_LOWER, -0.4)
	server.pin_joint_set_param(joints[0], PhysicsServer2D.PIN_JOINT_LIMIT_UPPER, 0.4)
	var frame_a := Transform2D(0, Vector2(20, 0))
	var frame_b := Transform2D(0, Vector2(-20, 0))
	var configured := [
		[PhysicsServer2D.JOINT_TYPE_DISTANCE, 6, 7],
		[PhysicsServer2D.JOINT_TYPE_FILTER, 8, 9],
		[PhysicsServer2D.JOINT_TYPE_MOTOR, 10, 11],
		[PhysicsServer2D.JOINT_TYPE_PRISMATIC, 12, 13],
		[PhysicsServer2D.JOINT_TYPE_REVOLUTE, 14, 15],
		[PhysicsServer2D.JOINT_TYPE_WELD, 15, 16],
		[PhysicsServer2D.JOINT_TYPE_WHEEL, 1, 7],
	]
	for entry in configured:
		joints.append(make_joint(server, func(j): server.joint_make_configured(j, entry[0], rid.call(entry[1]), frame_a, rid.call(entry[2]), frame_b, {})))
	# A collision exception and a contact-monitoring body.
	server.body_add_collision_exception(rid.call(3), rid.call(16))
	bodies[9].contact_monitor = true
	bodies[9].max_contacts_reported = 6
	# A force-integrated server body with a method callable and user data.
	var source_recorder := Recorder.new()
	var restored_recorder := Recorder.new()
	var driven: RID = server.body_create()
	server.body_set_mode(driven, PhysicsServer2D.BODY_MODE_RIGID)
	var driven_shape := CircleShape2D.new()
	driven_shape.radius = 12.0
	server.body_add_shape(driven, driven_shape.get_rid())
	server.body_set_state(driven, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0, Vector2(150, 200)))
	server.body_set_space(driven, space)
	server.body_set_force_integration_callback(driven, Callable(source_recorder, "integrate"), 2)
	# A monitored area that replaces gravity where the bodies fall.
	var area: RID = server.area_create()
	var area_shape := RectangleShape2D.new()
	area_shape.size = Vector2(300, 200)
	server.area_add_shape(area, area_shape.get_rid())
	server.area_set_transform(area, Transform2D(0, Vector2(0, 260)))
	server.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY_OVERRIDE_MODE, PhysicsServer2D.AREA_SPACE_OVERRIDE_REPLACE)
	server.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY, 400.0)
	server.area_set_param(area, PhysicsServer2D.AREA_PARAM_GRAVITY_VECTOR, Vector2(0.2, 1.0))
	server.area_set_monitorable(area, true)
	server.area_set_monitor_callback(area, Callable(source_recorder, "on_body"))
	server.area_set_area_monitor_callback(area, Callable(source_recorder, "on_area"))
	server.area_set_space(area, space)
	for tick in 60:
		await physics_frame

	# Capture at the physics phase boundary of the running scene's space.
	check(server.call("space_portable_request_capture", space) == OK, "phase-boundary capture request")
	await physics_frame
	var boundary: Dictionary = server.call("space_portable_take_capture", space)
	check(boundary["error"] == OK and boundary["bytes"].size() > 0, "phase-boundary capture")
	if boundary["error"] != OK:
		print("RESULT: FAIL capture error ", boundary["error"], " refusal ", boundary.get("refusal", -1))
		quit(1)
		return
	var immediate: Dictionary = server.call("space_portable_capture", space)
	check(immediate["error"] == OK and immediate["bytes"] == boundary["bytes"], "an immediate capture at the same boundary is byte-identical")
	var bytes: PackedByteArray = boundary["bytes"]
	var source_events_at_capture := source_recorder.body_events.size()
	var source_integrations_at_capture := source_recorder.integrations

	# Node instance ids map to stand-ins; the recorder maps to its restored twin.
	var object_map := {}
	var standins: Array[Node] = []
	var sources: Array[CollisionObject2D] = [floor_body]
	sources.append_array(bodies)
	for node in sources:
		var standin := Node2D.new()
		root.add_child(standin)
		standins.append(standin)
		object_map[node.get_instance_id()] = standin
	var callable_map := { "area_monitor:%d" % area.get_id(): Callable(restored_recorder, "on_area") }

	# Negatives first; none may print engine errors.
	check(server.call("space_portable_restore", bytes, {})["error"] == ERR_DOES_NOT_EXIST, "an unmapped node instance id is rejected")
	check(server.call("space_portable_restore", bytes, object_map, {}, callable_map)["error"] == ERR_DOES_NOT_EXIST, "an unresolvable force-integration callback is rejected")
	object_map[source_recorder.get_instance_id()] = restored_recorder
	check(server.call("space_portable_restore", bytes, object_map, {}, { "area_monitor:%d" % area.get_id(): 5 })["error"] == ERR_DOES_NOT_EXIST, "a non-callable callback mapping is rejected")
	var tampered := bytes.duplicate()
	tampered[tampered.size() / 2] = tampered[tampered.size() / 2] ^ 0x20
	check(server.call("space_portable_restore", tampered, object_map, {}, callable_map)["error"] == ERR_FILE_CORRUPT, "tampered bytes are rejected")
	check(server.call("space_portable_restore", bytes.slice(0, bytes.size() - 1), object_map, {}, callable_map)["error"] != OK, "truncated bytes are rejected")
	var version := bytes.duplicate()
	version[8] = version[8] + 1
	check(server.call("space_portable_restore", version, object_map, {}, callable_map)["error"] != OK, "another format version is rejected")

	var restored: Dictionary = server.call("space_portable_restore", bytes, object_map, {}, callable_map)
	check(restored["error"] == OK, "restore into a fresh space through the physics server")
	if restored["error"] != OK:
		print("RESULT: FAIL restore error ", restored["error"], " refusal ", restored.get("refusal", -1), " native ", restored.get("native_status", -1))
		quit(1)
		return
	var fresh: RID = restored["space"]
	var mapped: Dictionary = restored["bodies"]
	var mapped_joints: Dictionary = restored["joints"]
	var mapped_areas: Dictionary = restored["areas"]
	check(fresh != space and mapped.size() == bodies.size() + 2, "fresh space with a new RID for every body")
	for index in sources.size():
		var source_rid: RID = sources[index].get_rid()
		check(mapped.has(source_rid.get_id()), "body RID mapped")
		if mapped.has(source_rid.get_id()):
			var new_rid: RID = mapped[source_rid.get_id()]
			check(new_rid != source_rid and server.body_get_space(new_rid) == fresh, "mapped body lives in the fresh space")
			check(server.body_get_object_instance_id(new_rid) == standins[index].get_instance_id(), "node instance id mapped through the registry")
			check(server.body_get_shape_count(new_rid) == server.body_get_shape_count(source_rid), "shape instances restored")
	check(mapped_joints.size() == joints.size(), "every joint restored with a new RID")
	for joint in joints:
		var twin: RID = mapped_joints.get(joint.get_id(), RID())
		check(twin.is_valid() and twin != joint and server.joint_get_type(twin) == server.joint_get_type(joint), "joint type restored")
	check(is_equal_approx(server.pin_joint_get_param(mapped_joints[joints[0].get_id()], PhysicsServer2D.PIN_JOINT_LIMIT_UPPER), 0.4) and server.pin_joint_get_flag(mapped_joints[joints[0].get_id()], PhysicsServer2D.PIN_JOINT_FLAG_ANGULAR_LIMIT_ENABLED), "pin joint limits restored")
	check(server.damped_spring_joint_get_param(mapped_joints[joints[3].get_id()], PhysicsServer2D.DAMPED_SPRING_STIFFNESS) == 35.0, "damped spring stiffness restored")
	var exceptions: Array = server.body_get_collision_exceptions(mapped[rid.call(3).get_id()])
	check(exceptions.size() == 1 and exceptions[0] == mapped[rid.call(16).get_id()], "collision exception remapped to the restored peer")
	check(mapped_areas.size() == 1 and server.area_get_space(mapped_areas[area.get_id()]) == fresh, "extra area restored with a new RID")
	server.space_set_active(fresh, true)
	check(server.call("space_portable_digest", space) == server.call("space_portable_digest", fresh), "digest equal at restore")

	var monitored_source: RID = rid.call(9)
	var monitored_restored: RID = mapped[monitored_source.get_id()]
	var equal := 0
	var first_mismatch := -1
	var contacts_equal := true
	var contacts_seen := 0
	for tick in TICKS:
		await physics_frame
		var a: PackedByteArray = server.call("space_portable_digest", space)
		var b: PackedByteArray = server.call("space_portable_digest", fresh)
		if a.size() == 32 and a == b:
			equal += 1
		elif first_mismatch < 0:
			first_mismatch = tick
		var ca: int = server.body_get_direct_state(monitored_source).get_contact_count()
		var cb: int = server.body_get_direct_state(monitored_restored).get_contact_count()
		contacts_seen = max(contacts_seen, ca)
		if ca != cb:
			contacts_equal = false
	check(equal == TICKS, "source and restored spaces keep identical digests on every tick")
	check(contacts_equal and contacts_seen > 0, "reported contacts are equal on every tick")
	var source_new_events := source_recorder.body_events.slice(source_events_at_capture)
	check(source_new_events == restored_recorder.body_events and restored_recorder.area_events == source_recorder.area_events.slice(0, restored_recorder.area_events.size()), "area monitor callbacks are rebound and report identical events")
	check(source_recorder.integrations - source_integrations_at_capture == restored_recorder.integrations and restored_recorder.integrations == TICKS, "force integration callbacks are rebound and run every tick")
	var transforms_equal := true
	for body in bodies:
		var new_rid: RID = mapped[body.get_rid().get_id()]
		if server.body_get_state(new_rid, PhysicsServer2D.BODY_STATE_TRANSFORM) != server.body_get_state(body.get_rid(), PhysicsServer2D.BODY_STATE_TRANSFORM):
			transforms_equal = false
	check(transforms_equal, "restored body transforms equal the scene bodies")
	print("PORTABLE_RESTORE bytes=", bytes.size(), " ticks=", TICKS, " equal=", equal, " first_mismatch=", first_mismatch, " joints=", mapped_joints.size(), " area_events=", restored_recorder.body_events.size(), " integrations=", restored_recorder.integrations, " max_contacts=", contacts_seen)

	# Persistent identities across captures.
	var identities := {}
	var largest := 0
	for body in bodies:
		var identity: int = server.call("body_portable_identity", body.get_rid())
		identities[body] = identity
		largest = max(largest, identity)
	check(largest > 0, "bodies carry persistent identities")
	var victim: RigidBody2D = bodies.pop_back()
	victim.queue_free()
	await physics_frame
	bodies.append(add_rigid(4, Vector2(200, 100)))
	await physics_frame
	check(server.call("space_portable_request_capture", space) == OK, "second capture request")
	await physics_frame
	var second: Dictionary = server.call("space_portable_take_capture", space)
	check(second["error"] == OK, "second phase-boundary capture")
	var stable := true
	for index in bodies.size() - 1:
		if server.call("body_portable_identity", bodies[index].get_rid()) != identities[bodies[index]]:
			stable = false
	check(stable, "surviving bodies keep their identities across captures")
	var newcomer: int = server.call("body_portable_identity", bodies[bodies.size() - 1].get_rid())
	check(newcomer > largest, "a new body receives a fresh identity")
	var info: Dictionary = server.call("space_portable_capture_info", space)
	check(info.get("captures", 0) >= 3 and info.get("births", 0) >= 1 and info.get("retirements", 0) >= 1 and info.get("survivors", 0) > 0, "capture statistics report persistence")
	print("PORTABLE_IDENTITIES largest=", largest, " newcomer=", newcomer, " info=", info)

	for joint in mapped_joints.values():
		server.free_rid(joint)
	for joint in joints:
		server.free_rid(joint)
	for body_rid in mapped.values():
		server.free_rid(body_rid)
	for area_rid in mapped_areas.values():
		server.free_rid(area_rid)
	server.free_rid(fresh)
	server.free_rid(area)
	server.free_rid(driven)
	for standin in standins:
		standin.queue_free()
	await physics_frame
	print("RESULT: PASS" if failures == 0 else "RESULT: FAIL")
	quit(0 if failures == 0 else 1)
