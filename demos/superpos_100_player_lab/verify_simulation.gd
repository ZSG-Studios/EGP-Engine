extends SceneTree
const SIM := preload("res://simulation_world.gd")
var failures: Array[String] = []
func _initialize() -> void:
	_run.call_deferred()
func _run() -> void:
	var world := EGPBox3DWorld.new()
	world.configure(60,4,1)
	world.queue_create_box(1,0,Vector3(0,-0.5,0),Vector3(35,0.5,35),0)
	world.queue_create_capsule(1000,0,Vector3(0,1.2,20),0.35,0.55,2,0.85)
	world.apply_queued_commands()
	var sim := SIM.new()
	if not sim.setup(world,0):
		failures.append("native setup")
		quit(1)
		return
	for tick in range(360):
		await physics_frame
		if tick==120:
			var p: Vector3=world.get_body_state(1000).position
			world.queue_destroy_body(1000,0)
			world.queue_create_capsule(1000,1,p-Vector3.UP*0.5,0.35,0.05,2,0.85)
			world.queue_body_state(1000,2,p-Vector3.UP*0.5,Quaternion.IDENTITY,Vector3.ZERO,Vector3.ZERO)
		sim.step(world,[],float(tick)/60)
		if world.step_tick(world.get_tick()+1)!=OK:
			failures.append("world step / collider replacement")
		# Joint travel and cloth deformation are sampled per tick, as the lab does.
		sim.observe()
	var report: Dictionary=sim.telemetry(world)
	var h: Array=report.float_heights
	if not (h[0]>1.8 and h[3]>1.8 and h[2]<0.65 and h[5]<0.65 and h[1]>1.3 and h[1]<1.8):
		failures.append("buoyancy equilibrium")
	if not report.cloth_finite or report.pin_error>0.001 or report.cloth_deformation<0.1 or report.joint_travel<1:
		failures.append("native cloth/joints")
	var capsule: Dictionary=world.get_body_state(1000)
	if absf(capsule.position.y-0.4)>0.02:
		failures.append("crawl collider does not settle at correct height")
	var file := FileAccess.open(ProjectSettings.globalize_path("res://../../.build/diagnostics/superpos-100/simulation-receipt.json"),FileAccess.WRITE)
	file.store_string(JSON.stringify({"passed":failures.is_empty(),"failures":failures,"native":report,"crawl_center":capsule.position.y},"\t"))
	sim.close()
	print("SIMULATION ","PASS" if failures.is_empty() else "FAIL"," ",failures)
	quit(0 if failures.is_empty() else 1)
