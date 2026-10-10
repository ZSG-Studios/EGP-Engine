// SPDX-License-Identifier: MIT
#include "superpos_replicator.h"
#include "u64_bits.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "private/property_path.hpp"
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
    ClassDB::bind_method(D_METHOD("bind_schema", "session", "target", "handle", "schema"), &SuperposReplicator::bind_schema);
    ClassDB::bind_method(D_METHOD("clear_binding"), &SuperposReplicator::clear_binding);
    ClassDB::bind_method(D_METHOD("mark_dirty", "field"), &SuperposReplicator::mark_dirty);
    ClassDB::bind_method(D_METHOD("mark_all_dirty"), &SuperposReplicator::mark_all_dirty);
    ClassDB::bind_method(D_METHOD("capture_dirty"), &SuperposReplicator::capture_dirty);
    ClassDB::bind_method(D_METHOD("capture_changes"), &SuperposReplicator::capture_changes);
    ClassDB::bind_method(D_METHOD("set_automatic_capture", "enabled"), &SuperposReplicator::set_automatic_capture);
    ClassDB::bind_method(D_METHOD("is_automatic_capture"), &SuperposReplicator::is_automatic_capture);
    ClassDB::bind_method(D_METHOD("set_capture_interval", "frames"), &SuperposReplicator::set_capture_interval);
    ClassDB::bind_method(D_METHOD("get_capture_interval"), &SuperposReplicator::get_capture_interval);
    ClassDB::bind_method(D_METHOD("project_current"), &SuperposReplicator::project_current);
    ClassDB::bind_method(D_METHOD("read_binding"), &SuperposReplicator::read_binding);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "automatic_capture"), "set_automatic_capture", "is_automatic_capture");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "capture_interval", PROPERTY_HINT_RANGE, "1,600,1"), "set_capture_interval", "get_capture_interval");
}

void SuperposReplicator::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: set_physics_process_internal(automatic); break;
        case NOTIFICATION_INTERNAL_PHYSICS_PROCESS:
            if (automatic && bound && !operating && ++frames_since_capture >= capture_interval) {
                frames_since_capture = 0;
                // The result is recorded in this node's counters; publication
                // callbacks may free this node, so nothing follows the call.
                capture_changes();
            }
            break;
        case NOTIFICATION_EXIT_TREE: case NOTIFICATION_PREDELETE: clear_binding(); break;
        default: break;
    }
}

