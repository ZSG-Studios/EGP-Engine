extends SceneTree

# Run in a disposable project using: EGP --headless --editor --path <project> --script <this file>
# A compiler and xmake must be available. The bindings are extracted from the editor itself.
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

func wait_for_filesystem() -> void:
	var filesystem := EditorInterface.get_resource_filesystem()
	# Plugins initialize inside the first scan, and EditorProgress pumps nested
	# frames. A frame alone does not mean plugin/scan initialization has returned.
	while filesystem.is_scanning():
		await filesystem.filesystem_changed
	# Give sources_changed and deferred layout/resource callbacks their boundary.
	await process_frame
	while filesystem.is_scanning():
		await filesystem.filesystem_changed
	await process_frame

func finish_build() -> bool:
	var deadline := Time.get_ticks_msec() + 900000
	while panel.is_building():
		if Time.get_ticks_msec() > deadline:
			require(false, "Native extension build timed out")
			return false
		await process_frame
	await wait_for_filesystem()
	return true

func capture_ui(stage: String) -> void:
	var output_dir := OS.get_environment("EGP_CPP_UI_CAPTURE")
	if output_dir.is_empty():
		return
	# Disposable fixture only: embed the settings dialog in the hidden renderer
	# window, so capture never creates a focused native popup.
	var main_window := panel.get_tree().root
	main_window.gui_embed_subwindows = true
	main_window.size = Vector2i(1280, 1024)
	var child: Node = panel
	var settings_window: Window
	while child.get_parent() != null:
		var parent := child.get_parent()
		if parent is TabContainer and child is Control:
			parent.current_tab = parent.get_tab_idx_from_control(child)
		if parent is Window:
			settings_window = parent
			break
		child = parent
	if not require(settings_window != null, "Settings window for capture was not found"):
		return
	settings_window.force_native = false
	settings_window.popup_centered(Vector2i(1152, 900))
	await RenderingServer.frame_post_draw
	var image := main_window.get_texture().get_image()
	if not require(image != null and not image.is_empty(), "C++ panel screenshot was empty"):
		return
	if not require(image.save_png(output_dir.path_join(stage + ".png")) == OK, "C++ panel screenshot could not be saved"):
		return
	print("EGP_CPP_UI_CAPTURED: " + stage)

func finish_fixture() -> void:
	quit(0)

func verify_cache_recovery() -> bool:
	var isolated_root := OS.get_environment("EGP_CPP_UI_CACHE_ROOT").replace("\\", "/")
	if isolated_root.is_empty():
		print("EGP_CPP_CACHE_RECOVERY_SKIPPED: editor cache isolation unavailable")
		return true
	var cache_path := EditorInterface.get_editor_paths().get_cache_dir().replace("\\", "/")
	if not require(cache_path.begins_with(isolated_root + "/"), "Recovery fixture cache is not isolated"):
		return false
	var sdk_root := cache_path.path_join("egp_cpp/sdk")
	var folders := DirAccess.get_directories_at(sdk_root)
	if not require(folders.size() == 1, "Isolated SDK extraction was not found"):
		return false
	var sdk := sdk_root.path_join(folders[0])
	var marker_path := sdk.path_join(".complete")
	var expected_hash := FileAccess.get_file_as_string(marker_path)
	if not require(expected_hash.length() == 64, "SDK completion marker is not a SHA-256 identity"):
		return false
	var marker := FileAccess.open(marker_path, FileAccess.WRITE)
	marker.store_string("wrong SDK identity")
	marker.close()
	if not require(panel.create_extension("cache_marker") == OK, "SDK with a mismatched marker did not recover"):
		return false
	await wait_for_filesystem()
	if not require(FileAccess.get_file_as_string(marker_path) == expected_hash, "SDK completion identity was not restored"):
		return false
	var required_path := sdk.path_join("tools/run_project.lua")
	var original := FileAccess.get_file_as_string(required_path)
	if not require(DirAccess.remove_absolute(required_path) == OK, "Could not induce isolated incomplete SDK cache"):
		return false
	if not require(panel.create_extension("cache_required") == OK, "SDK with a missing required file did not recover"):
		return false
	await wait_for_filesystem()
	if not require(FileAccess.get_file_as_string(required_path) == original, "Required SDK file was not restored"):
		return false
	print("EGP_CPP_CACHE_RECOVERY_PASSED")
	return true

