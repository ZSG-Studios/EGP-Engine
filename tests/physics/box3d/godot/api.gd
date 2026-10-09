extends SceneTree
# Full EGPBox3DWorld exposure: every shape type, joint type, force kind, world setting,
# events, readback and queries, plus snapshot replay and twin-world determinism.

const GROUND := 1
const FIELD := 2
const RAMP := 3
const COMPOUND := 10
const HULL := 11
const SENSOR := 12
const BAKED := 13
const CHAIN := 20
const JOINT_TYPES := ["distance", "spherical", "prismatic", "motor", "revolute", "weld", "wheel", "filter", "parallel", "generic"]

var failures := 0


func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func build(world) -> void:
	require(world.configure(60, 4, 1) == OK, "configure failed")
	require(world.queue_create_body(GROUND, 0, {"type": "static"}, [{"type": "box", "half_extents": Vector3(30, 0.5, 30), "center": Vector3(0, -0.5, 0), "friction": 0.8}]) == OK, "ground failed")
	var heights := PackedFloat32Array()
	for z in 5:
		for x in 5:
			heights.append(0.2 * sin(x) * cos(z))
	require(world.queue_create_body(FIELD, 0, {"type": "static", "position": Vector3(40, 0, 0)}, [{"type": "height_field", "heights": heights, "count_x": 5, "count_z": 5, "scale": Vector3(2, 1, 2)}]) == OK, "height field failed")
	var ramp := PackedVector3Array([Vector3(-3, 0, -3), Vector3(3, 0, -3), Vector3(3, 2, 3), Vector3(-3, 2, 3)])
	# Per-triangle materials: the second triangle uses table entry 1.
	require(world.queue_create_body(RAMP, 0, {"type": "static", "position": Vector3(-20, 0, 0)}, [{"type": "mesh", "points": ramp, "indices": PackedInt32Array([0, 2, 1, 0, 3, 2]),
			"friction": 0.6, "materials": [{"friction": 0.05, "restitution": 0.2}], "material_indices": PackedByteArray([0, 1])}]) == OK, "mesh failed")
	# Baked compound: many children populate the broad-phase as one static shape.
	var hull_points := PackedVector3Array([Vector3(-0.4, 0, -0.4), Vector3(0.4, 0, -0.4), Vector3(0, 0, 0.5), Vector3(0, 0.7, 0)])
	require(world.queue_create_body(BAKED, 0, {"type": "static", "position": Vector3(20, 0, -10)}, [{"type": "compound", "children": [
			{"type": "box", "half_extents": Vector3(2, 0.25, 2), "center": Vector3(0, 0.25, 0), "friction": 0.9},
			{"type": "sphere", "radius": 0.5, "center": Vector3(1, 1, 0)},
			{"type": "capsule", "radius": 0.2, "half_height": 0.6, "center": Vector3(-1, 1, 0), "rotation": Quaternion(Vector3.FORWARD, 1.2)},
			{"type": "hull", "points": hull_points, "center": Vector3(0, 0.5, 1)},
			{"type": "mesh", "points": ramp, "indices": PackedInt32Array([0, 2, 1]), "scale": Vector3(0.3, 0.3, 0.3), "center": Vector3(0, 0.6, -1)}]}]) == OK, "compound failed")
	require(world.queue_create_body(COMPOUND, 0, {"type": "dynamic", "position": Vector3(0, 3, 0), "angular_damping": 0.5, "bullet": true},
			[{"type": "capsule", "radius": 0.4, "half_height": 0.5, "contact_events": true, "hit_events": true, "sensor_events": true},
			{"type": "sphere", "radius": 0.3, "center": Vector3(0, 1.2, 0), "density": 0.5, "sensor_events": true}]) == OK, "compound failed")
	var hull := PackedVector3Array([Vector3(-0.5, 0, -0.5), Vector3(0.5, 0, -0.5), Vector3(0, 0, 0.6), Vector3(0, 0.9, 0)])
	require(world.queue_create_body(HULL, 0, {"type": "dynamic", "position": Vector3(3, 2, 0), "rotation": Quaternion(Vector3.UP, 0.4)}, [{"type": "hull", "points": hull, "restitution": 0.3}]) == OK, "hull failed")
	require(world.queue_create_body(SENSOR, 0, {"type": "static"}, [{"type": "box", "half_extents": Vector3(2, 2, 2), "center": Vector3(0, 2, 0), "sensor": true, "sensor_events": true}]) == OK, "sensor failed")
	# One pair of bodies per joint type.
	for i in JOINT_TYPES.size():
		var a := CHAIN + i * 2
		var x := -10.0 + i * 2.5
		require(world.queue_create_body(a, 0, {"type": "kinematic" if i % 2 == 0 else "static", "position": Vector3(x, 6, 10)}, [{"type": "box", "half_extents": Vector3(0.2, 0.2, 0.2)}]) == OK, "anchor body failed")
		require(world.queue_create_body(a + 1, 0, {"type": "dynamic", "position": Vector3(x, 5, 10)}, [{"type": "box", "half_extents": Vector3(0.3, 0.3, 0.3), "sensor_events": true}]) == OK, "jointed body failed")
		var fields := {"anchor_a": Vector3(0, -0.5, 0), "anchor_b": Vector3(0, 0.5, 0)}
		match JOINT_TYPES[i]:
			"distance":
				fields.merge({"length": 1.0, "enable_spring": true, "hertz": 4.0, "damping_ratio": 0.5})
			"revolute":
				fields.merge({"enable_limit": true, "lower_angle": -0.5, "upper_angle": 0.5, "enable_motor": true, "motor_speed": 1.0, "max_motor_torque": 50.0})
			"prismatic":
				fields.merge({"enable_limit": true, "lower_translation": -0.2, "upper_translation": 0.2})
			"wheel":
				fields.merge({"enable_spin_motor": true, "spin_speed": 4.0, "max_spin_torque": 10.0})
			"spherical":
				fields.merge({"enable_cone_limit": true, "cone_angle": 0.6})
			"generic":
				fields.merge({"enable_linear_limit": Vector3(1, 1, 1), "linear_lower_limit": Vector3(-0.1, -0.1, -0.1), "linear_upper_limit": Vector3(0.1, 0.1, 0.1), "enable_angular_motor": Vector3(0, 1, 0), "angular_motor_target_velocity": Vector3(0, 2, 0), "angular_motor_force_limit": Vector3(0, 20, 0)})
		require(world.queue_joint(100 + i, 0, JOINT_TYPES[i], a, a + 1, fields) == OK, "joint " + JOINT_TYPES[i] + " failed")
	require(world.apply_queued_commands() == OK, "baseline failed")


