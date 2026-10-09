extends Node

class Target extends Node:
	var first_value := 0.0
	var second_writes := 0
	var read_action: Callable
	var write_action: Callable
	@export var first: float:
		get:
			var result := first_value
			var action := read_action
			read_action = Callable()
			if action.is_valid(): action.call()
			return result
		set(value):
			first_value = value
			var action := write_action
			write_action = Callable()
			if action.is_valid(): action.call()
	@export var second := 0.0:
		set(value):
			second = value
			second_writes += 1

var checks := 0
var failed := false
var nested_error := OK
var nested_capture := PackedByteArray([1])

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failed = true
		push_error(message)

func configuration(smoothing := false) -> SuperpositionConfig:
	var config := SuperpositionConfig.new()
	var rules: Array[SuperpositionProperty] = []
	for property in [&"first", &"second"]:
		var rule := SuperpositionProperty.new()
		rule.property = property
		rule.value_type = TYPE_FLOAT
		rule.quantization = 0
		rule.smoothing = smoothing
		rules.append(rule)
	config.properties = rules
	return config

func pair(smoothing := false) -> Dictionary:
	var holder := Node.new()
	add_child(holder)
	var target := Target.new()
	target.name = "Target"
	holder.add_child(target)
	var component := Superposition.new()
	component.enabled = false
	component.target_path = NodePath("../Target")
	component.replication_key = "callback-test"
	component.config = configuration(smoothing)
	holder.add_child(component)
	return {"holder": holder, "target": target, "component": component}

func retire(test: Dictionary) -> void:
	if is_instance_valid(test.target) and not test.target.is_inside_tree(): test.target.free()
	if is_instance_valid(test.component) and not test.component.is_inside_tree(): test.component.free()
	if is_instance_valid(test.holder): test.holder.free()

func _ready() -> void:
	var source := pair()
	source.target.first_value = 10.0
	source.target.second = 20.0
	var bytes: PackedByteArray = source.component.capture_state()
	check(not bytes.is_empty(), "Capture complete valid two-property envelope")
	var bad: Array = bytes_to_var(bytes)
	bad[2][0][1] = TYPE_FLOAT + (1 << 32)
	var test := pair()
	check(test.component.apply_state(var_to_bytes(bad)) == ERR_INVALID_DATA, "Reject widened type tag before mutation")
	check(test.target.first_value == 0 and test.target.second_writes == 0, "Malformed schema makes no setter calls")
	retire(test)

	# Bound calls lock their receiver in debug builds. Exercise supported deletion
	# and detachment there; immediate receiver deletion also runs in release builds.
	var mutations := ["remove_target", "free_target", "remove_component", "queue_component", "session", "config"]
	if not OS.is_debug_build(): mutations.append("free_component")
	for mutation in mutations:
		test = pair()
		var current := test
		current.target.write_action = func():
			match mutation:
				"remove_target": current.holder.remove_child(current.target)
				"free_target": current.target.free()
				"remove_component": current.holder.remove_child(current.component)
				"queue_component": current.component.queue_free()
				"free_component": current.component.free()
				"session":
					var replacement := EGPNetSession.new()
					check(replacement.configure() == OK, "Configure replacement from setter")
					current.component.set_session(replacement)
				"config": current.component.config = configuration()
		var result: int = test.component.apply_state(bytes)
		check(result == ERR_UNAVAILABLE, "Abort setter transaction after " + mutation)
		if mutation == "queue_component": check(test.component.is_queued_for_deletion(), "Setter queued legal component deletion")
		if mutation == "remove_component": check(not test.component.is_inside_tree(), "Setter detached active component")
		if is_instance_valid(test.target):
			check(test.target.second_writes == 0, "Do not write remaining property after " + mutation)
		retire(test)

	test = pair()
	var recursive := test
	test.target.write_action = func(): nested_error = recursive.component.apply_state(bytes)
	check(test.component.apply_state(bytes) == OK and nested_error == ERR_BUSY, "Recursive apply is refused while outer apply completes")
	check(test.target.second == 20, "Outer validated application remains complete")
	test.target.read_action = func(): nested_capture = recursive.component.capture_state()
	check(not test.component.capture_state().is_empty() and nested_capture.is_empty(), "Recursive capture is bounded and refused")
	retire(test)

	for mutation in mutations:
		test = pair()
		var current := test
		test.target.read_action = func():
			match mutation:
				"remove_target": current.holder.remove_child(current.target)
				"free_target": current.target.free()
				"remove_component": current.holder.remove_child(current.component)
				"queue_component": current.component.queue_free()
				"free_component": current.component.free()
				"session": current.component.set_session(EGPNetSession.new())
				"config": current.component.config = configuration()
		check(test.component.capture_state().is_empty(), "Abort getter capture after " + mutation)
		if mutation == "queue_component": check(test.component.is_queued_for_deletion(), "Getter queued legal component deletion")
		if mutation == "remove_component": check(not test.component.is_inside_tree(), "Getter detached active component")
		retire(test)

	test = pair(true)
	check(test.component.apply_state(bytes) == OK, "Queue cosmetic smoothing values")
	var smoothed := test
	test.target.write_action = func(): smoothed.component.free()
	test.component.enabled = true
	await get_tree().process_frame
	await get_tree().process_frame
	check(not is_instance_valid(test.component) and test.target.second_writes == 0, "Smoothing stops after setter frees component")
	retire(test)
	retire(source)
	if not failed:
		print("SUPERPOSITION_CALLBACK_CHECKS_PASS ", checks)
	get_tree().quit(1 if failed else 0)
