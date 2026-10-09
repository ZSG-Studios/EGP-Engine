// SPDX-License-Identifier: MIT
#pragma once
#include "core/io/resource.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "scene/resources/packed_scene.h"
#include <array>

// Local Inspector authoring only. Network input selects an already admitted
// numeric resource ID; it cannot supply a scene path or a property name.
class SuperposSpawnEntry : public Resource {
    GDCLASS(SuperposSpawnEntry, Resource);
    uint64_t resource_id = 1, schema_id = 1;
    Ref<PackedScene> scene;
    Dictionary fields;
protected:
    static void _bind_methods();
public:
    void set_resource_id(uint64_t p_value) { resource_id = p_value; emit_changed(); }
    uint64_t get_resource_id() const { return resource_id; }
    void set_schema_id(uint64_t p_value) { schema_id = p_value; emit_changed(); }
    uint64_t get_schema_id() const { return schema_id; }
    void set_scene(const Ref<PackedScene> &p_value) { scene = p_value; emit_changed(); }
    Ref<PackedScene> get_scene() const { return scene; }
    void set_fields(const Dictionary &p_value) { fields = p_value; emit_changed(); }
    Dictionary get_fields() const { return fields; }
};

class SuperposSpawnCatalog : public Resource {
    GDCLASS(SuperposSpawnCatalog, Resource);
    TypedArray<SuperposSpawnEntry> entries;
protected:
    static void _bind_methods();
public:
    // Snapshotting never instantiates a scene, resolves a NodePath, inspects a
    // script property list, or executes a gameplay getter.
    struct Field {
        uint64_t id = 0;
        NodePath node;
        StringName property;
    };
    struct Entry {
        uint64_t resource = 0, schema = 0;
        Ref<PackedScene> scene;
        std::array<Field, 64> fields;
        uint32_t field_count = 0;
    };
    struct Snapshot {
        std::array<Entry, 64> entries;
        uint32_t entry_count = 0;
    };
    void set_entries(const TypedArray<SuperposSpawnEntry> &p_value) { entries = p_value.duplicate(); emit_changed(); }
    TypedArray<SuperposSpawnEntry> get_entries() const { return entries.duplicate(); }
    Error snapshot(Snapshot &r_result) const;
    Dictionary validate() const;
};
