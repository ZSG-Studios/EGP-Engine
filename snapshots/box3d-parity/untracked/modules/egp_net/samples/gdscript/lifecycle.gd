extends Node
const Net = preload("res://addons/egp_net/egp_net.gd")
var server: Node
var client: Node
var started := Time.get_ticks_msec()
var phase := 0
var closed := false
var finished := false
func _ready() -> void:
	var transient := Net.new()
	add_child(transient)
	transient.configure({"allow_insecure_loopback": true})
	transient.state_changed.connect(func(state: String):
		if state == "Listening":
			transient.close())
	if transient.host(0, "127.0.0.1") != OK or transient.session != null:
		finish(false, "close from listener state callback")
		return
	transient.queue_free()
	server = Net.new()
	client = Net.new()
	for net in [server, client]:
		net.auto_poll = false
		add_child(net)
		if net.configure({"allow_insecure_loopback": true}) != OK:
			finish(false, "lifecycle configuration")
			return
	client.register_message(&"close", func(_peer: int, _args: Array):
		closed = true
		client.close(), Net.Sender.SERVER)
	server.host(0, "127.0.0.1")
	client.join("127.0.0.1", server.get_statistics().local_port)

func _process(_delta: float) -> void:
	if finished:
		return
	if Time.get_ticks_msec() - started > 8000:
		finish(false, "lifecycle watchdog")
		return
	server.poll()
	if client.session != null:
		client.poll()
	if phase == 0 and client.get_state() == "Connected":
		if client.host(0, "127.0.0.1") != ERR_BUSY or client.is_server() or server.join("127.0.0.1", server.get_statistics().local_port) != ERR_BUSY or not server.is_server():
			finish(false, "busy endpoint calls must preserve roles")
			return
		server.send_message(server.get_peers()[0].peer_id, &"close", [])
		phase = 1
	elif phase == 1 and closed and server.get_peers().is_empty():
		server.state_changed.connect(func(state: String):
			if state == "Stopped":
				server.close())
		server.stop()
		finish(server.session == null and client.session == null, "close from listen, receive and stop callbacks; endpoint roles preserved")

func finish(passed: bool, message: String) -> void:
	if finished:
		return
	finished = true
	for net in [server, client]:
		if net != null:
			net.close()
	print("EGP_NETWORK_LIFECYCLE " + JSON.stringify({"passed": passed, "message": message}))
	get_tree().quit(0 if passed else 1)
