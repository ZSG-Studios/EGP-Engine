// SPDX-License-Identifier: MIT
#include "superpos_replicator.h"
#include "u64_bits.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include <limits>

struct SuperposReplicator::Operation {
    ObjectID self;
    explicit Operation(ObjectID p_self) : self(p_self) {}
    ~Operation() {
        auto *owner = Object::cast_to<SuperposReplicator>(ObjectDB::get_instance(self));
        if (owner) owner->operating = false;
    }
};

void SuperposReplicator::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind_fields", "session", "target", "handle", "mapping"), &SuperposReplicator::bind_fields);
    ClassDB::bind_method(D_METHOD("clear_binding"), &SuperposReplicator::clear_binding);
    ClassDB::bind_method(D_METHOD("mark_dirty", "field"), &SuperposReplicator::mark_dirty);
    ClassDB::bind_method(D_METHOD("mark_all_dirty"), &SuperposReplicator::mark_all_dirty);
    ClassDB::bind_method(D_METHOD("capture_dirty"), &SuperposReplicator::capture_dirty);
    ClassDB::bind_method(D_METHOD("project_current"), &SuperposReplicator::project_current);
    ClassDB::bind_method(D_METHOD("read_binding"), &SuperposReplicator::read_binding);
}

void SuperposReplicator::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE || p_what == NOTIFICATION_PREDELETE) clear_binding();
}

Error SuperposReplicator::clear_binding() {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    // Retirement is immediate even inside a getter/setter. The next callback
    // fence rejects old work. Exhausted incarnations can never bind again.
    bound = false;
    dirty = 0;
    session_id = ObjectID();
    target_id = ObjectID();
    object_handle = binding_generation = 0;
    if (incarnation != std::numeric_limits<uint64_t>::max()) ++incarnation;
    return OK;
}

Error SuperposReplicator::bind_fields(const Ref<SuperposSession> &p_session, Node *p_target,
        uint64_t p_handle, const Dictionary &p_mapping) {
    if (!Thread::is_main_thread() || operating) return ERR_BUSY;
    if (bound) return ERR_ALREADY_IN_USE;
    if (incarnation == std::numeric_limits<uint64_t>::max()) return ERR_UNAVAILABLE;
    if (p_session.is_null() || !p_target || p_target == this || p_target->is_queued_for_deletion() ||
            !p_handle || p_mapping.is_empty() || p_mapping.size() > 64) return ERR_INVALID_PARAMETER;
    std::array<Mapping, 64> staged;
    PackedInt64Array fields;
    Error error = fields.resize(p_mapping.size());
    if (error != OK) return error;
    for (int i = 0; i < p_mapping.size(); ++i) {
        const Variant key = p_mapping.get_key_at_index(i);
        const Variant value = p_mapping.get_value_at_index(i);
        if (key.get_type() != Variant::INT || !uint64_t(int64_t(key)) ||
                (value.get_type() != Variant::STRING_NAME && value.get_type() != Variant::STRING)) return ERR_INVALID_PARAMETER;
        staged[i] = {uint64_t(int64_t(key)), StringName(value)};
        if (staged[i].property == StringName() || String(staged[i].property).length() > 128) return ERR_INVALID_PARAMETER;
        for (int j = 0; j < i; ++j) if (staged[j].property == staged[i].property) return ERR_INVALID_PARAMETER;
        fields.set(i, int64_t(key));
    }
    const Dictionary image = p_session->read_fields(p_handle, fields);
    error = Error(int(image.get("error", ERR_UNCONFIGURED)));
    if (error != OK) return error;
    uint64_t generation = 0;
    if ((error = p_session->query_binding_generation(generation)) != OK) return error;
    if (generation != uint64_t(int64_t(image["binding_generation"]))) return ERR_UNAVAILABLE;
    mappings = staged;
    mapping_count = uint32_t(p_mapping.size());
    session_id = p_session->get_instance_id();
    target_id = p_target->get_instance_id();
    object_handle = p_handle;
    binding_generation = generation;
    ++incarnation;
    dirty = 0;
    bound = true;
    return OK;
}

