extends Node

var native: Node
var managed: Node
var receiver: Node
var extra_managed: Node
var cpp_callable: Callable
var cs_callable: Callable
var cpp_hits := 0
var cs_hits := 0
# Box3D mode (egp_reload/box3d): the C# probe steps one live world, the C++ node reads
# it, and an uninterrupted reference world plus the previous sample's snapshot prove
# that reloads leave the deterministic state untouched.
const BOX_BODY := 10000
var box3d := false
var box_world: EGPBox3DWorld
var box_reference: EGPBox3DWorld
var box_snapshot := PackedByteArray()
var box_snapshot_tick := -1

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
	box3d = ProjectSettings.get_setting("egp_reload/box3d", false)
	if box3d:
		box_world = _make_box_world()
		box_reference = _make_box_world()
		native.world = box_world
		managed.Box = {"world": box_world}

func _make_box_world() -> EGPBox3DWorld:
	var world := EGPBox3DWorld.new()
	var ok := world.configure(60, 4, 1) == OK
	ok = ok and world.queue_create_box(1, 0, Vector3(0, -0.5, 0), Vector3(20, 0.5, 20), 0) == OK
	for i in range(3):
		ok = ok and world.queue_create_box(100 + i, 0, Vector3(0, 0.5 + i * 1.01, 0), Vector3.ONE * 0.5, 2, 1.0) == OK
	# Off the ground, so it falls for the whole run.
	ok = ok and world.queue_create_box(BOX_BODY, 0, Vector3(100, 10000, 0), Vector3.ONE * 0.5, 2, 1.0) == OK
	assert(ok and world.apply_queued_commands() == OK, "Box3D fixture world")
	return world

func _box3d_state() -> Dictionary:
	var tick := box_world.get_tick()
	var state_hash := box_world.get_state_hash()
	while box_reference.get_tick() < tick:
		box_reference.step_tick(box_reference.get_tick() + 1)
	# The previous sample's snapshot, taken before the intervening reloads, must
	# step forward to exactly the live state.
	var replay_ok := true
	if not box_snapshot.is_empty():
		var restored := EGPBox3DWorld.new()
		replay_ok = restored.configure(60, 4, 1) == OK and restored.restore_snapshot(box_snapshot) == OK
		while replay_ok and restored.get_tick() < tick:
			replay_ok = restored.step_tick(restored.get_tick() + 1) == OK
		replay_ok = replay_ok and restored.get_state_hash() == state_hash
	var replayed_from := box_snapshot_tick
	box_snapshot = box_world.capture_snapshot()
	box_snapshot_tick = tick
	var body: Dictionary = box_world.get_body_state(BOX_BODY)
	var cpp: Dictionary = native.get_physics_state()
	var cs: Dictionary = managed.BoxState()
	return {
		"world_id": str(box_world.get_instance_id()), "tick": tick, "hash": state_hash,
		"reference_hash": box_reference.get_state_hash(), "position_y": body.position.y,
		"steps": managed.BoxSteps, "failures": managed.BoxStepFailures,
		"references_ok": native.world == box_world and managed.Box.get("world") == box_world,
		"cpp_state_ok": cpp.get("tick") == tick and cpp.get("hash") == state_hash and cpp.get("position") == body.position,
		"cs_state_ok": cs.get("tick") == tick and cs.get("hash") == state_hash and cs.get("position") == body.position,
		"replay_ok": replay_ok, "replayed_from": replayed_from, "snapshot_bytes": box_snapshot.size(),
	}

func _reload_native(data: Array) -> void:
	var status := GDExtensionManager.reload_extension("res://extensions/reload/reload.gdextension")
	EngineDebugger.send_message("egp_reload:state", [{"request": data[0], "status": status}])

func _capture(message: String, data: Array) -> bool:
	if message == "reload-native":
		_reload_native.call_deferred(data)
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
	if box3d:
		state.box3d = _box3d_state()
	EngineDebugger.send_message("egp_reload:state", [state])
	return true
