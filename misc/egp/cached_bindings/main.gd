extends Node

var victim: EGPBindingVictim
var observer: EGPBindingObserver
var mode: String
var stage := "initial"
var checks := 0
var failed := false

func require(good: bool, message: String) -> void:
	checks += 1
	if not good:
		failed = true
		print("EGP_CACHED_BINDING_FAILED ", stage, ": ", message)
		get_tree().quit(1)
		assert(good, message)

func checkpoint(value: String) -> void:
	stage = value
	var file := FileAccess.open("res://stage.json", FileAccess.WRITE)
	file.store_string(JSON.stringify({"stage": stage, "mode": mode, "pid": OS.get_process_id()}))
	file.close()

func select_library(library: String) -> void:
	var file := FileAccess.open("res://victim.gdextension", FileAccess.WRITE)
	file.store_string('[configuration]\nentry_symbol="fixture_init"\ncompatibility_minimum="4.8"\nreloadable=true\n[libraries]\nwindows.debug.x86_64="res://bin/%s.dll"\n' % library)
	file.close()

func typed_instance() -> int:
	return victim.read_value()

func typed_static() -> int:
	return EGPBindingVictim.read_static()

func typed_extra() -> Array:
	var text: String = victim.read_text()
	var vector: Vector3 = victim.read_vector()
	var array: Array = victim.read_array()
	var object: Object = victim.read_object()
	var bytes: PackedByteArray = victim.read_bytes()
	var variant: Variant = victim.read_variant()
	var boolean: bool = victim.read_bool()
	var number: float = victim.read_float()
	return [text, vector, array, object, bytes, variant, boolean, number]

func probe_extra(blocked: bool) -> void:
	checkpoint(stage + "-extra")
	var expected: Array = ["", Vector3.ZERO, [], null, PackedByteArray(), null, false, 0.0] if blocked else ["live", Vector3(1, 2, 3), [91], victim, PackedByteArray([91]), "live", true, 91.5]
	if mode.begins_with("validated"):
		var actual := typed_extra()
		require(typeof(actual[3]) == TYPE_OBJECT and actual[3] == (null if blocked else victim), "typed object default/recovery")
		# A typed null Object and an untyped NIL differ in Array equality.
		actual.remove_at(3)
		expected.remove_at(3)
		require(actual == expected, "typed builtin return defaults/recovery")
	elif mode.begins_with("ptr"):
		expected[3] = not blocked
		expected.insert(4, blocked)
		require(observer.probe_extra(victim, true) == expected, "ptrcall initialized builtin return defaults/recovery")
	else:
		var results: Array = observer.probe_extra(victim, false)
		for i in range(8):
			require(results[i * 2] == (null if blocked else expected[i]) and results[i * 2 + 1] == (1 if blocked else 0), "call builtin result/error " + str(i))

func probe(expected: int, blocked: bool) -> void:
	checkpoint(stage + "-call")
	if mode.begins_with("validated"):
		var value := typed_static() if mode.ends_with("static") else typed_instance()
		require(value == (0 if blocked else expected), "validated return")
	else:
		var result: Dictionary = observer.probe(victim, mode.begins_with("ptr"), mode.ends_with("static"))
		if mode.begins_with("ptr"):
			require(result.value == (0 if blocked else expected), "ptrcall return")
		elif blocked:
			require(result.type == TYPE_NIL and result.error == 1, "invalid binding returns nil/invalid-method error")
		else:
			require(result.type == TYPE_INT and result.error == 0 and result.value == expected, "call result")

func _ready() -> void:
	mode = OS.get_cmdline_user_args()[0]
	victim = EGPBindingVictim.new()
	observer = EGPBindingObserver.new()
	add_child(victim)
	add_child(observer)
	victim.name = "original"
	victim.counter = 91
	var identity := victim.get_instance_id()
	require(observer.cache(), "raw cache initialized")
	probe(100 if mode.ends_with("static") else 191, false)
	probe_extra(false)
	for fault in ["missing", "invalid"]:
		checkpoint(fault)
		select_library(fault)
		require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_FAILED, "failed load status")
		require(victim.get_class() == "Node", "retained native parent")
		victim.name = fault + "-parent-edit"
		probe(0, true)
		probe_extra(true)
		select_library("victim2" if fault == "missing" else "victim3")
		require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_OK, "compatible repair")
		require(victim.get_instance_id() == identity and victim.counter == 91 and victim.name == fault + "-parent-edit", "identity/extension/edited parent state")
		checkpoint(fault + "-repaired")
		probe((200 if fault == "missing" else 300) + (0 if mode.ends_with("static") else 91), false)
		probe_extra(false)
	checkpoint("signature")
	select_library("victim4")
	require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_OK, "changed signature reload")
	probe(0, true)
	select_library("victim3")
	require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_OK, "signature restored")
	probe(0, true) # Retired signature bindings stay invalid, even after repair.
	if not mode.begins_with("validated"):
		require(observer.cache(), "explicit raw cache refresh")
		probe(300 if mode.ends_with("static") else 391, false)
		checkpoint("removed-class")
		select_library("victim5")
		require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_NEEDS_RESTART, "class removal status")
		probe(0, true)
		select_library("victim3")
		require(GDExtensionManager.reload_extension("res://victim.gdextension") == GDExtensionManager.LOAD_STATUS_OK, "removed class repair")
		probe(0, true)
		require(observer.cache(), "removed-class cache refresh")
		probe(300 if mode.ends_with("static") else 391, false)
	if failed:
		get_tree().quit(1)
		return
	checkpoint("complete")
	print("EGP_CACHED_BINDING_PASSED ", JSON.stringify({"checks": checks, "mode": mode, "pid": OS.get_process_id()}))
	get_tree().quit()