Error SuperposReplicator::snapshot(Snapshot &r_snapshot) {
    if (!Thread::is_main_thread() || operating) return ERR_BUSY;
    if (!bound || is_queued_for_deletion()) return ERR_UNCONFIGURED;
    r_snapshot = {get_instance_id(), session_id, target_id, incarnation,
        binding_generation, object_handle, mapping_count, mappings};
    const Error error = validate(r_snapshot);
    if (error != OK) return error;
    operating = true;
    return OK;
}

Error SuperposReplicator::validate(const Snapshot &p_snapshot) {
    auto *owner = Object::cast_to<SuperposReplicator>(ObjectDB::get_instance(p_snapshot.self));
    auto *target = Object::cast_to<Node>(ObjectDB::get_instance(p_snapshot.target));
    auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(p_snapshot.session));
    if (!owner || !owner->bound || owner->incarnation != p_snapshot.incarnation ||
            owner->is_queued_for_deletion() || !target || target->is_queued_for_deletion() || !session) return ERR_UNAVAILABLE;
    uint64_t generation = 0;
    Error error = session->query_binding_generation(generation);
    if (error != OK) return error;
    return generation == p_snapshot.binding ? OK : ERR_UNAVAILABLE;
}

Dictionary SuperposReplicator::report(Error p_error, uint32_t p_completed,
        uint64_t p_revision, bool p_interrupted) {
    Dictionary result;
    result["error"] = p_error;
    result["status"] = p_error == OK ? "completed" : (p_interrupted ? "interrupted" : "failed");
    result["completed_fields"] = p_completed;
    result["source_revision"] = superpos_egp::signed_bits(p_revision);
    return result;
}

Error SuperposReplicator::mark_dirty(uint64_t p_field) {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    if (!bound) return ERR_UNCONFIGURED;
    for (uint32_t i = 0; i < mapping_count; ++i) if (mappings[i].field == p_field) {
        dirty |= uint64_t(1) << i;
        return OK;
    }
    return ERR_DOES_NOT_EXIST;
}
Error SuperposReplicator::mark_all_dirty() {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    if (!bound) return ERR_UNCONFIGURED;
    dirty = mapping_count == 64 ? std::numeric_limits<uint64_t>::max() : (uint64_t(1) << mapping_count) - 1;
    return OK;
}
void SuperposReplicator::restore_dirty(const Snapshot &p_snapshot, uint64_t p_mask) {
    auto *owner = Object::cast_to<SuperposReplicator>(ObjectDB::get_instance(p_snapshot.self));
    if (owner && owner->bound && owner->incarnation == p_snapshot.incarnation) owner->dirty |= p_mask;
}

Dictionary SuperposReplicator::capture_dirty() {
    Snapshot captured;
    Error error = snapshot(captured);
    if (error != OK) return report(error, 0, 0, false);
    Operation operation(captured.self);
    const uint64_t selected = dirty;
    // New marks raised by getters/publication callbacks survive this capture.
    dirty &= ~selected;
    if (!selected) return report(OK, 0, 0, false);
    PackedInt64Array fields;
    if ((error = fields.resize(1)) != OK) { restore_dirty(captured, selected); return report(error, 0, 0, false); }
    fields.set(0, superpos_egp::signed_bits(captured.mappings[0].field));
    auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
    const Dictionary before = session->read_fields(captured.handle, fields);
    error = Error(int(before.get("error", ERR_UNCONFIGURED)));
    if (error != OK) { restore_dirty(captured, selected); return report(error, 0, 0, false); }
    const uint64_t revision = uint64_t(int64_t(before["revision"]));
    Dictionary values;
    uint32_t count = 0;
    for (uint32_t i = 0; i < captured.count; ++i) if (selected & (uint64_t(1) << i)) {
        if ((error = validate(captured)) != OK) { restore_dirty(captured, selected); return report(error, 0, revision, true); }
        auto *target = Object::cast_to<Node>(ObjectDB::get_instance(captured.target));
        bool valid = false;
        const Variant value = target->get(captured.mappings[i].property, &valid);
        // A getter may free either Node, retire its Session, or change bindings.
        if ((error = validate(captured)) != OK) { restore_dirty(captured, selected); return report(error, 0, revision, true); }
        if (!valid) { restore_dirty(captured, selected); return report(ERR_DOES_NOT_EXIST, 0, revision, false); }
        values[superpos_egp::signed_bits(captured.mappings[i].field)] = value;
        ++count;
    }
    if ((error = validate(captured)) != OK) { restore_dirty(captured, selected); return report(error, 0, revision, true); }
    session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
    error = session->publish_fields(captured.handle, revision, values);
    if (error != OK) restore_dirty(captured, selected);
    // Do not touch this after canonical_published invokes arbitrary user code.
    return report(error, error == OK ? count : 0, revision, false);
}

