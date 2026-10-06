/**************************************************************************/
/*  native_extension_editor.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "native_extension_editor.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/extension/gdextension_manager.h"
#include "core/io/compression.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/regex.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/run/editor_run_bar.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/settings/gdextension/native_extension_sdk.gen.h"
#include "editor/settings/project_settings_editor.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/text_file.h"

void NativeExtensionEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_extension", "name"), &NativeExtensionEditor::create_extension);
	ClassDB::bind_method(D_METHOD("build_extension", "name", "release"), &NativeExtensionEditor::build_extension, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("is_building"), &NativeExtensionEditor::is_building);
	ClassDB::bind_method(D_METHOD("get_last_build_result"), &NativeExtensionEditor::get_last_build_result);
	ClassDB::bind_method(D_METHOD("check_toolchain"), &NativeExtensionEditor::check_toolchain);
	ClassDB::bind_method(D_METHOD("install_tools"), &NativeExtensionEditor::install_tools);
	ADD_SIGNAL(MethodInfo("diagnostic_found", PropertyInfo(Variant::STRING, "path"), PropertyInfo(Variant::INT, "line")));
}

String NativeExtensionEditor::_platform() const {
	const String name = OS::get_singleton()->get_name();
	return name == "Windows" ? "windows" : (name == "macOS" ? "macos" : "linux");
}

String NativeExtensionEditor::_suffix() const {
	return _platform() == "windows" ? ".dll" : (_platform() == "macos" ? ".dylib" : ".so");
}

Error NativeExtensionEditor::_prepare_sdk() {
	sdk_path = EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/sdk").path_join(String(egp_cpp_sdk_hash).left(16));
	if (FileAccess::exists(sdk_path.path_join(".complete"))) {
		return OK;
	}
	Vector<uint8_t> archive;
	archive.resize(egp_cpp_sdk_size);
	ERR_FAIL_COND_V(Compression::decompress(archive.ptrw(), archive.size(), egp_cpp_sdk_data,
							sizeof(egp_cpp_sdk_data), Compression::MODE_DEFLATE) != archive.size(),
			ERR_FILE_CORRUPT);
	int64_t offset = 0;
	while (offset < archive.size()) {
		ERR_FAIL_COND_V(offset + 8 > archive.size(), ERR_FILE_CORRUPT);
		const uint32_t name_size = decode_uint32(archive.ptr() + offset);
		const uint32_t data_size = decode_uint32(archive.ptr() + offset + 4);
		offset += 8;
		ERR_FAIL_COND_V(offset + name_size + data_size > archive.size(), ERR_FILE_CORRUPT);
		const String name = String::utf8((const char *)archive.ptr() + offset, name_size);
		offset += name_size;
		ERR_FAIL_COND_V(name.is_absolute_path() || name.contains(".."), ERR_FILE_CORRUPT);
		const String path = sdk_path.path_join(name);
		Error error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		ERR_FAIL_COND_V(error != OK, error);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &error);
		ERR_FAIL_COND_V(file.is_null(), error);
		ERR_FAIL_COND_V(!file->store_buffer(archive.ptr() + offset, data_size), ERR_FILE_CANT_WRITE);
		offset += data_size;
	}
	Ref<FileAccess> marker = FileAccess::open(sdk_path.path_join(".complete"), FileAccess::WRITE);
	ERR_FAIL_COND_V(marker.is_null(), ERR_FILE_CANT_WRITE);
	marker->store_string(egp_cpp_sdk_hash);
	return OK;
}

Error NativeExtensionEditor::create_extension(const String &p_name) {
	ERR_FAIL_COND_V(process_id != 0, ERR_BUSY);
	ERR_FAIL_COND_V(!p_name.is_valid_identifier() || p_name.length() > 64 || p_name != p_name.to_lower(), ERR_INVALID_PARAMETER);
	const String path = "res://extensions/" + p_name;
	ERR_FAIL_COND_V(DirAccess::dir_exists_absolute(path), ERR_ALREADY_EXISTS);
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	error = DirAccess::make_dir_recursive_absolute(path.path_join("src"));
	ERR_FAIL_COND_V(error != OK, error);
	const char *templates[] = { "CMakeLists.txt.in", "extension.cpp.in", "extension.gdextension.in" };
	const String targets[] = { "CMakeLists.txt", "src/extension.cpp", p_name + ".gdextension.in" };
	for (int i = 0; i < 3; i++) {
		Ref<FileAccess> source = FileAccess::open(sdk_path.path_join("templates").path_join(templates[i]), FileAccess::READ, &error);
		ERR_FAIL_COND_V(source.is_null(), error);
		String text = source->get_as_text().replace("@NAME@", p_name).replace("@CLASS@", "EGP_" + p_name + "_Node").replace("@PLATFORM@", _platform()).replace("@ARCH@", Engine::get_singleton()->get_architecture_name()).replace("@SUFFIX@", _suffix());
		Ref<FileAccess> target = FileAccess::open(path.path_join(targets[i]), FileAccess::WRITE, &error);
		ERR_FAIL_COND_V(target.is_null(), error);
		ERR_FAIL_COND_V(!target->store_string(text), ERR_FILE_CANT_WRITE);
	}
	Ref<FileAccess> ignore = FileAccess::open(path.path_join(".gitignore"), FileAccess::WRITE);
	if (ignore.is_valid()) {
		ignore->store_string("bin/\n");
	}
	_refresh_extensions();
	for (int i = 0; i < extensions->get_item_count(); i++) {
		if (extensions->get_item_text(i) == p_name) {
			extensions->select(i);
			break;
		}
	}
	EditorFileSystem::get_singleton()->scan();
	_append_line(vformat(TTR("Created %s. Build Debug to make its node available in the editor."), path));
	return OK;
}

void NativeExtensionEditor::_refresh_extensions() {
	String selected = extensions->get_selected() >= 0 ? extensions->get_item_text(extensions->get_selected()) : String();
	extensions->clear();
	Ref<DirAccess> directory = DirAccess::open("res://extensions");
	if (directory.is_null()) {
		return;
	}
	directory->list_dir_begin();
	for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
		if (directory->current_is_dir() && name.is_valid_identifier() && FileAccess::exists("res://extensions/" + name + "/" + name + ".gdextension.in")) {
			extensions->add_item(name);
			if (name == selected) {
				extensions->select(extensions->get_item_count() - 1);
			}
		}
	}
	directory->list_dir_end();
}

void NativeExtensionEditor::_create_pressed() {
	const Error error = create_extension(extension_name->get_text().strip_edges());
	if (error != OK) {
		_append_line(vformat(TTR("Could not create extension (error %d). Use a new lowercase C++ identifier."), error));
	}
}

void NativeExtensionEditor::_build_pressed(bool p_release) {
	if (extensions->get_selected() < 0) {
		_append_line(TTR("Create an extension first."));
		return;
	}
	const Error error = build_extension(extensions->get_item_text(extensions->get_selected()), p_release);
	if (error != OK) {
		_append_line(vformat(TTR("Could not start build (error %d). Stop the running game and check the CMake/toolchain path."), error));
	}
}

Error NativeExtensionEditor::build_extension(const String &p_name, bool p_release) {
	ERR_FAIL_COND_V(process_id != 0 || (EditorRunBar::get_singleton() && EditorRunBar::get_singleton()->is_playing()), ERR_BUSY);
	ERR_FAIL_COND_V(!p_name.is_valid_identifier() || !FileAccess::exists("res://extensions/" + p_name + "/CMakeLists.txt"), ERR_INVALID_PARAMETER);
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	operation = BUILD;
	cmake_executable = _find_cmake();
	process_executable = cmake_executable;
	ScriptEditor::get_singleton()->save_all_scripts();
	if (cmake_executable.is_empty()) {
		cmake_executable = "cmake";
	}
	EditorSettings::get_singleton()->set_setting("native_extensions/cmake_path", cmake_executable);
	building_name = p_name;
	build_config = p_release ? "Release" : "Debug";
	const String project_key = (ProjectSettings::get_singleton()->globalize_path("res://") + "/" + p_name).sha256_text().left(16);
	build_path = EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/build").path_join(project_key).path_join(build_config.to_lower()).path_join(String(egp_cpp_sdk_hash).left(16));
	output->clear();
	last_build_result = -1;
	restart_button->hide();
	configuring = true;
	List<String> arguments;
	arguments.push_back("-S");
	arguments.push_back(ProjectSettings::get_singleton()->globalize_path("res://extensions/" + p_name));
	arguments.push_back("-B");
	arguments.push_back(build_path);
	arguments.push_back("-DEGP_CPP_SDK=" + sdk_path);
	arguments.push_back("-DEGP_CPP_CACHE=" + EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/lib").path_join(String(egp_cpp_sdk_hash).left(16)));
	arguments.push_back("-DCMAKE_BUILD_TYPE=" + build_config);
#ifdef WINDOWS_ENABLED
	arguments.push_back("-A");
	arguments.push_back(Engine::get_singleton()->get_architecture_name() == "arm64" ? "ARM64" : (sizeof(void *) == 8 ? "x64" : "Win32"));
#endif
	return _start_process(arguments) ? OK : ERR_CANT_FORK;
}

bool NativeExtensionEditor::_start_process(const List<String> &p_arguments) {
	Dictionary process = OS::get_singleton()->execute_with_pipe(process_executable, p_arguments, false);
	if (!process.has("pid")) {
		last_build_result = -2;
		_set_busy(false);
		return false;
	}
	process_id = process["pid"];
	pipes[0] = process["stdio"];
	pipes[1] = process["stderr"];
	_set_busy(true);
	set_process(true);
	return true;
}

void NativeExtensionEditor::_set_busy(bool p_busy) {
	check_button->set_disabled(p_busy);
	install_button->set_disabled(p_busy);
	create_button->set_disabled(p_busy);
	debug_button->set_disabled(p_busy);
	release_button->set_disabled(p_busy);
	extensions->set_disabled(p_busy);
	cmake_path->set_editable(!p_busy);
}

void NativeExtensionEditor::_append_line(const String &p_line) {
	if (cli) {
		print_line(p_line);
	}
	RegEx diagnostic;
	diagnostic.compile("^(.+\\.(?:cpp|hpp|h|c))(?:(?:\\((\\d+)(?:,\\d+)?\\))|(?::(\\d+)(?::\\d+)?))\\s*:");
	Ref<RegExMatch> match = diagnostic.search(p_line);
	if (match.is_valid()) {
		Array location;
		location.push_back(match->get_string(1).strip_edges());
		location.push_back((match->get_string(2).is_empty() ? match->get_string(3) : match->get_string(2)).to_int());
		emit_signal(SNAME("diagnostic_found"), location[0], location[1]);
		output->push_meta(location);
		output->add_text(p_line);
		output->pop();
	} else {
		output->add_text(p_line);
	}
	output->add_text("\n");
}

void NativeExtensionEditor::_drain_pipe(int p_index, bool p_final) {
	if (pipes[p_index].is_null()) {
		return;
	}
	// Check availability first: a quiet compiler must never block the editor thread.
	for (int pass = 0; pass < 8; pass++) {
		int64_t available = pipes[p_index]->get_length();
		if (available <= 0) {
			break;
		}
		Vector<uint8_t> bytes = pipes[p_index]->get_buffer(MIN(available, int64_t(8192)));
		pending_output[p_index].append_array(bytes);
	}
	Vector<uint8_t> &pending = pending_output[p_index];
	int start = 0;
	for (int i = 0; i < pending.size(); i++) {
		if (pending[i] == '\n') {
			_append_line(String::utf8((const char *)pending.ptr() + start, i - start).trim_suffix("\r"));
			start = i + 1;
		}
	}
	if (p_final && start < pending.size()) {
		_append_line(String::utf8((const char *)pending.ptr() + start, pending.size() - start));
		start = pending.size();
	}
	if (start > 0) {
		pending = pending.slice(start);
	}
}

Error NativeExtensionEditor::_publish_library() {
	const String base = building_name + "." + _platform() + "." + build_config.to_lower() + "." + Engine::get_singleton()->get_architecture_name();
	const String library = "res://extensions/" + building_name + "/bin/" + base + _suffix();
	ERR_FAIL_COND_V(!FileAccess::exists(library), ERR_FILE_NOT_FOUND);
	// Publish immutable copies so a loaded Windows DLL never blocks the next build.
	const String published = library.trim_suffix(_suffix()) + "_" + FileAccess::get_sha256(library).left(16) + _suffix();
	Ref<DirAccess> directory = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	Error error = OK;
	if (!FileAccess::exists(published)) {
		error = directory->copy(library, published);
		ERR_FAIL_COND_V(error != OK, error);
	}
	const String descriptor = "res://extensions/" + building_name + "/" + building_name + ".gdextension";
	Ref<ConfigFile> config;
	config.instantiate();
	error = config->load(FileAccess::exists(descriptor) ? descriptor : descriptor + ".in");
	ERR_FAIL_COND_V(error != OK, error);
	config->set_value("libraries", _platform() + "." + build_config.to_lower() + "." + Engine::get_singleton()->get_architecture_name(), published);
	error = config->save(descriptor);
	ERR_FAIL_COND_V(error != OK, error);
	EditorFileSystem::get_singleton()->scan();
	if (build_config == "Debug") {
		GDExtensionManager *manager = GDExtensionManager::get_singleton();
		GDExtensionManager::LoadStatus status = manager->is_extension_loaded(descriptor) ? manager->reload_extension(descriptor) : manager->load_extension(descriptor);
		if (status != GDExtensionManager::LOAD_STATUS_OK && status != GDExtensionManager::LOAD_STATUS_ALREADY_LOADED) {
			restart_button->show();
			_append_line(TTR("Library built. Restart the editor to finish loading the extension."));
		} else {
			_append_line(TTR("Library built and loaded into the editor."));
		}
	} else {
		_append_line(TTR("Release library built and registered for export."));
	}
	return OK;
}

void NativeExtensionEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		callable_mp(this, &NativeExtensionEditor::_run_cli).call_deferred();
	}
	if (p_what == NOTIFICATION_READY || p_what == NOTIFICATION_WM_WINDOW_FOCUS_IN) {
		if (process_id == 0) {
			_refresh_extensions();
		}
	}
	if (p_what != NOTIFICATION_PROCESS) {
		return;
	}
	if (process_id == 0) {
		if (cli) {
			_next_cli();
		}
		return;
	}
	_drain_pipe(0);
	_drain_pipe(1);
	if (OS::get_singleton()->is_process_running(process_id)) {
		return;
	}
	const int result = OS::get_singleton()->get_process_exit_code(process_id);
	_drain_pipe(0, true);
	_drain_pipe(1, true);
	pipes[0].unref();
	pipes[1].unref();
	process_id = 0;
	if (result == 0 && configuring) {
		configuring = false;
		List<String> arguments;
		arguments.push_back("--build");
		arguments.push_back(build_path);
		arguments.push_back("--config");
		arguments.push_back(build_config);
		arguments.push_back("--parallel");
		arguments.push_back(itos(MAX(1, OS::get_singleton()->get_processor_count() - 1)));
		if (!_start_process(arguments)) {
			_complete_operation();
		}
		return;
	}
	last_build_result = result;
	if (result == 0 && operation == INSTALL) {
		if (check_toolchain() != OK) {
			last_build_result = -2;
			_complete_operation();
		}
		return;
	}
	if (result == 0 && operation == CHECK) {
		_append_line("EGP_CPP_TOOLCHAIN_READY: CMake, C++17 compiler and linker verified.");
	} else if (result == 0) {
		const Error error = _publish_library();
		if (error != OK) {
			last_build_result = -3;
			_append_line(vformat(TTR("Build completed, but publishing failed (error %d)."), error));
		}
	} else {
		_append_line(vformat(TTR("Build failed (exit code %d). The previous published library is unchanged."), result));
	}
	_complete_operation();
}

String NativeExtensionEditor::_find_cmake() const {
	const String configured = cmake_path->get_text().strip_edges();
	if (!configured.is_empty() && configured != "cmake") {
		return configured;
	}
#ifdef WINDOWS_ENABLED
	const String installed = OS::get_singleton()->get_environment("ProgramFiles").path_join("CMake/bin/cmake.exe");
	if (FileAccess::exists(installed)) {
		return installed;
	}
#elif defined(MACOS_ENABLED)
	if (FileAccess::exists("/Applications/CMake.app/Contents/bin/cmake")) {
		return "/Applications/CMake.app/Contents/bin/cmake";
	}
	if (FileAccess::exists("/opt/homebrew/bin/cmake")) {
		return "/opt/homebrew/bin/cmake";
	}
#endif
	return "cmake";
}

Error NativeExtensionEditor::check_toolchain() {
	ERR_FAIL_COND_V(process_id != 0, ERR_BUSY);
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	operation = CHECK;
	configuring = true;
	last_build_result = -1;
	build_config = "Debug";
	process_executable = _find_cmake();
	cmake_executable = process_executable;
	build_path = EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/toolchain").path_join(String(egp_cpp_sdk_hash).left(16));
	output->clear();
	List<String> arguments;
	arguments.push_back("-S");
	arguments.push_back(sdk_path.path_join("tools"));
	arguments.push_back("-B");
	arguments.push_back(build_path);
	arguments.push_back("-DCMAKE_BUILD_TYPE=Debug");
#ifdef WINDOWS_ENABLED
	arguments.push_back("-A");
	arguments.push_back(Engine::get_singleton()->get_architecture_name() == "arm64" ? "ARM64" : (sizeof(void *) == 8 ? "x64" : "Win32"));
#endif
	return _start_process(arguments) ? OK : ERR_CANT_FORK;
}

Error NativeExtensionEditor::install_tools() {
	ERR_FAIL_COND_V(process_id != 0, ERR_BUSY);
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	operation = INSTALL;
	configuring = false;
	last_build_result = -1;
	output->clear();
	List<String> arguments;
#ifdef WINDOWS_ENABLED
	process_executable = OS::get_singleton()->get_environment("SystemRoot").path_join("System32/WindowsPowerShell/v1.0/powershell.exe");
	arguments.push_back("-NoProfile");
	arguments.push_back("-ExecutionPolicy");
	arguments.push_back("Bypass");
	arguments.push_back("-File");
	arguments.push_back(sdk_path.path_join("tools/setup.ps1"));
#else
	process_executable = "/bin/sh";
	arguments.push_back(sdk_path.path_join("tools/setup.sh"));
#endif
	_append_line(TTR("Installing missing CMake/compiler tools. Complete any system installer or administrator prompts."));
	return _start_process(arguments) ? OK : ERR_CANT_FORK;
}

void NativeExtensionEditor::_complete_operation() {
	_set_busy(false);
	set_process(cli);
	if (cli) {
		_next_cli();
	}
}

void NativeExtensionEditor::_run_cli() {
	for (const String &argument : OS::get_singleton()->get_cmdline_user_args()) {
		if (argument.begins_with("--cpp-")) {
			cli = true;
			cli_commands.push_back(argument);
		}
	}
	if (cli) {
		last_build_result = 0;
		set_process(true);
	}
}

void NativeExtensionEditor::_next_cli() {
	if (last_build_result != 0) {
		SceneTree::get_singleton()->quit(1);
		set_process(false);
		return;
	}
	if (EditorFileSystem::get_singleton()->is_scanning()) {
		return;
	}
	while (cli_index < cli_commands.size()) {
		const String command = cli_commands[cli_index++];
		Error error = OK;
		if (command == "--cpp-help") {
			print_line("EGP --headless --editor --path PROJECT -- [--cpp-cmake=PATH] [--cpp-install] [--cpp-check] [--cpp-create=NAME] [--cpp-build=NAME:debug|release]");
		} else if (command.begins_with("--cpp-cmake=")) {
			cmake_path->set_text(command.trim_prefix("--cpp-cmake="));
		} else if (command == "--cpp-check") {
			error = check_toolchain();
		} else if (command == "--cpp-install") {
			error = install_tools();
		} else if (command.begins_with("--cpp-create=")) {
			error = create_extension(command.trim_prefix("--cpp-create="));
		} else if (command.begins_with("--cpp-build=")) {
			const PackedStringArray parts = command.trim_prefix("--cpp-build=").split(":");
			if (parts.size() != 2 || (parts[1] != "debug" && parts[1] != "release")) {
				error = ERR_INVALID_PARAMETER;
			} else {
				error = build_extension(parts[0], parts[1] == "release");
			}
		} else {
			error = ERR_INVALID_PARAMETER;
		}
		if (error != OK) {
			print_error(vformat("EGP C++ command failed: %s (error %d)", command, error));
			last_build_result = -1;
			_complete_operation();
			return;
		}
		if (process_id != 0 || EditorFileSystem::get_singleton()->is_scanning()) {
			return;
		}
	}
	print_line("EGP_CPP_CLI_PASSED");
	SceneTree::get_singleton()->quit(0);
	set_process(false);
}

void NativeExtensionEditor::_diagnostic_clicked(const Variant &p_meta) {
	Array location = p_meta;
	if (location.size() != 2) {
		return;
	}
	String path = location[0];
	if (!path.is_absolute_path()) {
		path = ProjectSettings::get_singleton()->globalize_path("res://extensions/" + building_name).path_join(path);
	}
	Ref<TextFile> file;
	path = ProjectSettings::get_singleton()->localize_path(path);
	file = ResourceCache::get_ref(path);
	if (file.is_null()) {
		file.instantiate();
		if (file->load_text(path) != OK) {
			return;
		}
		file->set_file_path(path);
		file->set_path(path);
	}
	ProjectSettingsEditor::get_singleton()->hide();
	ScriptEditor::get_singleton()->edit(file, MAX(0, int(location[1]) - 1), 0);
}

void NativeExtensionEditor::_restart_pressed() {
	EditorNode::get_singleton()->save_all_scenes();
	EditorNode::get_singleton()->restart_editor();
}

NativeExtensionEditor::NativeExtensionEditor() {
	set_name("NativeExtensionEditor");
	Label *title = memnew(Label(TTRC("C++ Extensions")));
	title->set_theme_type_variation("HeaderSmall");
	add_child(title);
	Label *description = memnew(Label(TTRC("Create and build extensions with EGP's bundled C++ SDK.")));
	add_child(description);
	HBoxContainer *create_row = memnew(HBoxContainer);
	add_child(create_row);
	extension_name = memnew(LineEdit);
	extension_name->set_placeholder("my_extension");
	extension_name->set_h_size_flags(SIZE_EXPAND_FILL);
	create_row->add_child(extension_name);
	create_button = memnew(Button(TTRC("Create Extension")));
	create_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_create_pressed));
	create_row->add_child(create_button);
	HBoxContainer *build_row = memnew(HBoxContainer);
	add_child(build_row);
	extensions = memnew(OptionButton);
	extensions->set_h_size_flags(SIZE_EXPAND_FILL);
	build_row->add_child(extensions);
	debug_button = memnew(Button(TTRC("Build Debug")));
	debug_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_build_pressed).bind(false));
	build_row->add_child(debug_button);
	release_button = memnew(Button(TTRC("Build Release")));
	release_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_build_pressed).bind(true));
	build_row->add_child(release_button);
	HBoxContainer *tool_row = memnew(HBoxContainer);
	add_child(tool_row);
	tool_row->add_child(memnew(Label(TTRC("CMake executable:"))));
	cmake_path = memnew(LineEdit);
	cmake_path->set_h_size_flags(SIZE_EXPAND_FILL);
	cmake_path->set_text(EDITOR_DEF("native_extensions/cmake_path", "cmake"));
	tool_row->add_child(cmake_path);
	check_button = memnew(Button(TTRC("Check Toolchain")));
	check_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::check_toolchain));
	tool_row->add_child(check_button);
	install_button = memnew(Button(TTRC("Install Tools")));
	install_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::install_tools));
	tool_row->add_child(install_button);
	output = memnew(RichTextLabel);
	output->set_custom_minimum_size(Size2(0, 150 * EDSCALE));
	output->set_scroll_follow(true);
	output->set_selection_enabled(true);
	output->connect("meta_clicked", callable_mp(this, &NativeExtensionEditor::_diagnostic_clicked));
	add_child(output);
	restart_button = memnew(Button(TTRC("Save Scenes and Restart Editor")));
	restart_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_restart_pressed));
	restart_button->hide();
	add_child(restart_button);
	set_process(false);
}

NativeExtensionEditor::~NativeExtensionEditor() {
	if (process_id != 0) {
#ifdef WINDOWS_ENABLED
		// CMake launches MSBuild/compiler children. Terminate this build's tree on editor shutdown.
		const String taskkill = OS::get_singleton()->get_environment("SystemRoot").path_join("System32/taskkill.exe");
		if (FileAccess::exists(taskkill)) {
			List<String> arguments;
			arguments.push_back("/PID");
			arguments.push_back(itos(process_id));
			arguments.push_back("/T");
			arguments.push_back("/F");
			String ignored;
			OS::get_singleton()->execute(taskkill, arguments, &ignored);
		}
#endif
		OS::get_singleton()->kill(process_id);
	}
}