func drive(world, tick: int) -> void:
	if tick == 10:
		require(world.queue_apply(COMPOUND, 1, EGPBox3DWorld.APPLY_IMPULSE_AT_POINT, Vector3(2, 0, 0), Vector3(0, 3.5, 0)) == OK, "impulse failed")
		require(world.queue_apply(HULL, 1, EGPBox3DWorld.APPLY_TORQUE, Vector3(0, 5, 0)) == OK, "torque failed")
		require(world.queue_set_shape(COMPOUND, 2, 1, {"friction": 0.1, "density": 0.8}) == OK, "set shape failed")
		require(world.queue_add_shape(HULL, 2, 4, {"type": "box", "half_extents": Vector3(0.2, 0.2, 0.2), "center": Vector3(0, 1.0, 0), "rotation": Quaternion(Vector3.FORWARD, 0.3)}) == OK, "add shape failed")
	if tick == 30:
		require(world.queue_set_world(0, {"gravity": Vector3(0, -12, 0), "contact_hertz": 40.0, "speculative": false}) == OK, "set world failed")
		require(world.queue_set_shape(RAMP, 2, 0, {"friction": 0.9}, 1) == OK, "set mesh material failed")
		require(world.queue_set_joint(100 + JOINT_TYPES.find("revolute"), 1, {"motor_speed": -1.0}) == OK, "set joint failed")
		require(world.queue_set_body(HULL, 3, {"gravity_scale": 0.5, "lock_angular": Vector3(1, 0, 1)}) == OK, "set body failed")
	if tick == 40:
		require(world.queue_explode(1, Vector3(2, 0, 0), 5.0, 1.0, 2.0) == OK, "explode failed")
		require(world.queue_destroy_shape(HULL, 4, 4) == OK, "destroy shape failed")
	if tick == 50:
		require(world.queue_apply(COMPOUND, 5, EGPBox3DWorld.APPLY_FORCE, Vector3(0, 0, 30)) == OK, "force failed")


