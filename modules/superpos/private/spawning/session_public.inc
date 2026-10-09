// SPDX-License-Identifier: MIT
// Include after private/session_fields.inc; uses that exact checked decoder.
// Requires friendship in SuperposSession and NativeReceiver.
namespace { uint64_t next_spawn_binding = 0; }
Dictionary SuperposReceiverPublicAccess::read(SuperposSession &owner,
        uint64_t binding, uint64_t expected_handle,
        const std::array<uint64_t, 6> &identity, const PackedInt64Array *fields) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner.owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner.owner_retired || !owner.impl || !owner.impl->network || !owner.impl->network->receiver) { return result; }
    if (owner.managed_reload_paused || owner.closing || owner.simulation_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!binding || owner.impl->network->spawn_binding != binding || identity[0] > UINT16_MAX) { result["error"] = ERR_INVALID_DATA; return result; }
    const superpos::ReplicaKey key{uint16_t(identity[0]), identity[1], identity[2], identity[3], identity[4], identity[5]};
    auto &store = owner.impl->network->receiver->receiver_;
    if (!store) { return result; }
    const auto record = store->inspect_record(key);
    if (!record) { result["error"] = translate(record.error()); return result; }
    if (record->handle.value != expected_handle) { result["error"] = ERR_INVALID_DATA; return result; }
    result["canonical_ready"] = record->phase == superpos::ReceiverPhase::Ready;
    result["binding_generation"] = superpos_egp::signed_bits(binding);
    result["handle"] = superpos_egp::signed_bits(expected_handle);
    if (!fields) {
        Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime(Object::cast_to<superpos_egp::spawning::SuperposSpawnRuntime>(ObjectDB::get_instance(owner.impl->network->spawn_runtime)));
        if (runtime.is_valid()) result["scene"] = runtime->projection_status(expected_handle, key);
        result["error"] = OK; result["revision"] = superpos_egp::signed_bits(record->current_revision); return result; }
    if (fields->is_empty() || fields->size() > int(superpos::Schema::maximum_fields)) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    const auto image = store->inspect(key);
    if (!image) { result["error"] = translate(image.error()); return result; }
    Dictionary values;
    for (int i = 0; i < fields->size(); ++i) {
        const int64_t raw = (*fields)[i];
        const auto *field = image->schema->field(uint64_t(raw));
        if (!field || values.has(raw)) { result["error"] = ERR_INVALID_PARAMETER; return result; }
        Variant value;
        const Error decoded = decode_engine_field(*image->schema, *field, image->canonical, value);
        if (decoded != OK) { result["error"] = decoded; return result; }
        values[raw] = value;
    }
    result["error"] = OK;
    result["values"] = values;
    result["revision"] = superpos_egp::signed_bits(image->revision);
    result["owner"] = superpos_egp::signed_bits(image->owner);
    result["ownership_revision"] = superpos_egp::signed_bits(image->ownership_revision);
    return result;
}

Error SuperposReceiverPublicAccess::retry(SuperposSession &owner, uint64_t binding, uint64_t handle,
        const std::array<uint64_t, 6> &identity) {
    const Dictionary checked = read(owner, binding, handle, identity, nullptr);
    if (int(checked.get("error", ERR_UNCONFIGURED)) != OK) return Error(int(checked["error"]));
    if (!bool(checked.get("canonical_ready", false))) return ERR_BUSY;
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime(Object::cast_to<superpos_egp::spawning::SuperposSpawnRuntime>(ObjectDB::get_instance(owner.impl->network->spawn_runtime)));
    if (runtime.is_null()) return ERR_UNCONFIGURED;
    return runtime->retry(handle, {uint16_t(identity[0]), identity[1], identity[2], identity[3], identity[4], identity[5]});
}

