// SPDX-License-Identifier: MIT
#include "superpos_uint64_editor.h"
#ifdef TOOLS_ENABLED
#include "superpos_schema.h"
#include "superpos_uint64.h"
#include "superpos_world.h"
#include "u64_bits.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"
#include "core/input/input_event.h"

namespace {
Ref<SuperposUInt64InspectorPlugin> inspector;
void prioritize_inspector() {
    // The default inspector is registered by EditorNode after module startup.
    // Move this native editor to the end so its exact fields take precedence.
    EditorInspector::remove_inspector_plugin(inspector);
    EditorInspector::add_inspector_plugin(inspector);
}
}

SuperposUInt64EditorProperty::SuperposUInt64EditorProperty(bool p_allow_zero) : allow_zero(p_allow_zero) {
    text = memnew(LineEdit);
    text->set_h_size_flags(SIZE_EXPAND_FILL);
    text->set_max_length(20);
    text->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
    add_child(text);
    add_focusable(text);
    text->connect("text_changed", callable_mp(this, &SuperposUInt64EditorProperty::text_changed));
    text->connect("focus_exited", callable_mp(this, &SuperposUInt64EditorProperty::editing_finished));
    exact_menu = memnew(PopupMenu);
    add_child(exact_menu);
    exact_menu->add_item("Copy Value", 0);
    exact_menu->add_item("Paste Value", 1);
    exact_menu->connect("id_pressed", callable_mp(this, &SuperposUInt64EditorProperty::menu_selected));
}

void SuperposUInt64EditorProperty::_bind_methods() {
    ClassDB::bind_method(D_METHOD("submit_text", "input"), &SuperposUInt64EditorProperty::submit_text);
    ClassDB::bind_method(D_METHOD("submit_exact_value", "input"), &SuperposUInt64EditorProperty::submit_exact_value);
    ClassDB::bind_method(D_METHOD("get_decimal_text"), &SuperposUInt64EditorProperty::get_decimal_text);
}
void SuperposUInt64EditorProperty::validation(bool p_valid) {
    if (p_valid) {
        text->remove_theme_color_override("font_color");
        text->set_tooltip_text(text->get_text());
    } else {
        text->add_theme_color_override("font_color", Color(1.0, 0.35, 0.3));
        text->set_tooltip_text(allow_zero ? "Enter a whole number from 0 to 18446744073709551615." : "Enter a whole number from 1 to 18446744073709551615.");
    }
}
Error SuperposUInt64EditorProperty::submit_text(const Variant &p_input) {
    if (p_input.get_type() != Variant::STRING) { validation(false); return ERR_INVALID_DATA; }
    String value = p_input;
    if (value.is_empty() || value.length() > 20) { validation(false); return ERR_INVALID_DATA; }
    for (int i = 0; i < value.length(); ++i) {
        if (value[i] < '0' || value[i] > '9') { validation(false); return ERR_INVALID_DATA; }
    }
    CharString encoded = value.utf8();
    uint64_t parsed = 0;
    if (!superpos_egp::parse_decimal(std::string(encoded.get_data(), size_t(encoded.length())), parsed) || (!allow_zero && !parsed)) {
        validation(false);
        return ERR_INVALID_DATA;
    }
    return submit_exact_value(superpos_egp::signed_bits(parsed));
}
Error SuperposUInt64EditorProperty::submit_exact_value(const Variant &p_input) {
    // Inspector clipboard values never pass through a floating-point cast.
    if (p_input.get_type() != Variant::INT || (!allow_zero && int64_t(p_input) == 0)) {
        validation(false);
        return ERR_INVALID_DATA;
    }
    if (is_read_only()) { return ERR_UNAUTHORIZED; }
    if (!get_edited_object()) { return ERR_UNCONFIGURED; }
    updating = true;
    text->set_text(SuperposUInt64::to_decimal(superpos_egp::unsigned_bits(int64_t(p_input))));
    updating = false;
    validation(true);
    // Do not access this control after callbacks; an inspector rebuild may free it.
    emit_changed(get_edited_property(), p_input);
    return OK;
}
String SuperposUInt64EditorProperty::get_decimal_text() const { return text->get_text(); }
void SuperposUInt64EditorProperty::text_changed(const String &p_text) {
    if (!updating) { submit_text(p_text); }
}
void SuperposUInt64EditorProperty::editing_finished() { update_property(); }
void SuperposUInt64EditorProperty::update_property() {
    Variant value = get_edited_property_value();
    if (value.get_type() != Variant::INT) { validation(false); return; }
    updating = true;
    text->set_text(SuperposUInt64::to_decimal(superpos_egp::unsigned_bits(int64_t(value))));
    text->set_editable(!is_read_only());
    updating = false;
    validation(true);
}
void SuperposUInt64EditorProperty::_set_read_only(bool p_read_only) { text->set_editable(!p_read_only); }
void SuperposUInt64EditorProperty::menu_selected(int p_index) {
    if (p_index == 0) {
        EditorInspector::set_property_clipboard(EditorInspector::PropertyClipboard::Type::PROPERTY, get_edited_property_value());
    } else if (p_index == 1) {
        submit_exact_value(EditorInspector::get_property_clipboard_value());
    }
}
void SuperposUInt64EditorProperty::gui_input(const Ref<InputEvent> &p_event) {
    Ref<InputEventMouseButton> mouse = p_event;
    if (mouse.is_valid() && mouse->get_button_index() == MouseButton::RIGHT && mouse->is_pressed()) {
        exact_menu->set_item_disabled(1, is_read_only() || EditorInspector::get_property_clipboard_type() != EditorInspector::PropertyClipboard::Type::PROPERTY || EditorInspector::get_property_clipboard_value().get_type() != Variant::INT);
        exact_menu->set_position(get_screen_position());
        exact_menu->popup();
        accept_event();
        return;
    }
    EditorProperty::gui_input(p_event);
}
void SuperposUInt64EditorProperty::shortcut_input(const Ref<InputEvent> &p_event) {
    if (p_event->is_pressed() && !p_event->is_echo() && ED_IS_SHORTCUT("property_editor/paste_value", p_event)) {
        submit_exact_value(EditorInspector::get_property_clipboard_value());
        accept_event();
        return;
    }
    EditorProperty::shortcut_input(p_event);
}