void SuperposReplicator::set_automatic_capture(bool p_enabled) {
    automatic = p_enabled;
    frames_since_capture = 0;
    if (is_inside_tree()) set_physics_process_internal(p_enabled);
}
void SuperposReplicator::set_capture_interval(uint32_t p_frames) {
    capture_interval = CLAMP(p_frames, 1u, 600u);
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

Error SuperposReplicator::bind_schema(const Ref<SuperposSession> &p_session, Node *p_target,
        uint64_t p_handle, const Ref<SuperposSchema> &p_schema) {
    if (!Thread::is_main_thread() || operating) return ERR_BUSY;
    if (p_session.is_null() || p_schema.is_null()) return ERR_INVALID_PARAMETER;
    const Dictionary object = p_session->read_object(p_handle);
    const Error error = Error(int(object.get("error", ERR_UNCONFIGURED)));
    if (error != OK) return error;
    if (uint64_t(int64_t(object["schema_id"])) != p_schema->get_schema_id()) return ERR_INVALID_PARAMETER;
    const TypedArray<SuperposField> fields = p_schema->get_fields();
    if (fields.is_empty() || fields.size() > 64) return ERR_INVALID_PARAMETER;
    Dictionary mapping;
    for (int i = 0; i < fields.size(); ++i) {
        const Ref<SuperposField> field = fields[i];
        if (field.is_null()) return ERR_INVALID_PARAMETER;
        const int64_t key = superpos_egp::signed_bits(field->get_field_id());
        if (mapping.has(key)) return ERR_INVALID_PARAMETER;
        mapping[key] = field->get_field_name();
    }
    // bind_fields validates every ID against the object's frozen schema.
    return bind_fields(p_session, p_target, p_handle, mapping);
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
        staged[i] = {uint64_t(int64_t(key)), StringName(value), Vector<StringName>()};
        if ((error = superpos_egp::parse_property_path(String(value), staged[i].path)) != OK) return error;
        for (int j = 0; j < i; ++j) if (superpos_egp::property_paths_overlap(staged[j].path, staged[i].path)) return ERR_INVALID_PARAMETER;
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
        const Variant value = target->get_indexed(captured.mappings[i].path, &valid);
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

void SuperposReplicator::count_capture(const Snapshot &p_snapshot, Error p_error, bool p_published) {
    auto *owner = Object::cast_to<SuperposReplicator>(ObjectDB::get_instance(p_snapshot.self));
    if (!owner || owner->incarnation != p_snapshot.incarnation) return;
    owner->last_capture_error = p_error;
    if (p_error != OK) ++owner->failed_captures;
    else if (p_published) ++owner->published_captures;
    else ++owner->unchanged_captures;
}

Dictionary SuperposReplicator::capture_changes() {
    Snapshot captured;
    Error error = snapshot(captured);
    if (error != OK) return report(error, 0, 0, false);
    Operation operation(captured.self);
    ++captures;
    // Explicit marks are subsumed: every bound field is a candidate.
    const uint64_t marked = dirty;
    dirty = 0;
    auto fail = [&](Error p_error, bool p_interrupted, uint64_t p_revision) {
        restore_dirty(captured, marked);
        count_capture(captured, p_error, false);
        return report(p_error, 0, p_revision, p_interrupted);
    };
    Dictionary values;
    for (uint32_t i = 0; i < captured.count; ++i) {
        if ((error = validate(captured)) != OK) return fail(error, true, 0);
        auto *target = Object::cast_to<Node>(ObjectDB::get_instance(captured.target));
        bool valid = false;
        const Variant value = target->get_indexed(captured.mappings[i].path, &valid);
        // A getter may free either Node, retire its Session, or change bindings.
        if ((error = validate(captured)) != OK) return fail(error, true, 0);
        if (!valid) return fail(ERR_DOES_NOT_EXIST, false, 0);
        values[superpos_egp::signed_bits(captured.mappings[i].field)] = value;
    }
    auto *session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
    const Dictionary diff = session->read_field_changes(captured.handle, values);
    error = Error(int(diff.get("error", ERR_UNCONFIGURED)));
    if (error != OK) return fail(error, false, 0);
    const uint64_t revision = uint64_t(int64_t(diff["revision"]));
    const PackedInt64Array changed = diff["changed"];
    if (changed.is_empty()) {
        count_capture(captured, OK, false);
        Dictionary result = report(OK, 0, revision, false);
        result["changed_fields"] = 0;
        return result;
    }
    Dictionary publication;
    for (int i = 0; i < changed.size(); ++i) publication[changed[i]] = values[changed[i]];
    if ((error = validate(captured)) != OK) return fail(error, true, revision);
    session = Object::cast_to<SuperposSession>(ObjectDB::get_instance(captured.session));
    // Counters precede publication: canonical_published may run arbitrary code.
    count_capture(captured, OK, true);
    error = session->publish_fields(captured.handle, revision, publication);
    if (error != OK) {
        restore_dirty(captured, marked);
        auto *owner = Object::cast_to<SuperposReplicator>(ObjectDB::get_instance(captured.self));
        if (owner && owner->incarnation == captured.incarnation) {
            --owner->published_captures;
            ++owner->failed_captures;
            owner->last_capture_error = error;
        }
    }
    Dictionary result = report(error, error == OK ? uint32_t(changed.size()) : 0, revision, false);
    result["changed_fields"] = changed.size();
    return result;
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
        target->set_indexed(captured.mappings[i].path,
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
    result["automatic_capture"] = automatic;
    result["capture_interval"] = capture_interval;
    result["captures"] = superpos_egp::signed_bits(captures);
    result["published_captures"] = superpos_egp::signed_bits(published_captures);
    result["unchanged_captures"] = superpos_egp::signed_bits(unchanged_captures);
    result["failed_captures"] = superpos_egp::signed_bits(failed_captures);
    result["last_capture_error"] = last_capture_error;
    return result;
}