Dictionary SuperposReplicator::project_current() {
    Snapshot captured;
    Error error = snapshot(captured);
    if (error != OK) return report(error, 0, 0, false);
    Operation operation(captured.self);
    PackedInt64Array fields;
    if ((error = fields.resize(captured.count)) != OK) return report(error, 0, 0, false);
    for (uint32_t i = 0; i < captured.count; ++i) fields.set(i, superpos_egp::signed_bits(captured.mappings[i].field));
    auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
    const Dictionary image = session->read_fields(captured.handle, fields);
    error = Error(int(image.get("error", ERR_UNCONFIGURED)));
    if (error != OK) return report(error, 0, 0, false);
    const uint64_t revision = uint64_t(int64_t(image["revision"]));
    const Dictionary values = image["values"];
    // Canonical publication already completed. Scene projection is explicitly
    // sequential and stops at the first invalidated callback boundary.
    PackedInt64Array revision_probe;
    if ((error = revision_probe.resize(1)) != OK) return report(error, 0, revision, false);
    revision_probe.set(0, fields[0]);
    for (uint32_t i = 0; i < captured.count; ++i) {
        if ((error = validate(captured)) != OK) return report(error, i, revision, true);
        auto *target = Object::cast_to<Node>(ObjectDB::get_instance(captured.target));
        bool valid = false;
        target->set(captured.mappings[i].property,
            values[superpos_egp::signed_bits(captured.mappings[i].field)], &valid);
        if ((error = validate(captured)) != OK) return report(error, i + uint32_t(valid), revision, true);
        if (!valid) return report(ERR_DOES_NOT_EXIST, i, revision, false);
        session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
        const Dictionary current = session->read_fields(captured.handle, revision_probe);
        error = Error(int(current.get("error", ERR_UNCONFIGURED)));
        if (error != OK || uint64_t(int64_t(current.get("revision", 0))) != revision)
            return report(error == OK ? ERR_INVALID_DATA : error, i + 1, revision, true);
    }
    return report(OK, captured.count, revision, false);
}

Dictionary SuperposReplicator::read_binding() const {
    Dictionary result;
    if (!Thread::is_main_thread()) { result["error"] = ERR_BUSY; return result; }
    const Snapshot current{get_instance_id(), session_id, target_id, incarnation,
        binding_generation, object_handle, mapping_count, mappings};
    const Error error = bound ? validate(current) : ERR_UNCONFIGURED;
    result["error"] = error;
    if (error == OK) {
        result["handle"] = superpos_egp::signed_bits(object_handle);
        result["binding_generation"] = superpos_egp::signed_bits(binding_generation);
        result["incarnation"] = superpos_egp::signed_bits(incarnation);
        result["field_count"] = mapping_count;
        result["dirty_mask"] = superpos_egp::signed_bits(dirty);
    }
    return result;
}
