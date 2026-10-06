extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
const Bridge = preload("res://addons/egp_net/egp_net_box3d.gd")
var server: Node
var client: Node
var world: RefCounted
var bridge: RefCounted
var entity := 0
var started := Time.get_ticks_msec()
var finished := false

func _ready() -> void:
	world = ClassDB.instantiate("EGPBox3DWorld")
	if world == null or world.configure(60, 4, 1, Vector3(0, -9.8, 0)) != OK:
		finish(false, "native world configuration")
		return
	server = Net.new()
	client = Net.new()
	var options := {"allow_insecure_loopback": true, "tick_rate": 60, "simulation_fingerprint": world.get_simulation_fingerprint()}
	for net in [server, client]:
		net.auto_poll = false
		add_child(net)
		if net.configure(options) != OK:
			finish(false, "network configuration")
			return
	if server.host(0, "127.0.0.1") != OK:
		finish(false, "network listener")
		return
	entity = server.spawn(1, {"position": Vector3(0, 10, 0)})
	if entity == 0 or world.queue_create_sphere(entity, 1, Vector3(0, 10, 0), 0.5) != OK:
		finish(false, "networked body creation")
		return
	bridge = Bridge.new()
	var incompatible: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	incompatible.configure(120, 4, 1, Vector3(0, -9.8, 0))
	if bridge.attach(server, incompatible) != ERR_INVALID_PARAMETER:
		finish(false, "physics rate/profile mismatch must be rejected")
		return
	if bridge.attach(server, world) != OK or bridge.track(entity) != OK:
		finish(false, "physics clock attachment")
		return
	bridge.failed.connect(func(_error: Error): finish(false, "physics adapter error"))
	client.join("127.0.0.1", server.get_statistics().local_port)

func _process(_delta: float) -> void:
	if finished:
		return
	if Time.get_ticks_msec() - started > 12000:
		finish(false, "networked physics watchdog")
		return
	if server.poll() != OK or client.poll() != OK:
		finish(false, "native pump")
		return
	var record: Dictionary = client.get_entity(entity)
	if world.get_tick() >= 45 and not record.is_empty() and record.state.get("physics_tick", 0) >= 40:
		finish(record.state.position.y < 9.5 and world.get_tick() == server.get_statistics().tick, "native fixed clock, Box3D motion and replicated body state")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	if bridge != null:
		bridge.detach()
	for net in [server, client]:
		if net != null:
			net.close()
	print("EGP_NETWORK_PHYSICS " + JSON.stringify({"passed": passed, "message": message}))
	get_tree().quit(0 if passed else 1)
