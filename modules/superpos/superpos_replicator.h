// SPDX-License-Identifier: MIT
#pragma once
#include "scene/main/node.h"
#include "superpos_session.h"
#include <array>

// Native scene projection. This header remains C++17-compatible. Publication
// is canonical; arbitrary property setters only provide sequential projection.
class SuperposReplicator : public Node {
    GDCLASS(SuperposReplicator, Node);
    struct Mapping { uint64_t field = 0; StringName property; };
    struct Snapshot {
        ObjectID self, session, target;
        uint64_t incarnation = 0, binding = 0, handle = 0;
        uint32_t count = 0;
        std::array<Mapping, 64> mappings;
    };
    ObjectID session_id, target_id;
    uint64_t incarnation = 0, binding_generation = 0, object_handle = 0;
    uint64_t dirty = 0;
    uint32_t mapping_count = 0;
    std::array<Mapping, 64> mappings;
    bool bound = false, operating = false;
    struct Operation;
    Error snapshot(Snapshot &r_snapshot);
    static Error validate(const Snapshot &p_snapshot);
    static Dictionary report(Error p_error, uint32_t p_completed, uint64_t p_revision, bool p_interrupted);
    static void restore_dirty(const Snapshot &p_snapshot, uint64_t p_mask);
protected:
    static void _bind_methods();
    void _notification(int p_what);
public:
    // Mapping keys are full-width field IDs and values are StringName/String.
    // Binding validates only the frozen schema; it executes no scene getters,
    // property-list hooks or gameplay methods.
    Error bind_fields(const Ref<SuperposSession> &p_session, Node *p_target,
        uint64_t p_handle, const Dictionary &p_mapping);
    Error clear_binding();
    Error mark_dirty(uint64_t p_field);
    Error mark_all_dirty();
    // Call once at the owner's capture phase, not once per interested peer.
    Dictionary capture_dirty();
    // Applies a single immutable canonical image through ordinary setters.
    // Reports interruption/failure and an exact applied prefix. No atomic
    // scene projection, automatic polling or script bridge is implied.
    Dictionary project_current();
    Dictionary read_binding() const;
};