func run_test() -> void:
	await wait_for_filesystem()
	var panels := root.find_children("NativeExtensionEditor", "", true, false)
	if not require(panels.size() == 1, "Built-in C++ editor panel was not found"):
		return
	panel = panels[0]
	if OS.get_environment("EGP_CPP_UI_CACHE_ONLY") == "1":
		if not require(panel.create_extension("cache_initial") == OK, "Isolated cache fixture scaffolding failed"):
			return
		await wait_for_filesystem()
		if not await verify_cache_recovery():
			return
		print("EGP_CPP_CACHE_ONLY_PASSED")
		finish_fixture()
		return
	print("EGP_CPP_STAGE: empty and invalid-name workflow controls")
	var create_button := panel.find_child("CreateExtension", true, false) as Button
	var debug_button := panel.find_child("BuildDebug", true, false) as Button
	var release_button := panel.find_child("BuildRelease", true, false) as Button
	var name_input := panel.find_child("ExtensionName", true, false) as LineEdit
	if not require(create_button != null and debug_button != null and release_button != null and name_input != null,
			"Named C++ workflow controls were not found"):
		return
	if not require(create_button.disabled and debug_button.disabled and release_button.disabled,
			"Empty project should not offer invalid create/build actions"):
		return
	name_input.text = "Invalid Name"
	name_input.emit_signal("text_changed", name_input.text)
	if not require(create_button.disabled, "Invalid extension name was accepted by UI"):
		return
	name_input.text = "valid_name"
	name_input.emit_signal("text_changed", name_input.text)
	if not require(not create_button.disabled, "Valid extension name did not enable creation"):
		return
	await capture_ui("01-ready")
	print("EGP_CPP_STAGE: create and cold debug build")
	panel.diagnostic_found.connect(func(path: String, line: int): diagnostics.append([path, line]))
	if not require(panel.create_extension("smoke") == OK, "Extension scaffolding failed"):
		return
	await wait_for_filesystem()
	if not require(not debug_button.disabled and not release_button.disabled
			and panel.get_status().contains("Open Source"), "Created extension has no actionable next step"):
		return
	if not require(panel.build_extension("smoke", false) == OK, "Debug build did not start"):
		return
	if not require(debug_button.disabled and release_button.disabled and name_input.editable == false
			and panel.get_status().contains("Configuring smoke"), "Busy operation did not report stage or lock conflicting controls"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() == 0, "Debug build failed"):
		return
	if not require(panel.get_status().contains("built and loaded") and not debug_button.disabled,
			"Successful build did not restore controls and explain load result"):
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
	await wait_for_filesystem()
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
	var cache_output := panel.find_children("*", "RichTextLabel", true, false)
	if not require(cache_output.size() == 1
			and not cache_output[0].get_parsed_text().contains("<godot-cpp> compiling"),
			"A second native project rebuilt the shared SDK instead of using its archive"):
		return
	var descriptor := "res://extensions/smoke/smoke.gdextension"
	print("EGP_CPP_STAGE: deliberate compile failure and diagnostic navigation")
	var before := FileAccess.get_file_as_string(descriptor)
	var source_path := "res://extensions/smoke/src/extension.cpp"
	var source := FileAccess.get_file_as_string(source_path)
	var file := FileAccess.open(source_path, FileAccess.WRITE)
	file.store_string(source + "\n#error EGP deliberate diagnostic fixture\n")
	file.close()
	# Emit actual terminal controls through the build pipe. Clang quotes controls
	# in pragma diagnostics, xmake trims earlier warnings, and MSVC stops at #error.
	var recipe_path := "res://extensions/smoke/xmake.lua"
	var recipe := FileAccess.get_file_as_string(recipe_path)
	file = FileAccess.open(recipe_path, FileAccess.WRITE)
	file.store_string(recipe + "\ntarget(\"extension\")\n    before_build(function ()\n        io.stdout:write(string.char(27) .. \"[31m\" .. string.char(27) .. \"]8;;https://example.invalid\" .. string.char(7) .. \"EGP terminal link fixture\" .. string.char(27) .. \"]8;;\" .. string.char(7) .. string.char(27) .. \"[0m\\n\")\n        io.stdout:flush()\n    end)\n")
	file.close()
	var failure_build_started: bool = panel.build_extension("smoke", false) == OK
	var failure_selection_updated: bool = selector.get_item_text(selector.get_selected()) == "smoke"
	var failure_build_finished := false
	if failure_build_started:
		failure_build_finished = await finish_build()
	# Restore exact project input before any recovery build or implementation reload.
	file = FileAccess.open(recipe_path, FileAccess.WRITE)
	file.store_string(recipe)
	file.close()
	if not require(FileAccess.get_file_as_string(recipe_path) == recipe, "Diagnostic fixture did not restore its build recipe"):
		return
	if not require(failure_build_started, "Failed-build fixture did not start"):
		return
	if not require(failure_selection_updated,
			"Public build target did not update the selected extension/source path"):
		return
	if not failure_build_finished:
		return
	if not require(panel.get_status().contains("build failed") and panel.get_status().contains("fix the source")
			and not debug_button.disabled, "Compile failure lacks persistent recovery guidance"):
		return
	await capture_ui("02-compile-error")
	if not require(panel.get_last_build_result() != 0, "Invalid C++ unexpectedly compiled"):
		return
	if not require(FileAccess.get_file_as_string(descriptor) == before, "Failed build changed the published descriptor"):
		return
	var logs := panel.find_children("*", "RichTextLabel", true, false)
	if not require(logs.size() == 1 and logs[0].get_parsed_text().contains("EGP deliberate diagnostic fixture"),
			"Compiler diagnostics were not captured by the editor panel"):
		return
	var compiler_output: String = logs[0].get_parsed_text()
	if not require(not compiler_output.contains(String.chr(27)) and not compiler_output.contains("[0m")
			and not compiler_output.contains("]8;;https://example.invalid")
			and compiler_output.contains("EGP terminal link fixture"), "Terminal escapes obscured compiler diagnostics"):
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
	print("EGP_CPP_STAGE: descriptor publication failure preserves previous build")
	var previous_descriptor := FileAccess.get_file_as_string(descriptor)
	var pending_path := descriptor + ".pending-" + str(OS.get_process_id())
	if not require(DirAccess.make_dir_absolute(pending_path) == OK, "Could not induce a descriptor publication conflict"):
		return
	if not require(panel.build_extension("smoke", false) == OK, "Descriptor conflict build did not start"):
		return
	if not await finish_build():
		return
	if not require(panel.get_last_build_result() != 0 and panel.get_status().contains("Descriptor transaction")
			and panel.get_status().contains("previous descriptor is unchanged"), "Publication error lost its precise recovery guidance"):
		return
	if not require(FileAccess.get_file_as_string(descriptor) == previous_descriptor, "Publication failure changed the last working descriptor"):
		return
	node = ClassDB.instantiate("EGP_smoke_Node")
	if not require(node.get_message() == "Hello after rebuild!", "Publication failure changed the loaded working extension"):
		return
	node.free()
	if not require(DirAccess.remove_absolute(pending_path) == OK, "Could not remove fixture transaction conflict"):
		return
	print("EGP_CPP_DESCRIPTOR_FAILURE_PASSED")
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
	await capture_ui("03-release-ready")
	print("EGP_CPP_EDITOR_SMOKE_PASSED")
	finish_fixture()
