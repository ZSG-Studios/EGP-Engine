extends SceneTree

# Run in a disposable project using: EGP --headless --editor --path <project> --script <this file>
# A compiler and CMake must be available. The bindings are extracted from the editor itself.
var panel: Node
var diagnostics: Array = []

func _initialize() -> void:
	call_deferred("run_test")

func require(condition: bool, message: String) -> bool:
	if not condition:
		if is_instance_valid(panel):
			for log in panel.find_children("*", "RichTextLabel", true, false):
				print(log.get_parsed_text())
		push_error(message)
		quit(1)
	return condition

func finish_build() -> bool:
	var deadline := Time.get_ticks_msec() + 900000
	while panel.is_building():
		if Time.get_ticks_msec() > deadline:
			require(false, "Native extension build timed out")
			return false
		await process_frame
	return true

func run_test() -> void:
	await process_frame
	var panels := root.find_children("NativeExtensionEditor", "", true, false)
	if not require(panels.size() == 1, "Built-in C++ editor panel was not found"):
		return
	panel = panels[0]
	print("EGP_CPP_STAGE: create and cold debug build")
	panel.diagnostic_found.connect(func(path: String, line: int): diagnostics.append([path, line]))
	if not require(panel.create_extension("smoke") == OK, "Extension scaffolding failed"):
		return
	if not require(panel.build_extension("smoke", false) == OK, "Debug build did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() == 0, "Debug build failed"):
		return
	if not require(ClassDB.class_exists("EGP_smoke_Node"), "Custom C++ node was not registered"):
		return
	var node = ClassDB.instantiate("EGP_smoke_Node")
	if not require(node.get_message() == "Hello from smoke!", "Custom C++ method returned the wrong value"):
		return
	node.free()
	print("EGP_CPP_STAGE: second extension and shared SDK cache")
	if not require(panel.create_extension("second") == OK, "Second extension scaffolding failed"):
		return
	var selector = panel.find_children("*", "OptionButton", true, false)[0]
	if not require(selector.get_item_text(selector.get_selected()) == "second", "New extension was not selected"):
		return
	if not require(panel.build_extension("second", false) == OK, "Second extension build did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() == 0 and ClassDB.class_exists("EGP_second_Node"),
			"Second extension failed to reuse the SDK library and register its node"):
		return
	var descriptor := "res://extensions/smoke/smoke.gdextension"
	print("EGP_CPP_STAGE: deliberate compile failure and diagnostic navigation")
	var before := FileAccess.get_file_as_string(descriptor)
	var source_path := "res://extensions/smoke/src/extension.cpp"
	var source := FileAccess.get_file_as_string(source_path)
	var file := FileAccess.open(source_path, FileAccess.WRITE)
	file.store_string(source + "\n#error EGP deliberate diagnostic fixture\n")
	file.close()
	if not require(panel.build_extension("smoke", false) == OK, "Failed-build fixture did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() != 0, "Invalid C++ unexpectedly compiled"):
		return
	if not require(FileAccess.get_file_as_string(descriptor) == before, "Failed build changed the published descriptor"):
		return
	var logs := panel.find_children("*", "RichTextLabel", true, false)
	if not require(logs.size() == 1 and logs[0].get_parsed_text().contains("EGP deliberate diagnostic fixture"),
			"Compiler diagnostics were not captured by the editor panel"):
		return
	if not require(not diagnostics.is_empty() and str(diagnostics[-1][0]).ends_with("extension.cpp"),
			"Compiler source location was not parsed"):
		return
	logs[0].emit_signal("meta_clicked", diagnostics[-1])
	await process_frame
	var text_editor = EditorInterface.get_script_editor().get_current_editor().get_base_editor()
	if not require(text_editor.get_text().contains("EGP deliberate diagnostic fixture")
			and text_editor.get_caret_line() == int(diagnostics[-1][1]) - 1, "Diagnostic navigation opened the wrong source line"):
		return
	# Fix the source through the actual editor; Build must save its unsaved changes.
	print("EGP_CPP_STAGE: save edited source and reload changed implementation")
	text_editor.set_text(source.replace("Hello from smoke!", "Hello after rebuild!"))
	if not require(panel.build_extension("smoke", false) == OK, "Rebuild did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() == 0, "Rebuild failed"):
		return
	node = ClassDB.instantiate("EGP_smoke_Node")
	if not require(node.get_message() == "Hello after rebuild!", "Reload kept the old C++ implementation"):
		return
	node.free()
	print("EGP_CPP_STAGE: release build and export mappings")
	if not require(panel.build_extension("smoke", true) == OK, "Release build did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() == 0, "Release build failed"):
		return
	var config := ConfigFile.new()
	if not require(config.load(descriptor) == OK and config.get_section_keys("libraries").size() == 2,
			"Debug/release export mappings were not preserved"):
		return
	print("EGP_CPP_EDITOR_SMOKE_PASSED")
	quit(0)
