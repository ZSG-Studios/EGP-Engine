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
    result["tick"] = superpos_egp::signed_bits(image->tick);
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
    static const std::array<StringName, 12> allowed{ "authority_epoch", "connection_epoch", "replica_epoch", "peer",
        "maximum_active", "maximum_transitions", "control_channel", "state_channel", "bulk_channel", "native_bytes", "spawn_timeout_ms", "rpc_channel" };
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
    // Typed RPCs use their own reliable ordered application channel.
    const uint64_t rpc_channel = value("rpc_channel", 3);
    if (rpc_channel >= 32 || rpc_channel == control || rpc_channel == state || rpc_channel == bulk ||
            control == state || control == bulk || state == bulk) return ERR_INVALID_PARAMETER;
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
    // Complete RPC capacity (inbox and pre-readiness buffer) before admission.
    auto endpoint = superpos_egp::captured_create<superpos_egp::replication::RpcEndpoint>(owner.impl->allocator, superpos::MemoryDomain::Session, owner.impl->allocator);
    if (!endpoint) return translate(endpoint.error());
    if (auto reserved = (*endpoint)->reserve(uint8_t(rpc_channel)); !reserved) return translate(reserved.error());
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
    owner.impl->network->rpc = std::move(*endpoint);
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
    owned->network->rpc.reset();
    owned->network->spawn_runtime = ObjectID{};
    owned->network->spawn_binding = 0;
    owned->native_recipient = ObjectID{};
    return OK;
}

void SuperposReceiverPublicAccess::abandon(SuperposSession &owner) noexcept {
    if (Thread::is_main_thread() && owner.impl && owner.impl->network && owner.impl->network->receiver)
        SuperposNativeReceiverAccess::orphan(owner);
}

Error SuperposReceiverPublicAccess::call_rpc(SuperposSession &owner, uint64_t binding, uint64_t handle,
        const std::array<uint64_t, 6> &identity, uint64_t rpc_id, const PackedByteArray &payload) {
    if (!Thread::is_main_thread() || Thread::get_caller_id() != owner.owner_thread) return ERR_BUSY;
    if (owner.owner_retired || !owner.impl || !owner.impl->network || !owner.impl->network->receiver || !owner.impl->network->rpc) return ERR_UNCONFIGURED;
    if (owner.managed_reload_paused || owner.closing || owner.simulation_in_flight) return ERR_BUSY;
    if (!binding || owner.impl->network->spawn_binding != binding || identity[0] > UINT16_MAX || !rpc_id) return ERR_INVALID_PARAMETER;
    auto &store = owner.impl->network->receiver->receiver_;
    if (!store) return ERR_UNCONFIGURED;
    const superpos::ReplicaKey key{uint16_t(identity[0]), identity[1], identity[2], identity[3], identity[4], identity[5]};
    const auto record = store->inspect_record(key);
    if (!record) return translate(record.error());
    if (record->handle.value != handle) return ERR_INVALID_DATA;
    if (record->phase != superpos::ReceiverPhase::Ready || !record->catalog) return ERR_BUSY;
    const auto *descriptor = record->catalog->schema.rpc(rpc_id);
    if (!descriptor) return ERR_UNAVAILABLE;
    if (size_t(payload.size()) > descriptor->maximum_payload) return ERR_INVALID_PARAMETER;
    auto local = owner.impl->network->receiver->local_peer();
    if (!local) return translate(local.error());
    // A client is never the authority. Owner calls carry the observed
    // ownership revision; the authority rechecks both at execution.
    if (descriptor->permission == superpos::RpcPermission::Authority) return ERR_UNAUTHORIZED;
    if (descriptor->permission == superpos::RpcPermission::Owner && record->owner != *local) return ERR_UNAUTHORIZED;
    auto access = owner.impl->network->access();
    if (!access) return translate(access.error());
    const superpos_egp::replication::RpcFrame frame{rpc_id, record->handle, record->ownership_revision, byte_view(payload)};
    auto sent = owner.impl->network->rpc->send(*access->operator->(), owner.impl->tick, frame);
    return sent ? OK : translate(sent.error());
}

Array SuperposReceiverPublicAccess::take_rpcs(SuperposSession &owner, uint32_t maximum) {
    Array result;
    if (!Thread::is_main_thread() || Thread::get_caller_id() != owner.owner_thread) return result;
    if (owner.owner_retired || owner.closing || !owner.impl || !owner.impl->network || !owner.impl->network->receiver || !owner.impl->network->rpc) return result;
    auto &endpoint = *owner.impl->network->rpc;
    auto &inbox = endpoint.inbox();
    while (inbox.size() && uint32_t(result.size()) < std::min<uint32_t>(maximum, 64)) {
        const auto frame = inbox.frame(0);
        // Execution-time recheck: the replica must still be Ready.
        auto record = owner.impl->network->receiver->find_handle(frame.handle);
        if (!record || record->phase != superpos::ReceiverPhase::Ready) {
            ++endpoint.totals().rejected_stale;
            inbox.remove(0);
            continue;
        }
        PackedByteArray payload;
        if (payload.resize(int64_t(frame.payload.size())) != OK) break;
        if (!frame.payload.empty()) memcpy(payload.ptrw(), frame.payload.data(), frame.payload.size());
        Dictionary call;
        call["handle"] = superpos_egp::signed_bits(frame.handle.value);
        call["rpc_id"] = superpos_egp::signed_bits(frame.rpc);
        call["ownership_revision"] = superpos_egp::signed_bits(frame.ownership_revision);
        call["payload"] = payload;
        result.append(call);
        ++endpoint.totals().taken;
        inbox.remove(0);
    }
    return result;
}

Dictionary rpc_totals_dictionary(const superpos_egp::replication::RpcEndpoint &endpoint) {
    const auto &totals = endpoint.totals();
    Dictionary result;
    result["channel"] = int64_t(endpoint.channel());
    result["inbox"] = int64_t(endpoint.inbox().size());
    result["inbox_bytes"] = int64_t(endpoint.inbox().bytes());
    result["pre_readiness"] = int64_t(endpoint.pending().size());
    result["pre_readiness_bytes"] = int64_t(endpoint.pending().bytes());
    result["sent"] = superpos_egp::signed_bits(totals.sent);
    result["send_refused"] = superpos_egp::signed_bits(totals.send_refused);
    result["retired"] = superpos_egp::signed_bits(totals.retired);
    result["received"] = superpos_egp::signed_bits(totals.received);
    result["delivered"] = superpos_egp::signed_bits(totals.delivered);
    result["buffered"] = superpos_egp::signed_bits(totals.buffered);
    result["buffer_overflow"] = superpos_egp::signed_bits(totals.buffer_overflow);
    result["expired"] = superpos_egp::signed_bits(totals.expired);
    result["dropped"] = superpos_egp::signed_bits(totals.dropped);
    result["taken"] = superpos_egp::signed_bits(totals.taken);
    result["rejected_permission"] = superpos_egp::signed_bits(totals.rejected_permission);
    result["rejected_stale"] = superpos_egp::signed_bits(totals.rejected_stale);
    result["rejected_unsupported"] = superpos_egp::signed_bits(totals.rejected_unsupported);
    result["failure"] = int(endpoint.failure());
    return result;
}

Dictionary SuperposReceiverPublicAccess::rpc_status(SuperposSession &owner) {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner.owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (!owner.impl || !owner.impl->network || !owner.impl->network->rpc) return result;
    result = rpc_totals_dictionary(*owner.impl->network->rpc);
    result["error"] = OK;
    return result;
}
