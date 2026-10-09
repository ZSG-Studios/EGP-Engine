extends SceneTree
# Deterministic tick cost at scale, headless: the same gameplay step every peer runs,
# fed bot-like inputs (steering to goals, stances, jumps). Prints per-tick milliseconds
# split into gameplay script, exhibit forces and the native Box3D step.
# godot --headless --path . -s res://bench_step.gd -- [players] [workers] [ticks]
const GAMEPLAY := preload("res://gameplay.gd")


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var players := int(args[0]) if args.size() > 0 else 256
	var workers := int(args[1]) if args.size() > 1 else 1
	var ticks := int(args[2]) if args.size() > 2 else 600
	GAMEPLAY.configure_players(players)
	var game = GAMEPLAY.new()
	if not game.create(players, workers):
		push_error("create failed")
		quit(1)
		return
	var inputs: Array = []
	inputs.resize(players)
	var samples: Array[float] = []
	for tick in range(1, ticks + 1):
		for id in range(players):
			var a: Dictionary = game.actors[id]
			var target: Vector3 = GAMEPLAY.goal(int(a.goal))
			var p: Vector3 = a.position
			var direction := Vector3(target.x - p.x, 0, target.z - p.z).normalized()
			var flags := 0
			if (tick + id * 7) % 240 == 0:
				flags |= 1
			var phase := int(tick / 60.0 + id * 1.7) % 48
			if phase >= 28 and phase < 32:
				flags |= 8
			elif phase >= 32 and phase < 36:
				flags |= 16
			if id % 3 == 0:
				flags |= 4
			inputs[id] = GAMEPLAY.encode_input(direction, flags, atan2(direction.x, direction.z))
		GAMEPLAY.profile = [0, 0, 0, 0]
		var started := Time.get_ticks_usec()
		game.step(tick, inputs)
		samples.append(float(Time.get_ticks_usec() - started) / 1000.0)
		if tick == ticks:
			print("LAST profile us script=%d exhibits=%d native=%d" % [GAMEPLAY.profile[0], GAMEPLAY.profile[1], GAMEPLAY.profile[2]])
	# Steady state: skip the first second.
	var steady := samples.slice(60)
	steady.sort()
	var total := 0.0
	for value in steady:
		total += value
	print("BENCH players=%d workers=%d ticks=%d mean=%.2fms p50=%.2fms p95=%.2fms max=%.2fms bodies=%d" % [players, workers, ticks, total / steady.size(), steady[steady.size() / 2], steady[int(steady.size() * 0.95)], steady[-1], game.world.get_body_count()])
	_split(game, inputs, players, ticks)
	game.close()
	quit(0)


# Average split over 120 more ticks.
func _split(game, inputs: Array, players: int, from: int) -> void:
	GAMEPLAY.profile = [0, 0, 0, 0]
	for tick in range(from + 1, from + 121):
		game.step(tick, inputs)
	var n := float(GAMEPLAY.profile[3])
	print("SPLIT script=%.2fms exhibits=%.2fms native=%.2fms (players=%d)" % [GAMEPLAY.profile[0] / n / 1000.0, GAMEPLAY.profile[1] / n / 1000.0, GAMEPLAY.profile[2] / n / 1000.0, players])
