extends "res://addons/egp_net/egp_net.gd"
var hello_count := 0
var packet_count := 0
func prepare(token: PackedByteArray) -> Error:
	auto_poll = false
	var error := configure()
	if error != OK:
		return error
	error = register_message(&"hello", _hello, Sender.SERVER)
	if error != OK:
		return error
	packet_received.connect(_packet)
	return join_token(111, token)
func _hello(peer: int, args: Array) -> void:
	if args == [77, Vector3(1, 2, 3)]:
		hello_count += 1
		send_message(peer, &"reply", ["gdscript", 88])
func _packet(_peer: int, bytes: PackedByteArray, channel: int, delivery: int) -> void:
	if bytes == PackedByteArray([4, 5, 6]) and channel == 3 and delivery == Delivery.UNRELIABLE:
		packet_count += 1
