extends SceneTree
## Capture actual reflected property/signal metadata missing from the extension API.

func _initialize() -> void:
	var output := ""
	var api := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--output="):
			output = argument.trim_prefix("--output=")
		elif argument.begins_with("--api="):
			api = argument.trim_prefix("--api=")
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
