extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
var server: Node
var low: RefCounted
var cpp: Node
var replies := 0
var applications := 0
var frames := 0
var sent := false
func _ready() -> void:
	server = Net.new()
	server.auto_poll = false
	add_child(server)
	assert(server.configure() == OK and server.host(0) == OK)
	assert(server.register_message(&"reply", func(_peer: int, args: Array):
		if args == ["cpp", 88]:
			replies += 1, Net.Sender.CLIENT) == OK)
	low = ClassDB.instantiate("EGPNetSession")
	assert(low.configure() == OK and low.listen(0) == OK)
	low.application_received.connect(func(_peer: int, bytes: PackedByteArray):
		if bytes == PackedByteArray([9, 8, 7]):
			applications += 1)
	cpp = ClassDB.instantiate("EGPNetCppProbe")
	add_child(cpp)
	var token: Dictionary = server.issue_token(333, "127.0.0.1:%d" % server.get_statistics().local_port)
	var low_token: Dictionary = low.issue_token(444, "127.0.0.1:%d" % low.get_statistics().local_port)
	assert(token.error == OK and low_token.error == OK)
	assert(cpp.start(token.token, low_token.token) == OK)
	assert(cpp.prediction_check())
	assert(server.spawn(12, {"stamp": 55, "position": Vector3(1, 2, 3)}) > 0)
	assert(low.command("spawn", {"kind": 7, "state": PackedByteArray([1, 2, 3])}).error == OK)
func _process(_delta: float) -> void:
	frames += 1
	server.poll()
	low.poll()
	cpp.poll()
	if frames > 600:
		finish(false)
		return
	if not sent and server.get_peers().size() == 1 and low.command("peers").size() == 1:
		sent = true
		assert(server.broadcast_message(&"hello", [77, Vector3(1, 2, 3)]) == OK)
		assert(server.broadcast_packet(PackedByteArray([4, 5, 6]), 3, Net.Delivery.UNRELIABLE) == OK)
		assert(low.send_application(low.command("peers")[0].peer_id, PackedByteArray([9, 8, 7])) == OK)
	var status: Dictionary = cpp.status()
	if replies == 1 and applications == 1 and status.messages == 1 and status.packets == 1 and status.entities.size() == 1 and status.low_entities.size() == 1:
		finish(status.record.state.stamp == 55)
func finish(passed: bool) -> void:
	server.close()
	low.close()
	cpp.stop()
	print("EGP_CPP_NETWORK_" + ("PASSED" if passed else "FAILED"))
	get_tree().quit(0 if passed else 1)
