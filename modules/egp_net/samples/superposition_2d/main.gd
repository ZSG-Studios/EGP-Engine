extends Node
# Real native sessions; this project imports no 3D node scripts or scenes.
class Actor extends Node:
	@export var value: int = 0

var server := EGPNetSession.new()
var client := EGPNetSession.new()
var checks := 0
var failed := false
var source: Superposition
var replica: Superposition

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failed = true
		push_error(message)

func component(actor: Actor, session: EGPNetSession) -> Superposition:
	var node := Superposition.new()
	node.enabled = false
	node.replication_key = "generic-property"
	var config := SuperpositionConfig.new()
	var property := SuperpositionProperty.new()
	property.property = "value"
	property.value_type = TYPE_INT
	config.properties = [property]
	node.config = config
	actor.add_child(node)
	node.set_session(session)
	return node

func pump() -> void:
	for frame in range(30):
		server.poll()
		client.poll()
		if client.get_state() == "Connected":
			replica.replicate_now()
		await get_tree().process_frame

func _ready() -> void:
	var options := {"allow_insecure_loopback": true, "game_protocol": "superposition-2d-fixture", "max_entities": 8}
	check(server.configure(options) == OK and client.configure(options) == OK, "Configure native generic replication sessions")
	check(server.listen(0, "127.0.0.1") == OK, "Listen on loopback")
	var actor := Actor.new()
	var copy := Actor.new()
	add_child(actor)
	add_child(copy)
	source = component(actor, server)
	replica = component(copy, client)
	var no_3d := not ClassDB.class_exists("Node3D")
	if no_3d:
		source.config.interest_radius = 5.0
		check(source.capture_state().is_empty(), "2D-only schema rejects spatial capture")
		check(source.replicate_now() == ERR_UNAVAILABLE and server.command("entities").is_empty(), "Unsupported spatial policy rejects before spawning")
		source.config.interest_radius = INF
		check(source.replicate_now() == ERR_INVALID_PARAMETER and server.command("entities").is_empty(), "Nonfinite spatial policy retains invalid-parameter validation")
		source.config.interest_radius = 0.0
	actor.value = 17
	check(source.replicate_now() == OK and server.command("entities").size() == 1, "Generic property spawns without 3D support")
	check(client.connect_to_server("127.0.0.1", int(server.get_statistics().local_port)) == OK, "Connect native generic client")
	await pump()
	check(client.get_state() == "Connected" and copy.value == 17, "Native generic late-join baseline applies")
	if no_3d:
		var before: Array = server.command("entities")
		var sent: int = source.get_statistics().sent
		source.config.interest_radius = 5.0
		actor.value = 33
		check(source.replicate_now() == ERR_UNAVAILABLE and server.command("entities") == before and source.get_statistics().sent == sent, "Unsupported mutable policy rejects before updating")
		source.config.interest_radius = 0.0
	actor.value = 47
	check(source.replicate_now() == OK, "Generic dirty property updates")
	await pump()
	check(copy.value == 47, "Native generic property update applies")
	server.stop()
	client.stop()
	print("EGP_SUPERPOSITION_2D " + JSON.stringify({"passed": not failed, "checks": checks, "disable_3d": no_3d}))
	get_tree().quit(1 if failed else 0)
