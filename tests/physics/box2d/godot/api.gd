extends SceneTree
# Full EGPBox2DWorld exposure: every shape type, joint type, force kind, wind, world
# settings, events, readback and queries, plus snapshot replay and twin-world determinism.

const GROUND := 1
const TERRAIN := 2
const SENSOR := 3
const BALL := 10
const CRATE := 11
const KITE := 12
const CHAIN := 20
const JOINT_TYPES := ["distance", "filter", "motor", "prismatic", "revolute", "weld", "wheel"]

var failures := 0


func require(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func build(world) -> void:
	# The test project runs Box2D in metres (physics/box_2d/pixels_per_meter=1).
	require(world.configure(60, 4, 1, Vector2(0, -10)) == OK, "configure failed")
	require(world.queue_create_body(GROUND, 0, {"type": "static"}, [{"type": "box", "half_extents": Vector2(40, 0.5), "center": Vector2(0, -0.5), "friction": 0.8}]) == OK, "ground failed")
	# Open chain: the first and last points are ghosts, leaving three one-sided segments facing up.
	var hills := PackedVector2Array([Vector2(62, 0), Vector2(60, 0), Vector2(50, 2), Vector2(45, 0), Vector2(40, 1), Vector2(38, 1)])
	require(world.queue_create_body(TERRAIN, 0, {"type": "static"}, [{"type": "chain", "points": hills, "friction": 0.6},
			{"type": "segment", "point_a": Vector2(-45, 0), "point_b": Vector2(-40, 3)}]) == OK, "terrain failed")
	require(world.queue_create_body(SENSOR, 0, {"type": "static"}, [{"type": "box", "half_extents": Vector2(2, 2), "center": Vector2(0, 2), "sensor": true, "sensor_events": true}]) == OK, "sensor failed")
	require(world.queue_create_body(BALL, 0, {"type": "dynamic", "position": Vector2(0, 3), "bullet": true, "angular_damping": 0.2},
			[{"type": "circle", "radius": 0.4, "contact_events": true, "hit_events": true, "sensor_events": true},
			{"type": "capsule", "radius": 0.2, "half_height": 0.3, "center": Vector2(0, 0.7), "density": 0.5, "sensor_events": true}]) == OK, "ball failed")
	var tri := PackedVector2Array([Vector2(-0.5, 0), Vector2(0.5, 0), Vector2(0, 0.8)])
	require(world.queue_create_body(CRATE, 0, {"type": "dynamic", "position": Vector2(3, 2), "rotation": 0.4}, [{"type": "polygon", "points": tri, "radius": 0.05, "restitution": 0.3}]) == OK, "crate failed")
	require(world.queue_create_body(KITE, 0, {"type": "dynamic", "position": Vector2(-3, 5), "gravity_scale": 0.2}, [{"type": "box", "half_extents": Vector2(0.6, 0.05)}]) == OK, "kite failed")
	for i in JOINT_TYPES.size():
		var a := CHAIN + i * 2
		var x := -12.0 + i * 3.0
		require(world.queue_create_body(a, 0, {"type": "static", "position": Vector2(x, 8)}, [{"type": "box", "half_extents": Vector2(0.2, 0.2)}]) == OK, "anchor failed")
		require(world.queue_create_body(a + 1, 0, {"type": "dynamic", "position": Vector2(x, 7)}, [{"type": "box", "half_extents": Vector2(0.3, 0.3)}]) == OK, "jointed body failed")
		var fields := {"anchor_a": Vector2(0, -0.5), "anchor_b": Vector2(0, 0.5)}
		match JOINT_TYPES[i]:
			"distance":
				fields.merge({"length": 1.0, "enable_spring": true, "hertz": 4.0, "damping_ratio": 0.5})
			"revolute":
				fields.merge({"enable_limit": true, "lower_angle": -0.5, "upper_angle": 0.5, "enable_motor": true, "motor_speed": 1.0, "max_motor_torque": 50.0})
			"prismatic":
				fields.merge({"rotation_a": PI / 2, "rotation_b": PI / 2, "enable_limit": true, "lower_translation": -0.2, "upper_translation": 0.2})
			"wheel":
				fields.merge({"rotation_a": PI / 2, "rotation_b": PI / 2, "enable_motor": true, "motor_speed": 4.0, "max_motor_torque": 10.0, "enable_spring": true, "hertz": 3.0})
			"motor":
				fields.merge({"max_velocity_force": 20.0, "linear_velocity": Vector2(0.5, 0)})
		require(world.queue_joint(100 + i, 0, JOINT_TYPES[i], a, a + 1, fields) == OK, "joint " + JOINT_TYPES[i] + " failed")
	require(world.apply_queued_commands() == OK, "baseline failed")


func drive(world, tick: int) -> void:
	if tick == 10:
		require(world.queue_apply(BALL, 1, EGPBox2DWorld.APPLY_IMPULSE_AT_POINT, Vector2(1, 0), 0.0, Vector2(0, 3.5)) == OK, "impulse failed")
		require(world.queue_apply(CRATE, 1, EGPBox2DWorld.APPLY_TORQUE, Vector2(), 4.0) == OK, "torque failed")
		require(world.queue_set_shape(BALL, 2, 1, {"friction": 0.1, "density": 0.8}) == OK, "set shape failed")
		require(world.queue_add_shape(CRATE, 2, 4, {"type": "circle", "radius": 0.15, "center": Vector2(0, 0.9)}) == OK, "add shape failed")
		require(world.queue_set_shape(TERRAIN, 1, 0, {"friction": 0.3, "category": 2}) == OK, "set chain failed")
	if tick >= 10 and tick < 40:
		require(world.queue_apply_wind(KITE, 3, 0, Vector2(6, 0), 1.0, 0.5) == OK, "wind failed")
	if tick == 30:
		require(world.queue_set_world(0, {"gravity": Vector2(0, -12), "contact_hertz": 40.0, "contact_damping_ratio": 8.0, "contact_speed": 2.5}) == OK, "set world failed")
		require(world.queue_set_joint(100 + JOINT_TYPES.find("revolute"), 1, {"motor_speed": -1.0}) == OK, "set joint failed")
		require(world.queue_set_body(CRATE, 4, {"gravity_scale": 0.5, "lock_angular": true}) == OK, "set body failed")
	if tick == 40:
		require(world.queue_explode(1, Vector2(2, 0), 5.0, 1.0, 2.0) == OK, "explode failed")
		require(world.queue_destroy_shape(CRATE, 5, 4) == OK, "destroy shape failed")
	if tick == 50:
		require(world.queue_apply(BALL, 6, EGPBox2DWorld.APPLY_FORCE, Vector2(30, 0)) == OK, "force failed")


func _initialize() -> void:
	for set_name in ["world", "body", "shape", "joint"]:
		require(EGPBox2DWorld.get_field_names(set_name).size() > 10, "no " + set_name + " fields")
	var world := EGPBox2DWorld.new()
	var twin := EGPBox2DWorld.new()
	build(world)
	build(twin)
	require(":u3f800000:" in world.get_simulation_fingerprint(), "length units missing from the fingerprint")
	require(world.get_body_count() == 6 + JOINT_TYPES.size() * 2, "body count")
	require(world.get_joint_count() == JOINT_TYPES.size(), "joint count")
	require(world.queue_set_body(BALL, 9, {"no_such_field": 1.0}) != OK, "unknown field accepted")
	require(world.queue_set_world(9, {"contact_hertz": 30.0}) != OK, "partial contact tuning accepted")
	require(world.queue_create_body(99, 9, {}, [{"type": "polygon", "points": PackedVector2Array([Vector2.ZERO, Vector2.ONE])}]) != OK, "two-point polygon accepted")
	require(world.queue_set_shape(BALL, 9, 7, {"friction": 0.5}) == OK, "missing shape not queued")
	require(world.apply_queued_commands() == ERR_INVALID_DATA, "missing shape batch applied")
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
		require(events.moved_transforms.size() == events.moved.size() * 3, "moved transforms")
		if tick == 45:
			snapshot = world.capture_snapshot()
	require(contacts > 0, "no contact events")
	require(sensors > 0, "no sensor events")
	require(moved > 0, "no move events")

	var ball: Dictionary = world.get_body(BALL)
	require(ball.shapes == PackedInt32Array([0, 1]) and ball.mass > 0.0 and ball.bullet, "ball readback")
	require(world.get_body(CRATE).shapes == PackedInt32Array([0]), "destroyed shape still listed")
	require(is_equal_approx(world.get_body(CRATE).gravity_scale, 0.5) and world.get_body(CRATE).lock_angular, "crate readback")
	require(is_equal_approx(world.get_shape(BALL, 1).friction, 0.1), "shape friction readback")
	require(is_equal_approx(world.get_shape(TERRAIN, 0).friction, 0.3) and world.get_shape(TERRAIN, 0).segment_count == 3, "chain readback")
	require(world.get_world().gravity.is_equal_approx(Vector2(0, -12)), "world gravity readback")
	for i in JOINT_TYPES.size():
		require(world.get_joint(100 + i).get("type", "") == JOINT_TYPES[i], "joint type readback " + JOINT_TYPES[i])
	require(world.get_joint(100 + JOINT_TYPES.find("revolute")).has("angle"), "revolute angle missing")

	var rays: Dictionary = world.cast_rays(PackedVector2Array([Vector2(10, 5), Vector2(50, 5)]), PackedVector2Array([Vector2(0, -10), Vector2(0, -10)]))
	require(rays.hit[0] == 1 and rays.entity[0] == GROUND and rays.shape[0] == 0, "ray ground hit")
	require(rays.hit[1] == 1 and rays.entity[1] == TERRAIN and rays.shape[1] == 0, "ray chain hit")
	require(world.overlap_aabb(Vector2(-1, 0), Vector2(1, 4)).size() >= 2, "overlap aabb")
	require(world.overlap_shape({"type": "circle", "radius": 1.0}, Vector2(10, 0.2)).has(GROUND), "overlap shape")
	var sweep: Dictionary = world.cast_shape({"type": "box", "half_extents": Vector2(0.3, 0.3)}, Vector2(10, 5), 0.0, Vector2(0, -10))
	require(sweep.hit and sweep.entity == GROUND, "cast shape")
	var mover: Dictionary = world.move_capsule(Vector2(15, 1), Vector2(0, -0.3), Vector2(0, 0.3), 0.3, Vector2(0, -3))
	require(mover.position.y > 0.5 and mover.position.y < 1.0, "mover stops on ground at %s" % mover.position)

	require(not snapshot.is_empty(), "capture failed")
	require(world.restore_snapshot(snapshot) == OK, "restore failed")
	require(world.get_tick() == 45, "restore tick")
	require(world.get_body(BALL).shapes == PackedInt32Array([0, 1]), "restored shape indices")
	require(world.get_world().gravity.is_equal_approx(Vector2(0, -12)), "restored gravity")
	for tick in range(46, 121):
		drive(world, tick)
		require(world.step_tick(tick) == OK, "replay step failed at %d" % tick)
		require(world.get_state_hash() == hashes[tick - 1], "replay diverged at %d" % tick)
	var restored_rays: Dictionary = world.cast_rays(PackedVector2Array([Vector2(50, 5)]), PackedVector2Array([Vector2(0, -10)]))
	require(restored_rays.entity[0] == TERRAIN, "identities lost after restore")
	if failures == 0:
		print("BOX2D_API_PASS bodies=", world.get_body_count(), " joints=", world.get_joint_count(), " contacts=", contacts, " sensors=", sensors, " hash=", world.get_state_hash())
	quit(1 if failures else 0)
