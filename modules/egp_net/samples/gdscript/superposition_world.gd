extends Node

var checks := 0
var failed := false
var ticks := 0
var packets := 0
var server: SuperpositionWorld
var client: SuperpositionWorld

func actor(parent: SuperpositionWorld, position: Vector3) -> Node3D:
	var target := Node3D.new()
	target.name = "Actor"
	target.position = position
	parent.add_child(target)
	var rule := SuperpositionProperty.new()
	rule.property = &"position"
	rule.value_type = TYPE_VECTOR3
	var config := SuperpositionConfig.new()
	config.properties = [rule]
	config.delta_replication = true
	config.update_rate = 30
	var component := Superposition.new()
	component.config = config
	component.replication_key = "world-restart-actor"
	target.add_child(component)
	return target

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failed = true
		push_error(message)

func _ready() -> void:
	server = SuperpositionWorld.new()
	server.port = 0
	server.allow_insecure_loopback = true
	server.simulation_tick.connect(func(_tick: int, _server: bool): ticks += 1)
	server.application_received.connect(func(_peer: int, payload: PackedByteArray):
		if payload == PackedByteArray([17, 28, 39]): packets += 1)
	add_child(server)
	check(server.start_server() == OK, "Inspector World starts server")
	check(server.get_session().get_state() == "Listening", "World exposes configured native session")
	var source := actor(server, Vector3(2, 3, 4))
	check(server.configure() == ERR_ALREADY_IN_USE, "World refuses implicit active session replacement")
	client = SuperpositionWorld.new()
	client.role = 1
	client.port = server.get_session().get_statistics().local_port
	add_child(client)
	check(client.start() == ERR_UNAUTHORIZED and client.get_session() == null, "Local client requires explicit loopback checkbox")
	client.allow_insecure_loopback = true
	var replica := actor(client, Vector3.ZERO)
	check(client.start() == OK, "Inspector World starts local client")
	var deadline := Time.get_ticks_msec() + 3000
	while Time.get_ticks_msec() < deadline and client.get_session().get_state() != "Connected":
		await get_tree().process_frame
	check(client.get_session().get_state() == "Connected", "World automatically polls client and server")
	check(ticks > 0, "World forwards fixed simulation ticks")
	var peers: Array = client.get_session().command("peers")
	check(peers.size() == 1, "World client has one authenticated transport peer")
	if not peers.is_empty():
		check(client.get_session().send_application(peers[0].peer_id, PackedByteArray([17, 28, 39])) == OK, "World native session sends reliable payload")
	deadline = Time.get_ticks_msec() + 2000
	while Time.get_ticks_msec() < deadline and packets == 0:
		await get_tree().process_frame
	check(packets == 1, "World forwards received application payload once")
	deadline = Time.get_ticks_msec() + 2000
	while Time.get_ticks_msec() < deadline and replica.position != source.position:
		await get_tree().process_frame
	check(replica.position == source.position, "Child replication discovers World without session paths or code binding")
	var previous := server.get_session()
	client.stop()
	server.stop()
	check(server.get_session() == null and previous.get_state() == "Stopped", "World closes and releases previous session")
	check(server.start_server() == OK and server.get_session() != previous, "Restart replaces session identity for child rebinding")
	client.port = server.get_session().get_statistics().local_port
	source.position = Vector3(7, 8, 9)
	check(client.start() == OK, "Restart client on new World session")
	deadline = Time.get_ticks_msec() + 3000
	while Time.get_ticks_msec() < deadline and replica.position != source.position:
		await get_tree().process_frame
	check(replica.position == source.position, "Child replication automatically rebinds after World restart")
	check(server.get("status/statistics").state == "Listening", "World exposes runtime state in Inspector")
	client.stop()
	server.stop()
	check(server.configure({"unknown_option": 1}) == ERR_INVALID_PARAMETER and server.get_session() == null, "Invalid configuration leaves no half-bound session")
	check(server.get_statistics().last_error != "", "World reports actionable errors")
	var packed := PackedScene.new()
	var authoring := SuperpositionWorld.new()
	authoring.auto_start = true
	authoring.role = 1
	authoring.allow_insecure_loopback = true
	check(packed.pack(authoring) == OK, "Inspector settings serialize")
	var restored := packed.instantiate() as SuperpositionWorld
	check(restored.auto_start and restored.role == 1 and restored.allow_insecure_loopback, "Inspector settings survive scene reload")
	restored.free()
	authoring.free()
	print("EGP_SUPERPOSITION_WORLD ", JSON.stringify({"passed": not failed, "checks": checks}))
	get_tree().quit(1 if failed else 0)
