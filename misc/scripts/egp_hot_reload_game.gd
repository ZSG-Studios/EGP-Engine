extends Node

var native: Node
var managed: Node
var receiver: Node
var extra_managed: Node
var cpp_callable: Callable
var cs_callable: Callable
var cpp_hits := 0
var cs_hits := 0
var network: Node

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
	extra_managed = load("res://ReloadProbe.cs").new()
	add_child(extra_managed)
	receiver.Listen(extra_managed)
	cs_callable = Callable(managed, "Version")
	# An instance freed before reload must not be reconstructed.
	var temporary = load("res://ReloadProbe.cs").new()
	temporary.free()

func _capture(message: String, data: Array) -> bool:
	if message.begins_with("network-"):
		if network == null:
			network = load("res://network.gd").new()
			add_child(network)
			network.setup(native, managed)
		var proof: Dictionary = await network.run_action(message)
		proof.request = data[0]
		EngineDebugger.send_message("egp_reload:state", [proof])
		return true
	if message == "reload-native":
		var status := GDExtensionManager.reload_extension("res://extensions/reload/reload.gdextension")
		EngineDebugger.send_message("egp_reload:state", [{"request": data[0], "status": status}])
		return true
	if message == "hold":
		managed.HoldRoot(ProjectSettings.globalize_path("res://release-root"))
		return true
	if message == "drop":
		extra_managed.free()
		extra_managed = null
		return true
	if message == "rename-native":
		native.name = "RecoveredNative"
		return true
	if message == "finish":
		if network != null:
			network.close()
		get_tree().quit.call_deferred(0)
		return true
	if message == "sample-abi":
		EngineDebugger.send_message("egp_reload:state", [{
			"request": data[0], "cpp_id": str(native.get_instance_id()),
			"cpp_counter": native.counter, "cpp_name": str(native.name),
			"parent_ok": native.get_parent() == self,
			"cpp_version": native.call("get_message", 7),
			"cpp_callable": cpp_callable.call(7),
		}])
		return true
	if message != "sample":
		return false
	var extension := GDExtensionManager.get_extension("res://extensions/reload/reload.gdextension")
	if not extension.is_library_open() or not native.has_method("get_message"):
		# Failed native loads retain the parent object, with no extension methods.
		# Do not call cached native methods until a valid library is restored.
		EngineDebugger.send_message("egp_reload:state", [{
			"request": data[0], "native_unavailable": true,
			"cpp_id": str(native.get_instance_id()), "base_class": native.get_class(),
			"parent_ok": native.get_parent() == self, "cpp_name": str(native.name),
		}])
		return true
	if not managed.has_method("Version"):
		EngineDebugger.send_message("egp_reload:state", [{
			"request": data[0], "placeholder": true,
			"cs_id": str(managed.get_instance_id()), "cs_counter": managed.get("Counter"),
			"extra_alive": is_instance_valid(extra_managed),
		}])
		return true
	native.emit_signal("pulse", 1)
	managed.Fire()
	var state := {
		"request": data[0], "editor_hint": Engine.is_editor_hint(),
		"pid": OS.get_process_id(),
		"cpp_id": str(native.get_instance_id()), "cs_id": str(managed.get_instance_id()),
		"receiver_id": str(receiver.get_instance_id()), "cpp_counter": native.counter,
		"cpp_name": str(native.name),
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