bool SuperposUInt64InspectorPlugin::can_handle(Object *p_object) {
    return Object::cast_to<SuperposField>(p_object) || Object::cast_to<SuperposSchema>(p_object) || Object::cast_to<SuperposWorld>(p_object);
}
bool SuperposUInt64InspectorPlugin::parse_property(Object *p_object, Variant::Type p_type, const String &p_path,
        PropertyHint p_hint, const String &p_hint_text, BitField<PropertyUsageFlags> p_usage, bool p_wide) {
    if (p_type != Variant::INT) { return false; }
    bool exact = (Object::cast_to<SuperposField>(p_object) && p_path == "field_id") ||
        (Object::cast_to<SuperposSchema>(p_object) && (p_path == "schema_id" || p_path == "revision")) ||
        (Object::cast_to<SuperposWorld>(p_object) && (p_path == "authority_epoch" || p_path == "authority_peer"));
    if (!exact) { return false; }
    add_property_editor(p_path, memnew(SuperposUInt64EditorProperty(p_path == "authority_peer")));
    return true;
}
void initialize_superpos_uint64_inspector() {
    GDREGISTER_INTERNAL_CLASS(SuperposUInt64EditorProperty);
    GDREGISTER_INTERNAL_CLASS(SuperposUInt64InspectorPlugin);
    inspector.instantiate();
    EditorInspector::add_inspector_plugin(inspector);
    EditorNode::add_plugin_init_callback(prioritize_inspector);
}
void uninitialize_superpos_uint64_inspector() {
    // Only the module and inspector registry own this private plugin instance.
    // EditorNode clears its registry during normal editor teardown; headless
    // SceneTree/script/API-export paths never construct EditorNode.
    if (inspector.is_valid() && inspector->get_reference_count() > 1) {
        EditorInspector::remove_inspector_plugin(inspector);
    }
    inspector.unref();
}
#endif