Error SuperposReceiverPublicAccess::attach(SuperposSession &owner,
        SuperposSpawner &spawner, const Dictionary &configuration) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != owner.owner_thread) return ERR_BUSY;
    const ObjectID spawner_id = spawner.get_instance_id();
    Ref<SuperposSession> keep_alive(&owner);
    if (keep_alive.is_null() || ObjectDB::get_instance(spawner_id) != &spawner) return ERR_UNCONFIGURED;
    if (owner.owner_retired || owner.closing || owner.managed_reload_paused || owner.callbacks_in_flight ||
            owner.simulation_in_flight || !owner.impl || !owner.impl->network || !owner.impl->registry) return ERR_UNCONFIGURED;
    if (spawner.runtime_id.is_valid() || spawner.catalog.is_null() || spawner.session.ptr() != &owner) return ERR_INVALID_PARAMETER;
    static const std::array<StringName, 11> allowed{ "authority_epoch", "connection_epoch", "replica_epoch", "peer",
        "maximum_active", "maximum_transitions", "control_channel", "state_channel", "bulk_channel", "native_bytes", "spawn_timeout_ms" };
    for (int i = 0; i < configuration.size(); ++i) {
        const Variant key = configuration.get_key_at_index(i);
        if (key.get_type() != Variant::STRING && key.get_type() != Variant::STRING_NAME) return ERR_INVALID_PARAMETER;
        if (std::find(allowed.begin(), allowed.end(), StringName(key)) == allowed.end() || configuration[key].get_type() != Variant::INT) return ERR_INVALID_PARAMETER;
    }
    auto access = owner.impl->network->access(); if (!access) return translate(access.error());
    auto session_identity = (*access)->identity(); if (!session_identity) return translate(session_identity.error());
    auto authority = owner.impl->world->world().authority_epoch(); if (!authority) return translate(authority.error());
    auto value = [&](const char *key, uint64_t fallback) { return configuration.has(key) ? uint64_t(int64_t(configuration[key])) : fallback; };
    superpos::ReceiverConfig config;
    config.authority_epoch = value("authority_epoch", *authority);
    config.connection_epoch = value("connection_epoch", session_identity->connection_epoch);
    config.replica_epoch = value("replica_epoch", 1);
    config.peer = value("peer", session_identity->local_peer);
    config.maximum_active = value("maximum_active", 1000);
    config.maximum_transitions = value("maximum_transitions", 64);
    const size_t native_bytes = value("native_bytes", 64u << 20);
    const uint64_t control = value("control_channel", 0), state = value("state_channel", 1), bulk = value("bulk_channel", 2);
    const uint64_t spawn_timeout_ms = value("spawn_timeout_ms", 15000);
    if (!config.authority_epoch || !config.connection_epoch || !config.replica_epoch || !config.peer ||
            !config.maximum_active || config.maximum_active > 1000 || !config.maximum_transitions || config.maximum_transitions > 64 ||
            !native_bytes || native_bytes > (64u << 20) || control >= 32 || state >= 32 || bulk >= 32 ||
            !spawn_timeout_ms || spawn_timeout_ms > 60000) return ERR_INVALID_PARAMETER;
    SuperposSpawnCatalog::Snapshot catalog;
    Ref<SuperposSpawnCatalog> catalog_source = spawner.catalog;
    if (ObjectDB::get_instance(spawner_id) != &spawner || catalog_source.is_null() || spawner.catalog != catalog_source) return ERR_UNCONFIGURED;
    const Error captured = catalog_source->snapshot(catalog); if (captured != OK) return captured;
    if (ObjectDB::get_instance(spawner_id) != &spawner || owner.owner_retired || owner.closing ||
            !owner.impl || !owner.impl->network || !owner.impl->registry) return ERR_UNCONFIGURED;
    config.state_stride = 0;
    for (uint32_t e = 0; e < catalog.entry_count; ++e) {
        const superpos::Schema *schema = nullptr;
        for (const auto &record : owner.impl->records.span()) {
            if (record.occupied && record.schema.id() == catalog.entries[e].schema) { schema = &record.schema; break; }
        }
        if (!schema) return ERR_INVALID_PARAMETER;
        for (uint32_t f = 0; f < catalog.entries[e].field_count; ++f) if (!schema->field(catalog.entries[e].fields[f].id)) return ERR_INVALID_PARAMETER;
        config.state_stride = std::max(config.state_stride, schema->state_bytes());
    }
    // No scene or factory callbacks run during this complete capacity preflight.
    const uint32_t count = catalog.entry_count;
    auto runtime = superpos_egp::spawning::SuperposSpawnRuntime::create(); if (runtime.is_null()) return ERR_OUT_OF_MEMORY;
    const auto receiver_binding = superpos::increment(next_spawn_binding);
    if (!receiver_binding) return translate(receiver_binding.error());
    next_spawn_binding = *receiver_binding;
    const Error initialized = runtime->initialize(superpos_egp::module_backing(), std::move(catalog), spawner, owner,
        *receiver_binding, owner.impl->binding_generation, uint32_t(config.maximum_active + config.maximum_transitions), native_bytes, spawn_timeout_ms);
    if (initialized != OK) return initialized;
    std::array<superpos_egp::lifecycle_engine::Registration, 64> registrations;
    const auto prepared = runtime->registrations(std::span(registrations).first(count));
    if (!prepared) return translate(prepared.error());
    const Error admitted = SuperposNativeReceiverAccess::attach(owner, std::span(registrations).first(count), config,
        {uint8_t(control), uint8_t(state), uint8_t(bulk)}, {config.maximum_active, config.maximum_transitions, 1, native_bytes});
    if (admitted != OK) return admitted;
    if (ObjectDB::get_instance(spawner_id) != &spawner || !owner.impl || owner.owner_retired) {
        runtime->abandon_spawner(); SuperposNativeReceiverAccess::orphan(owner); return ERR_UNCONFIGURED;
    }
    spawner.runtime_id = runtime->get_instance_id();
    owner.impl->network->spawn_runtime = spawner.runtime_id;
    owner.impl->network->spawn_binding = *receiver_binding;
    return OK;
}

