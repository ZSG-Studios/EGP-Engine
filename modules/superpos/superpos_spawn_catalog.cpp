// SPDX-License-Identifier: MIT
#include "superpos_spawn_catalog.h"
#include "core/object/class_db.h"
#include "private/property_path.hpp"
#include <algorithm>
#include <cstring>

namespace {
uint64_t unsigned_bits(int64_t p_value) {
    uint64_t result;
    static_assert(sizeof(result) == sizeof(p_value));
    std::memcpy(&result, &p_value, sizeof(result));
    return result;
}
}

Error SuperposSpawnCatalog::snapshot(Snapshot &r_result) const {
    if (get_script_instance() || entries.is_empty() || entries.size() > 64) { return ERR_INVALID_PARAMETER; }
    const int count = entries.size();
    std::array<Ref<SuperposSpawnEntry>, 64> pinned_entries;
    for (int i = 0; i < count; ++i) {
        if (entries.size() != count) return ERR_BUSY;
        pinned_entries[i] = entries[i];
        if (entries.size() != count) return ERR_BUSY;
    }
    Snapshot candidate;
    for (int i = 0; i < count; ++i) {
        const Ref<SuperposSpawnEntry> &source = pinned_entries[i];
        if (source.is_null() || source->get_script_instance() || !source->get_resource_id() || !source->get_schema_id() || source->get_scene().is_null() || source->get_scene()->get_script_instance()) { return ERR_INVALID_PARAMETER; }
        auto &entry = candidate.entries[i];
        entry.resource = source->get_resource_id(); entry.schema = source->get_schema_id();
        // Copy native scene structure without instantiation or Resource script
        // getters. Later repacking the Inspector resource cannot change which
        // nodes are constructed by an already admitted catalog.
        Ref<SceneState> state; state.instantiate();
        const Error copied = state->copy_from(source->get_scene()->get_state());
        if (copied != OK) return copied;
        entry.scene.instantiate(); entry.scene->replace_state(state);
        for (int j = 0; j < i; ++j) {
            if (candidate.entries[j].resource == entry.resource || candidate.entries[j].schema == entry.schema) { return ERR_ALREADY_EXISTS; }
        }
        Dictionary fields = source->get_fields();
        if (fields.is_empty() || fields.size() > 64) { return ERR_INVALID_PARAMETER; }
        Array keys = fields.keys();
        for (int f = 0; f < keys.size(); ++f) {
            if (keys[f].get_type() != Variant::INT || fields[keys[f]].get_type() != Variant::DICTIONARY) { return ERR_INVALID_PARAMETER; }
            Dictionary mapping = fields[keys[f]];
            if (mapping.size() != 2 || !mapping.has("node") || !mapping.has("property")) { return ERR_INVALID_PARAMETER; }
            if (mapping["node"].get_type() != Variant::NODE_PATH ||
                    (mapping["property"].get_type() != Variant::STRING_NAME && mapping["property"].get_type() != Variant::STRING)) { return ERR_INVALID_PARAMETER; }
            auto &field = entry.fields[f];
            field.id = unsigned_bits(int64_t(keys[f])); field.node = mapping["node"]; field.property = mapping["property"];
            // Paths remain inside the instantiated scene. Root is the empty
            // path; parent traversal and absolute paths cannot escape custody.
            if (!field.id || field.node.is_absolute() || field.node.get_subname_count() || field.node.get_name_count() > 32) { return ERR_INVALID_PARAMETER; }
            if (superpos_egp::parse_property_path(String(field.property), field.path) != OK) { return ERR_INVALID_PARAMETER; }
            for (int n = 0; n < field.node.get_name_count(); ++n) { if (field.node.get_name(n) == ".." || String(field.node.get_name(n)).length() > 64) { return ERR_INVALID_PARAMETER; } }
            for (int j = 0; j < f; ++j) {
                const auto &previous = entry.fields[j];
                if (previous.id == field.id || (previous.node == field.node && superpos_egp::property_paths_overlap(previous.path, field.path))) { return ERR_ALREADY_EXISTS; }
            }
        }
        entry.field_count = uint32_t(keys.size());
        std::sort(entry.fields.begin(), entry.fields.begin() + entry.field_count,
                [](const Field &a, const Field &b) { return a.id < b.id; });
    }
    candidate.entry_count = uint32_t(count);
    r_result = std::move(candidate);
    return OK;
}

Dictionary SuperposSpawnCatalog::validate() const {
    Snapshot checked;
    Dictionary result;
    const Error error = snapshot(checked);
    result["error"] = error;
    result["entries"] = error == OK ? checked.entry_count : 0;
    result["projection"] = "sequential";
    return result;
}

void SuperposSpawnEntry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_resource_id", "value"), &SuperposSpawnEntry::set_resource_id);
    ClassDB::bind_method(D_METHOD("get_resource_id"), &SuperposSpawnEntry::get_resource_id);
    ClassDB::bind_method(D_METHOD("set_schema_id", "value"), &SuperposSpawnEntry::set_schema_id);
    ClassDB::bind_method(D_METHOD("get_schema_id"), &SuperposSpawnEntry::get_schema_id);
    ClassDB::bind_method(D_METHOD("set_scene", "value"), &SuperposSpawnEntry::set_scene);
    ClassDB::bind_method(D_METHOD("get_scene"), &SuperposSpawnEntry::get_scene);
    ClassDB::bind_method(D_METHOD("set_fields", "value"), &SuperposSpawnEntry::set_fields);
    ClassDB::bind_method(D_METHOD("get_fields"), &SuperposSpawnEntry::get_fields);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "resource_id"), "set_resource_id", "get_resource_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "schema_id"), "set_schema_id", "get_schema_id");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "fields"), "set_fields", "get_fields");
}

void SuperposSpawnCatalog::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_entries", "value"), &SuperposSpawnCatalog::set_entries);
    ClassDB::bind_method(D_METHOD("get_entries"), &SuperposSpawnCatalog::get_entries);
    ClassDB::bind_method(D_METHOD("validate"), &SuperposSpawnCatalog::validate);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_ARRAY_TYPE, "SuperposSpawnEntry"), "set_entries", "get_entries");
}
