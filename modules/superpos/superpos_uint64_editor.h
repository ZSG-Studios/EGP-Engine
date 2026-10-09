// SPDX-License-Identifier: MIT
#pragma once
#ifdef TOOLS_ENABLED
#include "editor/inspector/editor_inspector.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup_menu.h"

class SuperposUInt64EditorProperty : public EditorProperty {
    GDCLASS(SuperposUInt64EditorProperty, EditorProperty);
    LineEdit *text = nullptr;
    PopupMenu *exact_menu = nullptr;
    bool updating = false;
    bool allow_zero = false;
    void text_changed(const String &p_text);
    void editing_finished();
    void menu_selected(int p_index);
    void validation(bool p_valid);
protected:
    static void _bind_methods();
    void _set_read_only(bool p_read_only) override;
    void gui_input(const Ref<InputEvent> &p_event) override;
    void shortcut_input(const Ref<InputEvent> &p_event) override;
public:
    explicit SuperposUInt64EditorProperty(bool p_allow_zero = false);
    void update_property() override;
    Error submit_text(const Variant &p_input);
    Error submit_exact_value(const Variant &p_input);
    String get_decimal_text() const;
};

class SuperposUInt64InspectorPlugin : public EditorInspectorPlugin {
    GDCLASS(SuperposUInt64InspectorPlugin, EditorInspectorPlugin);
public:
    bool can_handle(Object *p_object) override;
    bool parse_property(Object *p_object, Variant::Type p_type, const String &p_path,
            PropertyHint p_hint, const String &p_hint_text,
            BitField<PropertyUsageFlags> p_usage, bool p_wide = false) override;
};

void initialize_superpos_uint64_inspector();
void uninitialize_superpos_uint64_inspector();
#endif
