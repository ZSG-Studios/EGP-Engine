/**************************************************************************/
/*  native_extension_editor.h                                            */
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

#pragma once

#include "core/io/file_access.h"
#include "scene/gui/box_container.h"

class Button;
class Label;
class LineEdit;
class OptionButton;
class RichTextLabel;

// Built-in editor workflow; project libraries continue to use the standard GDExtension ABI.
class NativeExtensionEditor : public VBoxContainer {
	GDCLASS(NativeExtensionEditor, VBoxContainer);

	LineEdit *extension_name = nullptr;
	LineEdit *cmake_path = nullptr;
	OptionButton *extensions = nullptr;
	Button *create_button = nullptr;
	Button *debug_button = nullptr;
	Button *release_button = nullptr;
	Button *restart_button = nullptr;
	RichTextLabel *output = nullptr;
	String sdk_path;
	String building_name;
	String build_path;
	String build_config;
	String cmake_executable;
	Ref<FileAccess> pipes[2];
	Vector<uint8_t> pending_output[2];
	int64_t process_id = 0;
	bool configuring = false;
	int last_build_result = -1;

	Error _prepare_sdk();
	String _platform() const;
	String _suffix() const;
	void _refresh_extensions();
	void _create_pressed();
	void _build_pressed(bool p_release);
	void _restart_pressed();
	void _diagnostic_clicked(const Variant &p_meta);
	void _append_line(const String &p_line);
	void _drain_pipe(int p_index, bool p_final = false);
	bool _start_process(const List<String> &p_arguments);
	void _set_busy(bool p_busy);
	Error _publish_library();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	Error create_extension(const String &p_name);
	Error build_extension(const String &p_name, bool p_release = false);
	bool is_building() const { return process_id != 0; }
	int get_last_build_result() const { return last_build_result; }
	NativeExtensionEditor();
	~NativeExtensionEditor();
};
