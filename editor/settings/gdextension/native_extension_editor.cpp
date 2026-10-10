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
#include "editor/debugger/editor_debugger_node.h"
#include "editor/doc/editor_help.h"
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
#include "servers/display/display_server.h"

void NativeExtensionEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create_extension", "name"), &NativeExtensionEditor::create_extension);
	ClassDB::bind_method(D_METHOD("build_extension", "name", "release"), &NativeExtensionEditor::build_extension, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("is_building"), &NativeExtensionEditor::is_building);
	ClassDB::bind_method(D_METHOD("get_last_build_result"), &NativeExtensionEditor::get_last_build_result);
	ClassDB::bind_method(D_METHOD("get_status"), &NativeExtensionEditor::get_status);
	ClassDB::bind_method(D_METHOD("check_toolchain"), &NativeExtensionEditor::check_toolchain);
	ClassDB::bind_method(D_METHOD("install_tools"), &NativeExtensionEditor::install_tools);
	ADD_SIGNAL(MethodInfo("diagnostic_found", PropertyInfo(Variant::STRING, "path"), PropertyInfo(Variant::INT, "line")));
}

String NativeExtensionEditor::get_status() const {
	return status_label->get_text();
}

// EGP builds with Clang only: clang-cl (MSVC STL/CRT and Windows SDK as headers and
// libraries) on Windows, clang on Linux and Xcode's Apple clang on macOS. Without an
// explicit toolchain xmake would pick MSVC on Windows and GCC on Linux.
void NativeExtensionEditor::_append_toolchain(List<String> &r_arguments) const {
	const String platform = _platform();
	if (platform == "windows") {
		r_arguments.push_back("--toolchain=clang-cl");
	} else if (platform == "linux") {
		r_arguments.push_back("--toolchain=clang");
	}
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
	const String marker_path = sdk_path.path_join(".complete");
	bool complete = FileAccess::exists(marker_path) && FileAccess::get_file_as_string(marker_path) == egp_cpp_sdk_hash;
	const char *required_files[] = { "sdk.json", "xmake.lua", "gen/include/gdextension_interface.h", "gen/include/godot_cpp/classes/node.hpp", "tools/run_project.lua", "tools/xmake.lua", "templates/xmake.lua.in", "templates/extension.cpp.in", "templates/extension.gdextension.in" };
	for (const char *required : required_files) {
		complete = complete && FileAccess::exists(sdk_path.path_join(required));
	}
	if (complete) {
		return OK;
	}
	// A partial extraction must never inherit a stale successful completion marker.
	if (FileAccess::exists(marker_path)) {
		const Error remove_error = DirAccess::remove_absolute(marker_path);
		ERR_FAIL_COND_V(remove_error != OK, remove_error);
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
	Ref<FileAccess> marker = FileAccess::open(marker_path, FileAccess::WRITE);
	ERR_FAIL_COND_V(marker.is_null(), ERR_FILE_CANT_WRITE);
	ERR_FAIL_COND_V(!marker->store_string(egp_cpp_sdk_hash), ERR_FILE_CANT_WRITE);
	marker->flush();
	ERR_FAIL_COND_V(marker->get_error() != OK, ERR_FILE_CANT_WRITE);
	return OK;
}

Error NativeExtensionEditor::create_extension(const String &p_name) {
	if (process_id != 0) {
		_set_status(TTR("Another C++ operation is running. Wait for it to finish before starting another."), true);
		return ERR_BUSY;
	}
	if (!p_name.is_valid_identifier() || p_name.length() > 64 || p_name != p_name.to_lower()) {
		_set_status(TTR("Use a lowercase C++ identifier up to 64 characters, such as player_movement."), true);
		return ERR_INVALID_PARAMETER;
	}
	const String path = "res://extensions/" + p_name;
	if (DirAccess::dir_exists_absolute(path)) {
		_set_status(TTR("That extension already exists. Select it below, or choose a new name."), true);
		return ERR_ALREADY_EXISTS;
	}
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	error = DirAccess::make_dir_recursive_absolute(path.path_join("src"));
	ERR_FAIL_COND_V(error != OK, error);
	const char *templates[] = { "xmake.lua.in", "extension.cpp.in", "extension.gdextension.in" };
	const String targets[] = { "xmake.lua", "src/extension.cpp", p_name + ".gdextension.in" };
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
	_update_controls();
	_set_status(vformat(TTR("Created %s. Open Source, then Build Debug to load your node."), p_name));
	_scan_filesystem();
	_append_line(vformat(TTR("Created %s. Build Debug to make its node available in the editor."), path));
	return OK;
}

void NativeExtensionEditor::_refresh_extensions() {
	String selected = extensions->get_selected() >= 0 ? extensions->get_item_text(extensions->get_selected()) : String();
	extensions->clear();
	Ref<DirAccess> directory = DirAccess::open("res://extensions");
	if (directory.is_null()) {
		_update_controls();
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
	_update_controls();
}

void NativeExtensionEditor::_create_pressed() {
	const Error error = create_extension(extension_name->get_text().strip_edges());
	if (error != OK) {
		_set_status(error == ERR_ALREADY_EXISTS ? TTR("That extension already exists. Select it below, or choose a new name.") : vformat(TTR("Could not create extension (error %d). Use a new lowercase C++ identifier and check project write permissions."), error), true);
	}
}

void NativeExtensionEditor::_build_pressed(bool p_release) {
	if (extensions->get_selected() < 0) {
		_set_status(TTR("Create an extension first, or refresh the list after adding an extension to res://extensions."), true);
		return;
	}
	const Error error = build_extension(extensions->get_item_text(extensions->get_selected()), p_release);
	if (error != OK) {
		_set_status(vformat(TTR("Could not start build (error %d). Stop the game, check the xmake path with Check Toolchain, and review output below."), error), true);
	}
}

Error NativeExtensionEditor::build_extension(const String &p_name, bool p_release) {
	ERR_FAIL_COND_V(process_id != 0, ERR_BUSY);
	if (EditorRunBar::get_singleton() && EditorRunBar::get_singleton()->is_playing() && (p_release || !bool(GLOBAL_GET("debug/hot_reload/enable_runtime")))) {
		_set_status(TTR("Stop the game before rebuilding. Running-game reload requires Debug and debug/hot_reload/enable_runtime enabled before launch."), true);
		return ERR_BUSY;
	}
	if (!p_name.is_valid_identifier() || !FileAccess::exists("res://extensions/" + p_name + "/xmake.lua")) {
		_set_status(vformat(TTR("Cannot build %s: its xmake.lua was not found. Create the extension or refresh the list after restoring its files."), p_name), true);
		return ERR_INVALID_PARAMETER;
	}
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	operation = BUILD;
	xmake_executable = _find_xmake();
	process_executable = xmake_executable;
	ScriptEditor::get_singleton()->save_all_scripts();
	if (xmake_executable.is_empty()) {
		xmake_executable = "xmake";
	}
	EditorSettings::get_singleton()->set_setting("native_extensions/xmake_path", xmake_executable);
	building_name = p_name;
	// Public/CLI builds must identify the same target as the selector and source link.
	for (int i = 0; i < extensions->get_item_count(); i++) {
		if (extensions->get_item_text(i) == p_name) {
			extensions->select(i);
			break;
		}
	}
	_update_controls();
	build_config = p_release ? "Release" : "Debug";
	const String project_key = (ProjectSettings::get_singleton()->globalize_path("res://") + "/" + p_name).sha256_text().left(16);
	build_path = EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/build").path_join(project_key).path_join(build_config.to_lower()).path_join(String(egp_cpp_sdk_hash).left(16));
	output->clear();
	_set_status(vformat(TTR("Configuring %s (%s). The first build compiles the bundled SDK; later builds reuse it."), p_name, build_config));
	last_build_result = -1;
	restart_button->hide();
	configuring = true;
	List<String> arguments;
	build_project_path = ProjectSettings::get_singleton()->globalize_path("res://extensions/" + p_name);
	arguments.push_back("f");
	arguments.push_back("-y");
	arguments.push_back("-P");
	arguments.push_back(build_project_path);
	arguments.push_back("-o");
	arguments.push_back(build_path);
	arguments.push_back("-p");
	arguments.push_back(_platform() == "macos" ? "macosx" : (_platform() == "linux" ? "linux" : _platform()));
	arguments.push_back("-a");
	arguments.push_back(Engine::get_singleton()->get_architecture_name() == "x86_64" ? "x64" : (Engine::get_singleton()->get_architecture_name() == "x86_32" ? "x86" : Engine::get_singleton()->get_architecture_name()));
	arguments.push_back("-m");
	arguments.push_back(build_config.to_lower());
	arguments.push_back("--egp_cpp_sdk=" + sdk_path);
	arguments.push_back("--egp_cpp_cache=" + EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/lib").path_join(String(egp_cpp_sdk_hash).left(16)));
	_append_toolchain(arguments);
	return _start_process(arguments) ? OK : ERR_CANT_FORK;
}

bool NativeExtensionEditor::_start_process(const List<String> &p_arguments) {
	List<String> arguments(p_arguments);
	if (operation != INSTALL) {
		arguments.push_front(build_path.path_join("config"));
		arguments.push_front(build_project_path);
		arguments.push_front(sdk_path.path_join("tools/run_project.lua"));
		arguments.push_front("lua");
	}
	Dictionary process = OS::get_singleton()->execute_with_pipe(process_executable, arguments, false);
	if (!process.has("pid")) {
		_set_status(vformat(TTR("Could not start %s. Set a valid xmake executable path or use Install Tools, then Check Toolchain."), process_executable), true);
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
	busy = p_busy;
	_update_controls();
}

void NativeExtensionEditor::_update_controls() {
	const bool selected = extensions->get_selected() >= 0;
	const String name = extension_name->get_text().strip_edges();
	const bool valid_name = name.is_valid_identifier() && name.length() <= 64 && name == name.to_lower();
	const bool playing = EditorRunBar::get_singleton() && EditorRunBar::get_singleton()->is_playing();
	const bool runtime_reload = bool(GLOBAL_GET("debug/hot_reload/enable_runtime"));
	check_button->set_disabled(busy);
	install_button->set_disabled(busy);
	create_button->set_disabled(busy || !valid_name);
	debug_button->set_disabled(busy || !selected || (playing && !runtime_reload));
	release_button->set_disabled(busy || !selected || playing);
	debug_button->set_tooltip_text(playing && !runtime_reload ? TTR("Stop the game first, or enable debug/hot_reload/enable_runtime before launching it.") : TTR("Save source changes, compile Debug and load or reload the node in the editor."));
	release_button->set_tooltip_text(playing ? TTR("Stop the game before building Release.") : TTR("Build the optimized library for release exports. Debug loads it into the editor."));
	extensions->set_disabled(busy || !selected);
	refresh_button->set_disabled(busy);
	source_button->set_disabled(busy || !selected);
	extension_name->set_editable(!busy);
	xmake_path->set_editable(!busy);
	restart_button->set_disabled(busy);
	selection_label->set_text(selected ? "res://extensions/" + extensions->get_item_text(extensions->get_selected()) + "/src/extension.cpp" : TTR("No C++ extensions yet. Create one above to get started."));
	copy_button->set_disabled(output->get_parsed_text().is_empty());
}

void NativeExtensionEditor::_set_status(const String &p_text, bool p_error) {
	status_label->set_text(p_text);
	status_label->set_tooltip_text(p_text);
	if (p_error) {
		status_label->add_theme_color_override("font_color", get_theme_color(SNAME("error_color"), SNAME("Editor")));
	} else {
		status_label->remove_theme_color_override("font_color");
	}
	_append_line(p_text);
}

void NativeExtensionEditor::_tool_pressed(bool p_install) {
	const Error error = p_install ? install_tools() : check_toolchain();
	if (error != OK && last_build_result != -2) {
		_set_status(vformat(TTR("Could not prepare bundled SDK (error %d). Check editor cache write permissions and available disk space."), error), true);
	}
}

void NativeExtensionEditor::_open_source() {
	if (extensions->get_selected() < 0) {
		return;
	}
	Array location;
	location.push_back(ProjectSettings::get_singleton()->globalize_path("res://extensions/" + extensions->get_item_text(extensions->get_selected()) + "/src/extension.cpp"));
	location.push_back(1);
	_diagnostic_clicked(location);
}

void NativeExtensionEditor::_copy_output() {
	DisplayServer::get_singleton()->clipboard_set(output->get_parsed_text());
}

void NativeExtensionEditor::_append_line(const String &p_line) {
	// Pipes can retain terminal colors and hyperlinks. Strip their complete
	// control sequences before displaying or parsing compiler diagnostics.
	RegEx terminal_sequences;
	terminal_sequences.compile("\\x1b(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07\\x1b]*(?:\\x07|\\x1b\\\\|$))");
	const String line = terminal_sequences.sub(p_line, "", true);
	if (cli) {
		print_line(line);
	}
	RegEx diagnostic;
	diagnostic.compile("^(.+\\.(?:cpp|hpp|h|c))(?:(?:\\((\\d+)(?:,\\d+)?\\))|(?::(\\d+)(?::\\d+)?))\\s*:");
	// xmake may prefix the first compiler diagnostic with its own severity.
	// Keep that text visible, but exclude it from the source location.
	String diagnostic_line = line;
	if (diagnostic_line.begins_with("error: ")) {
		diagnostic_line = diagnostic_line.substr(7);
	} else if (diagnostic_line.begins_with("warning: ")) {
		diagnostic_line = diagnostic_line.substr(9);
	}
	Ref<RegExMatch> match = diagnostic.search(diagnostic_line);
	if (match.is_valid()) {
		Array location;
		location.push_back(match->get_string(1).strip_edges());
		location.push_back((match->get_string(2).is_empty() ? match->get_string(3) : match->get_string(2)).to_int());
		emit_signal(SNAME("diagnostic_found"), location[0], location[1]);
		output->push_meta(location);
		output->add_text(line);
		output->pop();
	} else {
		output->add_text(line);
	}
	output->add_text("\n");
	copy_button->set_disabled(false);
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
	if (!FileAccess::exists(library)) {
		_set_status(vformat(TTR("Build completed without the expected library: %s. Check the extension target output path."), library), true);
		return ERR_FILE_NOT_FOUND;
	}
	// Publish immutable copies so a loaded Windows DLL never blocks the next build.
	const String published = library.trim_suffix(_suffix()) + "_" + FileAccess::get_sha256(library).left(16) + _suffix();
	Ref<DirAccess> directory = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	Error error = OK;
	if (!FileAccess::exists(published)) {
		error = directory->copy(library, published);
		if (error != OK) {
			_set_status(vformat(TTR("Could not publish %s (error %d). Check bin/ write permissions; the previous descriptor is unchanged."), published, error), true);
			return error;
		}
	}
	const String descriptor = "res://extensions/" + building_name + "/" + building_name + ".gdextension";
	const String transaction = "-" + itos(OS::get_singleton()->get_process_id());
	const String candidate = descriptor + ".pending" + transaction;
	const String backup = descriptor + ".previous" + transaction;
	if (FileAccess::exists(candidate) || directory->dir_exists(candidate) || FileAccess::exists(backup) || directory->dir_exists(backup)) {
		_set_status(vformat(TTR("Descriptor transaction files already exist for %s. Review .pending/.previous files before retrying; the previous descriptor is unchanged."), building_name), true);
		return ERR_ALREADY_EXISTS;
	}
	const bool had_previous = FileAccess::exists(descriptor);
	Ref<ConfigFile> config;
	config.instantiate();
	error = config->load(had_previous ? descriptor : descriptor + ".in");
	if (error != OK) {
		_set_status(vformat(TTR("Could not read the descriptor for %s (error %d). Fix its configuration before rebuilding."), building_name, error), true);
		return error;
	}
	const String feature = _platform() + "." + build_config.to_lower() + "." + Engine::get_singleton()->get_architecture_name();
	config->set_value("libraries", feature, published);
	error = config->save(candidate);
	Ref<ConfigFile> verified;
	verified.instantiate();
	if (error == OK) {
		error = verified->load(candidate);
		if (error == OK && String(verified->get_value("libraries", feature, String())) != published) {
			error = ERR_FILE_CORRUPT;
		}
	}
	if (error != OK) {
		directory->remove(candidate);
		_set_status(vformat(TTR("Could not save the new descriptor for %s (error %d). Check project write permissions; the previous descriptor is unchanged."), building_name, error), true);
		return error;
	}
	if (had_previous) {
		error = directory->rename(descriptor, backup);
		if (error != OK) {
			directory->remove(candidate);
			_set_status(vformat(TTR("Could not preserve the previous descriptor for %s (error %d). Close programs locking it, then retry."), building_name, error), true);
			return error;
		}
	}
	auto restore_descriptor = [&]() -> Error {
		if (had_previous) {
			return directory->rename(backup, descriptor);
		}
		return FileAccess::exists(descriptor) ? directory->remove(descriptor) : OK;
	};
	error = directory->rename(candidate, descriptor);
	if (error != OK) {
		const Error restore_error = restore_descriptor();
		directory->remove(candidate);
		_set_status(restore_error == OK ? vformat(TTR("Could not publish the new descriptor for %s (error %d). The previous descriptor was restored; check file permissions."), building_name, error) : vformat(TTR("Descriptor publication and restoration failed for %s. Recover the previous descriptor from %s before retrying."), building_name, backup), true);
		return error;
	}
	if (build_config == "Debug") {
		GDExtensionManager *manager = GDExtensionManager::get_singleton();
		GDExtensionManager::LoadStatus status = manager->is_extension_loaded(descriptor) ? manager->reload_extension(descriptor) : manager->load_extension(descriptor);
		if (status == GDExtensionManager::LOAD_STATUS_NEEDS_RESTART) {
			restart_button->show();
			_set_status(TTR("Build succeeded. This native class change needs a restart; save scenes and restart the editor to apply it."));
		} else if (status != GDExtensionManager::LOAD_STATUS_OK && status != GDExtensionManager::LOAD_STATUS_ALREADY_LOADED) {
			const Error restore_error = restore_descriptor();
			_scan_filesystem();
			restart_button->show();
			_set_status(restore_error == OK ? vformat(TTR("%s compiled but failed to load (status %d). The previous descriptor was restored. Review registration/dependency errors and restart the editor before retrying."), building_name, status) : vformat(TTR("%s failed to load (status %d), and descriptor restoration failed. Recover %s and restart the editor."), building_name, status, backup), true);
			return ERR_CANT_OPEN;
		} else {
			_set_status(vformat(TTR("%s Debug built and loaded. Its node is available in Create Node."), building_name));
			// The debugger applies this at an idle boundary in each running game.
			EditorDebuggerNode::get_singleton()->reload_all_scripts();
		}
	} else {
		_set_status(vformat(TTR("%s Release built and registered for export."), building_name));
	}
	if (had_previous && directory->remove(backup) != OK) {
		_append_line(vformat(TTR("Build succeeded, but descriptor backup cleanup failed: %s. Review this file before the next build."), backup));
	}
	_scan_filesystem();
	return OK;
}

void NativeExtensionEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		callable_mp(this, &NativeExtensionEditor::_run_cli).call_deferred();
	}
	if (p_what == NOTIFICATION_READY || p_what == NOTIFICATION_WM_WINDOW_FOCUS_IN || p_what == NOTIFICATION_VISIBILITY_CHANGED) {
		if (process_id == 0 && is_inside_tree()) {
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
		_set_status(operation == CHECK ? TTR("Checking C++17 compilation and linking...") : vformat(TTR("Compiling %s (%s). Compiler diagnostics below link to source lines."), building_name, build_config));
		List<String> arguments;
		arguments.push_back("-P");
		arguments.push_back(build_project_path);
		arguments.push_back("-b");
		arguments.push_back("-j");
		arguments.push_back(itos(MAX(1, OS::get_singleton()->get_processor_count() - 1)));
		arguments.push_back(operation == CHECK ? "toolchain-check" : "extension");
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
		_set_status(TTR("Toolchain ready: xmake, C++17 compiler and linker verified. Create or select an extension, then Build Debug."));
		_append_line("EGP_CPP_TOOLCHAIN_READY: xmake, C++17 compiler and linker verified.");
	} else if (result == 0) {
		const Error error = _publish_library();
		if (error != OK) {
			last_build_result = -3;
			// _publish_library retains the precise failed stage and recovery action.
			_append_line(get_status());
		}
	} else {
		_set_status(operation == BUILD ? vformat(TTR("%s %s build failed (exit code %d). Click the compiler error below, fix the source, then rebuild. The previous published library is unchanged."), building_name, build_config, result) : vformat(TTR("Toolchain %s failed (exit code %d). Review output below; check the xmake path and installed C++ compiler workload, then Check Toolchain again."), operation == INSTALL ? "installation" : "check", result), true);
	}
	_complete_operation();
}

String NativeExtensionEditor::_find_xmake() const {
	const String configured = xmake_path->get_text().strip_edges();
	if (!configured.is_empty() && configured != "xmake") {
		return configured;
	}
	const String override = OS::get_singleton()->get_environment("XMAKE").strip_edges();
	if (!override.is_empty()) {
		return override;
	}
#ifdef WINDOWS_ENABLED
	const String installed = OS::get_singleton()->get_environment("LOCALAPPDATA").path_join("xmake/xmake.exe");
	if (FileAccess::exists(installed)) {
		return installed;
	}
#elif defined(MACOS_ENABLED)
	const String pinned = OS::get_singleton()->get_environment("HOME").path_join(".local/bin/xmake");
	if (FileAccess::exists(pinned)) {
		return pinned;
	}
	if (FileAccess::exists("/opt/homebrew/bin/xmake")) {
		return "/opt/homebrew/bin/xmake";
	}
	if (FileAccess::exists("/usr/local/bin/xmake")) {
		return "/usr/local/bin/xmake";
	}
#else
	const String pinned = OS::get_singleton()->get_environment("HOME").path_join(".local/bin/xmake");
	if (FileAccess::exists(pinned)) {
		return pinned;
	}
#endif
	return "xmake";
}

Error NativeExtensionEditor::check_toolchain() {
	ERR_FAIL_COND_V(process_id != 0, ERR_BUSY);
	Error error = _prepare_sdk();
	ERR_FAIL_COND_V(error != OK, error);
	operation = CHECK;
	configuring = true;
	last_build_result = -1;
	build_config = "Debug";
	process_executable = _find_xmake();
	xmake_executable = process_executable;
	build_path = EditorPaths::get_singleton()->get_cache_dir().path_join("egp_cpp/toolchain").path_join(String(egp_cpp_sdk_hash).left(16));
	output->clear();
	_set_status(vformat(TTR("Checking xmake and compiler using %s..."), process_executable));
	List<String> arguments;
	build_project_path = sdk_path.path_join("tools");
	arguments.push_back("f");
	arguments.push_back("-y");
	arguments.push_back("-P");
	arguments.push_back(build_project_path);
	arguments.push_back("-o");
	arguments.push_back(build_path);
	arguments.push_back("-m");
	arguments.push_back("debug");
	_append_toolchain(arguments);
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
	_set_status(TTR("Installing missing xmake/compiler tools. Complete any system installer or administrator prompts. Output remains visible below."));
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
	if (!EditorNode::get_singleton()->is_editor_ready()) {
		return;
	}
	if (last_build_result != 0) {
		_quit_cli(1);
		return;
	}
	if (awaiting_filesystem || EditorFileSystem::get_singleton()->is_scanning()) {
		return;
	}
	while (cli_index < cli_commands.size()) {
		const String command = cli_commands[cli_index++];
		Error error = OK;
		if (command == "--cpp-help") {
			print_line("EGP --headless --editor --path PROJECT -- [--cpp-xmake=PATH] [--cpp-install] [--cpp-check] [--cpp-create=NAME] [--cpp-build=NAME:debug|release]");
		} else if (command.begins_with("--cpp-xmake=")) {
			xmake_path->set_text(command.trim_prefix("--cpp-xmake="));
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
		if (process_id != 0 || awaiting_filesystem || EditorFileSystem::get_singleton()->is_scanning()) {
			return;
		}
	}
	print_line("EGP_CPP_CLI_PASSED");
	_quit_cli(0);
}

void NativeExtensionEditor::_scan_filesystem() {
	// The worker clears is_scanning() before the main thread applies its results.
	// Wait for that completion signal before CLI commands advance or shut down.
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (!awaiting_filesystem) {
		awaiting_filesystem = true;
		filesystem->connect("filesystem_changed", callable_mp(this, &NativeExtensionEditor::_filesystem_changed), CONNECT_ONE_SHOT);
	}
	filesystem->scan();
}

void NativeExtensionEditor::_filesystem_changed() {
	awaiting_filesystem = false;
}

void NativeExtensionEditor::_quit_cli(int p_exit_code) {
	set_process(false);
	// Join the documentation worker before queuing shutdown so its deferred
	// callbacks run while EditorNode and the documentation database still exist.
	EditorHelp::get_doc_data();
	callable_mp(EditorNode::get_singleton(), &EditorNode::trigger_menu_option).call_deferred(EditorNode::SCENE_QUIT, true);
	callable_mp(SceneTree::get_singleton(), &SceneTree::quit).call_deferred(p_exit_code);
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
			_set_status(vformat(TTR("Could not open source %s. Verify the file still exists; refresh the extension list."), path), true);
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
	Label *description = memnew(Label(TTRC("Use the bundled SDK to create native nodes. Check tools, create an extension, edit its source, then build.")));
	description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(description);
	Label *tools_title = memnew(Label(TTRC("1. Toolchain")));
	tools_title->set_theme_type_variation("HeaderSmall");
	add_child(tools_title);
	HBoxContainer *tool_row = memnew(HBoxContainer);
	add_child(tool_row);
	tool_row->add_child(memnew(Label(TTRC("xmake:"))));
	xmake_path = memnew(LineEdit);
	xmake_path->set_name("xmakePath");
	xmake_path->set_h_size_flags(SIZE_EXPAND_FILL);
	xmake_path->set_text(EDITOR_DEF("native_extensions/xmake_path", "xmake"));
	xmake_path->set_tooltip_text(TTR("xmake executable or absolute path. Check Toolchain verifies C++17 compilation and linking. The bundled bindings require no download or separate generator setup."));
	tool_row->add_child(xmake_path);
	check_button = memnew(Button(TTRC("Check Toolchain")));
	check_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_tool_pressed).bind(false));
	tool_row->add_child(check_button);
	install_button = memnew(Button(TTRC("Install Tools")));
	install_button->set_tooltip_text(TTR("Install missing xmake/compiler tools. Your operating system may request administrator approval."));
	install_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_tool_pressed).bind(true));
	tool_row->add_child(install_button);
	Label *create_title = memnew(Label(TTRC("2. Create an extension")));
	create_title->set_theme_type_variation("HeaderSmall");
	add_child(create_title);
	HBoxContainer *create_row = memnew(HBoxContainer);
	add_child(create_row);
	extension_name = memnew(LineEdit);
	extension_name->set_name("ExtensionName");
	extension_name->set_placeholder("my_extension");
	extension_name->set_tooltip_text(TTR("A new lowercase C++ identifier, up to 64 characters. Example: player_movement."));
	extension_name->set_h_size_flags(SIZE_EXPAND_FILL);
	create_row->add_child(extension_name);
	create_button = memnew(Button(TTRC("Create Extension")));
	create_button->set_name("CreateExtension");
	create_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_create_pressed));
	create_row->add_child(create_button);
	Label *build_title = memnew(Label(TTRC("3. Edit and build")));
	build_title->set_theme_type_variation("HeaderSmall");
	add_child(build_title);
	HBoxContainer *select_row = memnew(HBoxContainer);
	add_child(select_row);
	extensions = memnew(OptionButton);
	extensions->set_name("ExtensionSelector");
	extensions->set_h_size_flags(SIZE_EXPAND_FILL);
	select_row->add_child(extensions);
	refresh_button = memnew(Button(TTRC("Refresh")));
	refresh_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_refresh_extensions));
	select_row->add_child(refresh_button);
	source_button = memnew(Button(TTRC("Open Source")));
	source_button->set_name("OpenSource");
	source_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_open_source));
	select_row->add_child(source_button);
	selection_label = memnew(Label);
	selection_label->set_name("SelectedSource");
	selection_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(selection_label);
	HBoxContainer *build_row = memnew(HBoxContainer);
	add_child(build_row);
	debug_button = memnew(Button(TTRC("Build Debug and Load")));
	debug_button->set_name("BuildDebug");
	debug_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_build_pressed).bind(false));
	build_row->add_child(debug_button);
	release_button = memnew(Button(TTRC("Build Release for Export")));
	release_button->set_name("BuildRelease");
	release_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_build_pressed).bind(true));
	build_row->add_child(release_button);
	status_label = memnew(Label(TTRC("Ready. Check Toolchain to verify xmake and a C++17 compiler.")));
	status_label->set_name("BuildStatus");
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(status_label);
	HBoxContainer *output_row = memnew(HBoxContainer);
	add_child(output_row);
	Label *output_title = memnew(Label(TTRC("Build output - click a compiler diagnostic to open its source line")));
	output_title->set_h_size_flags(SIZE_EXPAND_FILL);
	output_title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	output_row->add_child(output_title);
	copy_button = memnew(Button(TTRC("Copy Output")));
	copy_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_copy_output));
	output_row->add_child(copy_button);
	output = memnew(RichTextLabel);
	output->set_custom_minimum_size(Size2(0, 180 * EDSCALE));
	output->set_v_size_flags(SIZE_EXPAND_FILL);
	output->set_scroll_follow(true);
	output->set_selection_enabled(true);
	output->connect("meta_clicked", callable_mp(this, &NativeExtensionEditor::_diagnostic_clicked));
	add_child(output);
	restart_button = memnew(Button(TTRC("Save Scenes and Restart Editor")));
	restart_button->connect("pressed", callable_mp(this, &NativeExtensionEditor::_restart_pressed));
	restart_button->hide();
	add_child(restart_button);
	extension_name->connect("text_changed", callable_mp(this, &NativeExtensionEditor::_update_controls).unbind(1));
	extensions->connect("item_selected", callable_mp(this, &NativeExtensionEditor::_update_controls).unbind(1));
	_update_controls();
	set_process(false);
}

NativeExtensionEditor::~NativeExtensionEditor() {
	if (process_id != 0) {
#ifdef WINDOWS_ENABLED
		// xmake launches compiler and linker children. Terminate this build's tree on editor shutdown.
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
