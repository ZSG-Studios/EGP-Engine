extends SceneTree
# Deterministic world cost at entity scale, headless: N dynamic boxes resting on a
# ground plane (they settle and sleep), periodic explosions that wake one region, and
# the join keyframe cost (snapshot capture, zstd, restore) at that size.
# godot --headless --path . -s res://bench_scale.gd -- [bodies] [workers] [ticks] [dump_dir]
# With dump_dir, writes the settled level baseline (tick SETTLE, before any explosion,
# reproducible by every peer from the level recipe) and the final snapshot.
const TICK_RATE := 60
const SETTLE := 240


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var bodies := int(args[0]) if args.size() > 0 else 100000
	var workers := int(args[1]) if args.size() > 1 else 1
	var ticks := int(args[2]) if args.size() > 2 else 600
	var dump := args[3] if args.size() > 3 else ""
	var world := EGPBox3DWorld.new()
	if world.configure(TICK_RATE, 4, workers) != OK:
		push_error("configure failed")
		quit(1)
		return
	# A square grid with 1.5 m spacing; the ground covers it.
	var side := ceili(sqrt(float(bodies)))
	var half := side * 0.75 + 2.0
	var ok := world.queue_create_box(1, 0, Vector3(0, -0.5, 0), Vector3(half, 0.5, half), 0) == OK
	for i in range(bodies):
		var p := Vector3((i % side) * 1.5 - side * 0.75, 0.3, (i / side) * 1.5 - side * 0.75)
		ok = ok and world.queue_create_box(10 + i, 0, p, Vector3.ONE * 0.25, 2, 1.0) == OK
		# One batch holds at most 65,536 commands.
		if i % 50000 == 49999:
			ok = ok and world.apply_queued_commands() == OK
	var started := Time.get_ticks_usec()
	ok = ok and world.apply_queued_commands() == OK
	if not ok:
		push_error("create failed")
		quit(1)
		return
	print("CREATE bodies=%d %.1fms" % [world.get_body_count(), (Time.get_ticks_usec() - started) / 1000.0])
	var samples: Array[float] = []
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for tick in range(1, ticks + 1):
		# Every second, one explosion wakes a region of about 50 bodies.
		if tick == SETTLE + 1 and not dump.is_empty():
			_write(dump.path_join("base.bin"), world.capture_snapshot())
		if tick > SETTLE and tick % 60 == 0:
			var at := Vector3(rng.randf_range(-side * 0.7, side * 0.7), 0.0, rng.randf_range(-side * 0.7, side * 0.7))
			world.queue_explode(tick, at, 6.0, 2.0, 4.0)
			world.apply_queued_commands()
		started = Time.get_ticks_usec()
		if world.step_tick(tick) != OK:
			push_error("step failed at %d" % tick)
			quit(1)
			return
		samples.append(float(Time.get_ticks_usec() - started) / 1000.0)
	_report("STEP", samples.slice(SETTLE))
	var capture: Array[float] = []
	var snapshot := PackedByteArray()
	for _i in range(5):
		started = Time.get_ticks_usec()
		snapshot = world.capture_snapshot()
		capture.append(float(Time.get_ticks_usec() - started) / 1000.0)
	if not dump.is_empty():
		_write(dump.path_join("final.bin"), snapshot)
	started = Time.get_ticks_usec()
	var packed := snapshot.compress(FileAccess.COMPRESSION_ZSTD)
	var zstd_ms := float(Time.get_ticks_usec() - started) / 1000.0
	var restored := EGPBox3DWorld.new()
	restored.configure(TICK_RATE, 4, workers)
	started = Time.get_ticks_usec()
	var unpacked := packed.decompress(snapshot.size(), FileAccess.COMPRESSION_ZSTD)
	var restore_error := restored.restore_snapshot(unpacked)
	var restore_ok := restore_error == OK
	if not restore_ok:
		print("RESTORE error=%d unpacked=%d equal=%s" % [restore_error, unpacked.size(), unpacked == snapshot])
	var restore_ms := float(Time.get_ticks_usec() - started) / 1000.0
	capture.sort()
	print("KEYFRAME snapshot=%d B zstd=%d B capture_p50=%.1fms zstd=%.1fms restore=%.1fms ok=%s hash_equal=%s" % [snapshot.size(), packed.size(), capture[capture.size() / 2], zstd_ms, restore_ms, restore_ok, restored.get_state_hash() == world.get_state_hash()])
	quit(0)


func _report(label: String, values: Array[float]) -> void:
	values.sort()
	var total := 0.0
	for value in values:
		total += value
	print("%s n=%d mean=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms max=%.2fms" % [label, values.size(), total / values.size(), values[values.size() / 2], values[int(values.size() * 0.95)], values[int(values.size() * 0.99)], values[-1]])


func _write(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(bytes)
	file.close()
