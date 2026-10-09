@tool
extends EditorPlugin

class ReloadDebugger extends EditorDebuggerPlugin:
	var owner_plugin: EditorPlugin
	func _has_capture(prefix: String) -> bool:
		return prefix == "egp_reload"
	func _capture(message: String, data: Array, _session_id: int) -> bool:
		if message != "egp_reload:state":
			return false
		owner_plugin.write_json("sample.json", data[0])
		return true

var debugger: ReloadDebugger
var panel: Node
var last_request := -1
var busy := false

func _enter_tree() -> void:
	debugger = ReloadDebugger.new()
	debugger.owner_plugin = self
	add_debugger_plugin(debugger)
	set_process(true)

func _exit_tree() -> void:
	remove_debugger_plugin(debugger)

func write_json(path: String, value: Variant) -> void:
	var file := FileAccess.open("res://" + path, FileAccess.WRITE)
	file.store_string(JSON.stringify(value))
	file.close()

func _process(_delta: float) -> void:
	if busy or not FileAccess.file_exists("res://command.json"):
		return
	var command = JSON.parse_string(FileAccess.get_file_as_string("res://command.json"))
	if not command is Dictionary or int(command.get("id", -1)) == last_request:
		return
	last_request = int(command.id)
	busy = true
	run_command(command)

func run_command(command: Dictionary) -> void:
	var result := {"id": command.id, "passed": true}
	match command.action:
		"prepare":
			var panels := get_tree().root.find_children("NativeExtensionEditor", "", true, false)
			result.passed = panels.size() == 1
			if result.passed:
				panel = panels[0]
				result.passed = panel.create_extension("reload") == OK
		"build":
			result.passed = panel.build_extension("reload", false) == OK
			var deadline := Time.get_ticks_msec() + 900000
			while result.passed and panel.is_building():
				if Time.get_ticks_msec() > deadline:
					result.passed = false
					break
				await get_tree().process_frame
			result.build_result = panel.get_last_build_result()
		"play":
			EditorInterface.play_main_scene()
		"diagnostics":
			var messages: Array[String] = []
			for tree in get_tree().root.find_children("*", "Tree", true, false):
				var items: Array = []
				if tree.get_root():
					items.append(tree.get_root())
				while not items.is_empty():
					var item = items.pop_back()
					for column in tree.columns:
						var text: String = item.get_text(column)
						if text.contains(".NET:") or text.contains("GDExtension") or text.contains("dynamic library"):
							messages.append(text)
					items.append_array(item.get_children())
			result.diagnostics = messages
		"reload", "reload-native", "sample", "sample-abi", "drop", "hold", "rename-native":
			var sessions := debugger.get_sessions()
			result.passed = not sessions.is_empty() and sessions[0].is_active()
			if result.passed:
				if command.action == "reload":
					sessions[0].send_message("reload_all_scripts", [])
				else:
					sessions[0].send_message("egp_reload:" + command.action, [command.id])
		"close":
			for session in debugger.get_sessions():
				if session.is_active():
					session.send_message("egp_reload:finish", [])
			var deadline := Time.get_ticks_msec() + 15000
			while EditorInterface.is_playing_scene() and Time.get_ticks_msec() < deadline:
				await get_tree().process_frame
			result.passed = not EditorInterface.is_playing_scene()
			EditorInterface.stop_playing_scene()
			write_json("response.json", result)
			# Do not recursively notify the root while unloading this plugin.
			EditorInterface.get_base_control().get_parent().notification.call_deferred(Node.NOTIFICATION_WM_CLOSE_REQUEST)
			return
		_:
			result.passed = false
	write_json("response.json", result)
	busy = false
