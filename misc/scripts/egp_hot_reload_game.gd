extends Node

var native: Node
var managed: Node
var receiver: Node
var cpp_callable: Callable
var cs_callable: Callable
var cpp_hits := 0
var cs_hits := 0

func _ready() -> void:
	EngineDebugger.register_message_capture("egp_reload", _capture)
	native = ClassDB.instantiate("EGP_reload_Node")
	add_child(native)
	native.counter = 91
	native.pulse.connect(func(_value): cpp_hits += 1)
	cpp_callable = Callable(native, "get_message")
	managed = load("res://ReloadProbe.cs").new()
	receiver = load("res://ReloadReceiver.cs").new()
	add_child(managed)
	add_child(receiver)
	managed.Counter = 87
	managed.PositionValue = Vector3(3, 4, 5)
	managed.Reference = receiver
	managed.Pulse.connect(func(_value): cs_hits += 1)
	receiver.Listen(managed)
	cs_callable = Callable(managed, "Version")
	# An instance freed before reload must not be reconstructed.
	var temporary = load("res://ReloadProbe.cs").new()
	temporary.free()

func _capture(message: String, data: Array) -> bool:
	if message == "finish":
		get_tree().quit.call_deferred(0)
		return true
	if message != "sample":
		return false
	if not managed.has_method("Version"):
		EngineDebugger.send_message("egp_reload:state", [{
			"request": data[0], "placeholder": true,
			"cs_id": str(managed.get_instance_id()), "cs_counter": managed.get("Counter"),
		}])
		return true
	native.emit_signal("pulse", 1)
	managed.Fire()
	var state := {
		"request": data[0], "editor_hint": Engine.is_editor_hint(),
		"pid": OS.get_process_id(),
		"cpp_id": str(native.get_instance_id()), "cs_id": str(managed.get_instance_id()),
		"receiver_id": str(receiver.get_instance_id()), "cpp_counter": native.counter,
		"cs_counter": managed.Counter, "vector_ok": managed.PositionValue == Vector3(3, 4, 5),
		"reference_ok": managed.Reference == receiver,
		"parents_ok": native.get_parent() == self and managed.get_parent() == self,
		"cpp_version": native.get_message(), "cs_version": managed.Version(),
		"cpp_callable": cpp_callable.call(), "cs_callable": cs_callable.call(),
		"cpp_hits": cpp_hits, "cs_hits": cs_hits, "receiver_hits": receiver.Hits,
		"ready_count": managed.ReadyCount, "before_count": managed.BeforeCount,
		"after_count": managed.AfterCount, "collectible": managed.Collectible(),
	}
	EngineDebugger.send_message("egp_reload:state", [state])
	return true
