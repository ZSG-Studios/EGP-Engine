extends SceneTree
## Capture actual reflected property/signal metadata missing from the extension API.

func _initialize() -> void:
	var output := ""
	var api := ""
	var contract := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument.begins_with("--api="):
			api = argument.trim_prefix("--api=")
		elif argument.begins_with("--changes="):
			contract = argument.trim_prefix("--changes=")
	if output.is_empty() or not FileAccess.file_exists(api):
		push_error("ClassDB dump requires --output=FILE and --api=ACTUAL_EXTENSION_API")
		quit(2)
		return
	var classes: Dictionary = {}
	for name in ClassDB.get_class_list():
		classes[name] = {
			"properties": ClassDB.class_get_property_list(name, true),
			"signals": ClassDB.class_get_signal_list(name, true),
			"parent": ClassDB.get_parent_class(name),
		}
	if not contract.is_empty():
		var changes: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(contract))
		for name in changes.get("property_accessors", {}):
			var instance: Object = ClassDB.instantiate(name)
			var verified: Array = []
			for property in changes.property_accessors[name]:
				var mapping: Dictionary = changes.property_accessors[name][property]
				var original: Variant = instance.get(property)
				if typeof(original) != TYPE_BOOL or not instance.has_method(mapping.getter) or not instance.has_method(mapping.setter):
					push_error("Invalid property accessor contract: %s.%s" % [name, property])
					instance.free()
					quit(1)
					return
				instance.set(property, not original)
				var getter_matches: bool = instance.call(mapping.getter) == not original
				instance.call(mapping.setter, original)
				if not getter_matches or instance.get(property) != original:
					push_error("Property accessor behavior mismatch: %s.%s" % [name, property])
					instance.free()
					quit(1)
					return
				verified.append({"name": property, "type": TYPE_BOOL, "getter": mapping.getter, "setter": mapping.setter})
			classes[name]["verified_property_accessors"] = verified
			instance.free()
	var executable := OS.get_executable_path()
	var receipt := {
		"engine": executable,
		"engine_sha256": FileAccess.get_sha256(executable),
		"extension_api_sha256": FileAccess.get_sha256(api),
		"classes": classes,
	}
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		push_error("Cannot write ClassDB dump")
		quit(1)
		return
	file.store_string(JSON.stringify(receipt, "\t") + "\n")
	file.close()
	print("EGP_CLASSDB_DUMP: %d classes" % classes.size())
	quit(0)
