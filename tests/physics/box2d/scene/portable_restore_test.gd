extends SceneTree

# Portable restore of a running scene's Box2D space into a fresh space.
# A boundary capture of the scene's World2D space is restored through the
# physics server with mapped node ids; both spaces then step together and must
# keep identical complete-state digests on every tick. Persistent lifetime
# identities, tamper/truncation rejection and unmapped-id rejection are checked.

const TICKS := 120

var failures := 0


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
	for tick in 60:
		await physics_frame

	# Capture at the physics phase boundary of the running scene's space.
	check(server.call("space_portable_request_capture", space) == OK, "phase-boundary capture request")
	await physics_frame
	var boundary: Dictionary = server.call("space_portable_take_capture", space)
	check(boundary["error"] == OK and boundary["bytes"].size() > 0, "phase-boundary capture")
	if boundary["error"] != OK:
		print("RESULT: FAIL capture error ", boundary["error"], " refusal ", boundary.get("refusal", -1), " immediate ", server.call("space_portable_capture", space))
		quit(1)
		return
	var immediate: Dictionary = server.call("space_portable_capture", space)
	check(immediate["error"] == OK and immediate["bytes"] == boundary["bytes"], "an immediate capture at the same boundary is byte-identical")
	var bytes: PackedByteArray = boundary["bytes"]

	# Node instance ids are application objects: map each to a stand-in node.
	var object_map := {}
	var standins: Array[Node] = []
	var sources: Array[CollisionObject2D] = [floor_body]
	sources.append_array(bodies)
	for node in sources:
		var standin := Node2D.new()
		root.add_child(standin)
		standins.append(standin)
		object_map[node.get_instance_id()] = standin

	# Negatives first; none may print engine errors.
	var unmapped: Dictionary = server.call("space_portable_restore", bytes, {})
	check(unmapped["error"] == ERR_DOES_NOT_EXIST, "an unmapped node instance id is rejected")
	var tampered := bytes.duplicate()
	tampered[tampered.size() / 2] = tampered[tampered.size() / 2] ^ 0x20
	check(server.call("space_portable_restore", tampered, object_map)["error"] == ERR_FILE_CORRUPT, "tampered bytes are rejected")
	check(server.call("space_portable_restore", bytes.slice(0, bytes.size() - 1), object_map)["error"] != OK, "truncated bytes are rejected")
	var version := bytes.duplicate()
	version[8] = version[8] + 1
	check(server.call("space_portable_restore", version, object_map)["error"] != OK, "another format version is rejected")

	var restored: Dictionary = server.call("space_portable_restore", bytes, object_map)
	check(restored["error"] == OK, "restore into a fresh space through the physics server")
	if restored["error"] != OK:
		print("RESULT: FAIL restore error ", restored["error"])
		quit(1)
		return
	var fresh: RID = restored["space"]
	var mapped: Dictionary = restored["bodies"]
	check(fresh != space and mapped.size() == bodies.size() + 1, "fresh space with a new RID for every body")
	for index in sources.size():
		var source_rid: RID = sources[index].get_rid()
		check(mapped.has(source_rid.get_id()), "body RID mapped")
		if mapped.has(source_rid.get_id()):
			var rid: RID = mapped[source_rid.get_id()]
			check(rid != source_rid and server.body_get_space(rid) == fresh, "mapped body lives in the fresh space")
			check(server.body_get_object_instance_id(rid) == standins[index].get_instance_id(), "node instance id mapped through the registry")
			check(server.body_get_shape_count(rid) == server.body_get_shape_count(source_rid), "shape instances restored")
	server.space_set_active(fresh, true)
	check(server.call("space_portable_digest", space) == server.call("space_portable_digest", fresh), "digest equal at restore")

	var equal := 0
	var first_mismatch := -1
	for tick in TICKS:
		await physics_frame
		var a: PackedByteArray = server.call("space_portable_digest", space)
		var b: PackedByteArray = server.call("space_portable_digest", fresh)
		if a.size() == 32 and a == b:
			equal += 1
		elif first_mismatch < 0:
			first_mismatch = tick
	check(equal == TICKS, "source and restored spaces keep identical digests on every tick")
	var transforms_equal := true
	for body in bodies:
		var rid: RID = mapped[body.get_rid().get_id()]
		if server.body_get_state(rid, PhysicsServer2D.BODY_STATE_TRANSFORM) != server.body_get_state(body.get_rid(), PhysicsServer2D.BODY_STATE_TRANSFORM):
			transforms_equal = false
	check(transforms_equal, "restored body transforms equal the scene bodies")
	print("PORTABLE_RESTORE bytes=", bytes.size(), " ticks=", TICKS, " equal=", equal, " first_mismatch=", first_mismatch)

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

	for rid in mapped.values():
		server.free_rid(rid)
	server.free_rid(fresh)
	for standin in standins:
		standin.queue_free()
	await physics_frame
	print("RESULT: PASS" if failures == 0 else "RESULT: FAIL")
	quit(0 if failures == 0 else 1)
