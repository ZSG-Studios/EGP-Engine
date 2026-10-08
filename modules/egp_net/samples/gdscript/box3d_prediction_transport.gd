extends Node
const Adapter = preload("res://addons/egp_net/egp_net_box3d_prediction.gd")
var checks := 0
var networks: Array[Node] = []
var adapters: Array[Node] = []

func require(value: bool, label: String) -> bool:
	checks += 1
	if not value:
		push_error(label)
		get_tree().quit(1)
	return value

func make_world() -> RefCounted:
	var world: RefCounted = ClassDB.instantiate("EGPBox3DWorld")
	world.configure(60, 4, 1, Vector3.ZERO)
	world.queue_create_sphere(1, 1, Vector3.ZERO, 0.5)
	world.queue_create_box(2, 1, Vector3(1.1, 0, 0), Vector3.ONE * 0.5)
	world.apply_queued_commands()
	return world

func apply_input(world: RefCounted, tick: int, input: PackedByteArray, _replay: bool) -> Error:
	return world.queue_impulse(1, tick + 1, Vector3(input.decode_s16(0) / 32767.0 * 0.1, 0, 0))

func network(world: RefCounted) -> Node:
	var node: Node = ClassDB.instantiate("SuperpositionWorld")
	node.auto_poll = false
	add_child(node)
	var result: Error = node.configure({"game_protocol":"box3d-prediction-transport-v1", "simulation_fingerprint":world.get_simulation_fingerprint(), "tick_rate":60, "allow_insecure_loopback":true, "simulated_latency_ms":30.0, "simulated_jitter_ms":20.0, "simulated_loss":5.0})
	if not require(result == OK and node.get_simulation_fingerprint() == world.get_simulation_fingerprint() and node.get_tick_rate() == 60, "Configured provider reflects actual session option overrides"): return null
	networks.append(node)
	return node

func adapter(world: RefCounted, net: Node) -> Node:
	var node := Adapter.new()
	node.auto_predict = false
	node.send_inputs = false
	node.world_provider_path = NodePath("")
	add_child(node)
	if not require(node.start_prediction(world, net, func(_tick: int): return Adapter.encode_player_input(1,0,0), apply_input) == OK, "Native World binds prediction automatically through get_session"): return null
	adapters.append(node)
	return node

func pump() -> bool:
	for net in networks:
		if not require(net.poll() == OK, "Native session poll stays within unchanged watchdog"): return false
	return true

func _ready() -> void:
	var authority := make_world()
	var server := network(authority)
	if server == null: return
	server.port = 0
	if not require(server.start_server() == OK, "Dedicated native World listens"): return
	var port: int = server.get_session().get_statistics().local_port
	var worlds: Array[RefCounted] = []
	var clients: Array[Node] = []
	for index in 2:
		var world := make_world()
		var client := network(world)
		if client == null: return
		client.port = port
		client.address = "127.0.0.1"
		if not require(client.join_loopback() == OK, "Independent native client joins explicit test-only loopback"): return
		worlds.append(world)
		clients.append(client)
	var deadline := Time.get_ticks_msec() + 5000
	while server.get_session().command("peers").size() < 2 or clients[0].get_session().get_state() != "Connected" or clients[1].get_session().get_state() != "Connected":
		if Time.get_ticks_msec() > deadline: require(false, "Two native clients connection timeout"); return
		if not pump(): return
		await get_tree().create_timer(0.005).timeout
	var sender := adapter(authority, server)
	var receivers: Array[Node] = []
	for index in 2:
		var receiver := adapter(worlds[index], clients[index])
		if receiver == null: return
		receivers.append(receiver)
	var frames: Array[Dictionary] = []
	for tick in range(1, 9):
		for receiver in receivers:
			if not require(receiver.predict_next() == OK, "Local client predicts native complete-world input"): return
		var input: PackedByteArray = Adapter.encode_player_input(-1 if tick == 3 else 1,0,0)
		apply_input(authority, tick, input, false)
		authority.step_tick(tick)
		frames.append({"tick":tick,"input":input,"hash":authority.get_state_hash()})
	var peers := PackedInt64Array()
	for peer in server.get_session().command("peers"): peers.append(peer.peer_id)
	# Authoritative windows arrive out of tick order; missing1/2 are retransmitted.
	for index in [2,3,4,5,6,7,0,1,1]:
		if not require(sender.publish_authority(frames[index], peers) == OK, "Canonical inputs and hashes sent through actual native reliable transport"): return
	deadline = Time.get_ticks_msec() + 5000
	while receivers[0].get_statistics().acknowledged_tick < 8 or receivers[1].get_statistics().acknowledged_tick < 8:
		if Time.get_ticks_msec() > deadline: require(false, "Jitter/loss retransmission convergence timeout"); return
		if not pump(): return
		await get_tree().create_timer(0.005).timeout
	for index in 2:
		if not require(worlds[index].get_state_hash() == authority.get_state_hash() and receivers[index].get_statistics().pending_ticks == 0, "Real native transport correction converges exact collision-world hash"): return
		if not require(not receivers[index].get_statistics().failed, "No unexpected deterministic mismatch"): return
	for component in adapters: component.stop_prediction()
	for net in networks: net.stop()
	print("EGP_BOX3D_PREDICTION_TRANSPORT_PASS " + JSON.stringify({"checks":checks,"clients":2,"verified_ticks":8,"latency_each_way_ms":30,"jitter_ms":20,"loss_percent":5,"state_hash":authority.get_state_hash()}))
	get_tree().quit()