Error SuperposReceiverPublicAccess::detach(SuperposSession &owner) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != owner.owner_thread) return ERR_BUSY;
    if (owner.pending_native_retirement) return ERR_BUSY;
    if (!owner.impl || !owner.impl->network || !owner.impl->network->receiver) return OK;
    if (owner.closing || owner.callbacks_in_flight || owner.simulation_in_flight) return ERR_BUSY;
    Ref<SuperposSession> pin(&owner);
    if (pin.is_null()) return ERR_UNCONFIGURED;
    if (owner.pending_native_retirement || owner.closing || owner.callbacks_in_flight ||
            !owner.impl || !owner.impl->network || !owner.impl->network->receiver) return ERR_BUSY;
    auto *owned = owner.impl;
    owner.closing = true;
    struct Closing { SuperposSession &owner; ~Closing() { if (!owner.pending_native_retirement) owner.closing = false; } } closing{owner};
    ++owner.callbacks_in_flight;
    struct Callback { SuperposSession &owner; ~Callback() { owner._end_callback(); } } callback{owner};
    const auto drained = owned->network->receiver->stop();
    if (owner.impl != owned || owner.pending_native_retirement) return ERR_BUSY;
    if (!drained) return translate(drained.error());
    if (!owned->network->receiver->drained()) return ERR_BUSY;
    const auto released = receiver_retirements.release(owned->native_retirement);
    if (!released) return translate(released.error());
    receiver_owners[owned->native_slot] = nullptr;
    owned->network->receiver.reset();
    owned->network->spawn_runtime = ObjectID{};
    owned->network->spawn_binding = 0;
    owned->native_recipient = ObjectID{};
    return OK;
}

void SuperposReceiverPublicAccess::abandon(SuperposSession &owner) noexcept {
    if (Thread::is_main_thread() && owner.impl && owner.impl->network && owner.impl->network->receiver)
        SuperposNativeReceiverAccess::orphan(owner);
}
