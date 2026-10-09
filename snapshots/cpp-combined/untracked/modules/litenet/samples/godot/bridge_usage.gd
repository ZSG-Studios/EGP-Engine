extends Node

# Supply the EGPLiteRuntime.Bridge created by a retained C# autoload.
# The game registers its typed entities in C# once; GDScript/native callers share
# this session instead of invoking Godot's removed scene replication/RPC protocol.
var session: EGPLiteSession

func attach_session(managed_bridge: EGPLiteSession) -> void:
	session = managed_bridge
	session.application_received.connect(_on_application_received)

func _process(_delta: float) -> void:
	if session != null:
		var result := session.poll()
		if result != OK:
			push_error("EGP networking pump failed: %s" % result)

func _on_application_received(peer_id: int, payload: PackedByteArray) -> void:
	print("Application message from %s: %s bytes" % [peer_id, payload.size()])

func _exit_tree() -> void:
	if session != null:
		session.stop()