func _initialize() -> void:
	for set_name in ["world", "body", "shape", "joint"]:
		require(EGPBox3DWorld.get_field_names(set_name).size() > 10, "no " + set_name + " fields")
	require(EGPBox3DWorld.get_field_names("joint").has("angular_motor_target_velocity"), "generic joint fields missing")
	var world := EGPBox3DWorld.new()
	var twin := EGPBox3DWorld.new()
	build(world)
	build(twin)
	require(world.get_body_count() == 7 + JOINT_TYPES.size() * 2, "body count")
	require(world.get_joint_count() == JOINT_TYPES.size(), "joint count")
	# Rejections.
	require(world.queue_set_body(COMPOUND, 9, {"no_such_field": 1.0}) != OK, "unknown field accepted")
	require(world.queue_set_body(COMPOUND, 9, {"linear_damping": -1.0}) != OK, "negative damping accepted")
	require(world.queue_set_shape(COMPOUND, 9, 0, {"friction": -0.5}) != OK, "negative friction accepted")
	require(world.queue_joint(900, 9, "revolute", COMPOUND, HULL, {"enable_limit": true, "lower_angle": 1.0, "upper_angle": -1.0}) != OK, "inverted limits accepted")
	require(world.queue_joint(901, 9, "distance", COMPOUND, HULL, {"length": 0.0}) != OK, "zero length accepted")
	require(world.queue_create_body(98, 9, {"type": "static"}, [{"type": "compound", "sensor": true, "children": [{"type": "sphere"}]}]) != OK, "compound sensor accepted")
	require(world.queue_create_body(99, 9, {}, [{"type": "box", "half_extents": Vector3(-1, 1, 1)}]) != OK, "negative box accepted")
	require(world.queue_set_shape(COMPOUND, 9, 7, {"friction": 0.5}) == OK, "missing shape not queued")
	require(world.apply_queued_commands() == ERR_INVALID_DATA, "missing shape batch applied")
	world.clear_pending_commands()
	# Batch-level rules: static-only shapes, compound bodies keep their type, limits merge
	# with the joint's current values, material indices stay inside the table.
	var batch_rejections := [
		func(): return world.queue_add_shape(COMPOUND, 9, 5, {"type": "height_field", "heights": PackedFloat32Array([0, 0, 0, 0]), "count_x": 2, "count_z": 2}),
		func(): return world.queue_set_body(BAKED, 9, {"type": "dynamic"}),
		func(): return world.queue_set_joint(100 + JOINT_TYPES.find("revolute"), 9, {"lower_angle": 0.9}),
		func(): return world.queue_set_shape(RAMP, 9, 0, {"friction": 0.3}, 2),
		func(): return world.queue_set_shape(BAKED, 9, 0, {"friction": 0.3}),
	]
	for queue_rejected in batch_rejections:
		require(queue_rejected.call() == OK, "batch rule command not queued")
		require(world.apply_queued_commands() == ERR_INVALID_DATA, "batch rule not enforced")
		world.clear_pending_commands()

	var hashes: Array[String] = []
	var snapshot := PackedByteArray()
	var contacts := 0
	var sensors := 0
	var moved := 0
	for tick in range(1, 121):
		drive(world, tick)
		drive(twin, tick)
		require(world.step_tick(tick) == OK, "step failed at %d" % tick)
		require(twin.step_tick(tick) == OK, "twin step failed at %d" % tick)
		require(world.get_state_hash() == twin.get_state_hash(), "twin diverged at %d" % tick)
		hashes.append(world.get_state_hash())
		var events: Dictionary = world.get_events()
		contacts += events.contact_begin.size() / 4
		sensors += events.sensor_begin.size() / 4
		moved += events.moved.size()
		require(events.moved_transforms.size() == events.moved.size() * 7, "moved transforms")
		if tick == 45:
			snapshot = world.capture_snapshot()
	require(contacts > 0, "no contact events")
	require(sensors > 0, "no sensor events")
	require(moved > 0, "no move events")

	var body: Dictionary = world.get_body(COMPOUND)
	require(body.shapes == PackedInt32Array([0, 1]), "compound shape indices")
	require(body.mass > 0.0 and body.bullet, "compound readback")
	require(world.get_body(HULL).shapes == PackedInt32Array([0]), "destroyed shape still listed")
	require(is_equal_approx(world.get_body(HULL).gravity_scale, 0.5), "gravity scale readback")
	require(is_equal_approx(world.get_shape(COMPOUND, 1).friction, 0.1), "shape friction readback")
	var settings: Dictionary = world.get_world()
	require(settings.gravity.is_equal_approx(Vector3(0, -12, 0)), "world gravity readback")
	require(is_equal_approx(settings.contact_hertz, 40.0) and settings.contact_damping_ratio > 0.0 and not settings.speculative, "partial contact tuning readback")
	var ramp_shape: Dictionary = world.get_shape(RAMP, 0)
	require(ramp_shape.type == "mesh" and ramp_shape.material_count == 2 and is_equal_approx(ramp_shape.materials[0].friction, 0.9), "mesh material table readback")
	require(world.get_shape(BAKED, 0).type == "compound", "compound readback")
	require(world.get_joint(100 + JOINT_TYPES.find("wheel")).has("angular_separation"), "wheel angular separation missing")
	for i in JOINT_TYPES.size():
		var joint: Dictionary = world.get_joint(100 + i)
		require(joint.get("type", "") == JOINT_TYPES[i], "joint type readback " + JOINT_TYPES[i])
	require(world.get_joint(100 + JOINT_TYPES.find("revolute")).has("angle"), "revolute angle missing")

	var rays: Dictionary = world.cast_rays(PackedVector3Array([Vector3(5, 5, -5), Vector3(-20, 5, 2)]), PackedVector3Array([Vector3(0, -10, 0), Vector3(0, -10, 0)]))
	require(rays.hit[0] == 1 and rays.entity[0] == GROUND and rays.shape[0] == 0, "ray ground hit")
	require(rays.hit[1] == 1 and rays.entity[1] == RAMP, "ray mesh hit")
	var inside: PackedInt64Array = world.overlap_aabb(Vector3(-1, 0, -1), Vector3(1, 4, 1))
	require(inside.size() >= 2 and inside.size() % 2 == 0, "overlap aabb")
	var touching: PackedInt64Array = world.overlap_shape({"type": "sphere", "radius": 1.0}, Vector3(0, 0.2, 0))
	require(touching.has(GROUND), "overlap shape")
	var on_compound: Dictionary = world.cast_rays(PackedVector3Array([Vector3(20, 5, -10)]), PackedVector3Array([Vector3(0, -10, 0)]))
	require(on_compound.hit[0] == 1 and on_compound.entity[0] == BAKED, "ray hits baked compound")
	var sweep: Dictionary = world.cast_shape({"type": "box", "half_extents": Vector3(0.3, 0.3, 0.3)}, Vector3(5, 5, -5), Quaternion(), Vector3(0, -10, 0))
	require(sweep.hit and sweep.entity == GROUND, "cast shape")

	# Snapshot replay restores bodies, shape indices, joints and world settings.
	require(not snapshot.is_empty(), "capture failed")
	require(world.restore_snapshot(snapshot) == OK, "restore failed")
	require(world.get_tick() == 45, "restore tick")
	require(world.get_body(COMPOUND).shapes == PackedInt32Array([0, 1]), "restored shape indices")
	require(world.get_world().gravity.is_equal_approx(Vector3(0, -12, 0)), "restored gravity")
	for tick in range(46, 121):
		drive(world, tick)
		require(world.step_tick(tick) == OK, "replay step failed at %d" % tick)
		require(world.get_state_hash() == hashes[tick - 1], "replay diverged at %d" % tick)
	if failures == 0:
		print("BOX3D_API_PASS bodies=", world.get_body_count(), " joints=", world.get_joint_count(), " contacts=", contacts, " sensors=", sensors, " hash=", world.get_state_hash())
	quit(1 if failures else 0)
