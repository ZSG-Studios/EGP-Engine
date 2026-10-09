// SPDX-License-Identifier: MIT
#pragma once
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "superpos_schema.h"
#include "superpos_simulation_provider.h"
#include "superpos_managed_reload.h"

class SuperposSpawner;
class SuperposReceiverPublicAccess;

class SuperposSession : public RefCounted {
    GDCLASS(SuperposSession, RefCounted);
    struct Impl;
    Impl *impl = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
    Error last_error = OK;
    friend class SuperposWorld;
    friend struct SuperposRtcBindingAccess;
    friend struct SuperposNativeReceiverAccess;
    friend class SuperposReceiverPublicAccess;
    void *pending_native_retirement = nullptr;
    friend class SuperposSimulationScope;
    friend class SuperposManagedReload;
    uint64_t reload_world_owner = 0, managed_reload_epoch = 0;
    uint32_t callbacks_in_flight = 0;
    bool managed_reload_paused = false, closing = false;
    bool simulation_in_flight = false;
    bool simulation_tick_phase = false;
    Error _simulation_preflight(uint64_t binding, uint64_t epoch, bool registered) const;
    bool _simulation_gate_ready(uint64_t binding, uint64_t epoch) const;
    // Irreversible native retirement is separate from managed wrapper disposal.
    bool owner_retired = false, retirement_finished = false;
    void _retire_world_owner(uint64_t p_owner);
    void _finish_retirement();
    void _end_callback();
    SuperposManagedReload::DisposalNode managed_disposal;
    Error _reload_preflight() const;
    void _reload_pause(uint64_t p_epoch);
    Error _reload_validate(const SuperposManagedReload::Ticket &p_ticket) const;
    void _reload_resume(uint64_t p_epoch);
    void _reload_abort_close();
    String _reload_schema_fingerprint() const;
    String _reload_authored_fingerprint() const;
    uint64_t physics_owner = 0;
    Error advance_owned_tick(uint64_t p_owner, uint64_t p_physics_frame);
    Error set_physics_owner(uint64_t p_owner);
    Error step_tick();
protected:
    static void _bind_methods();
public:
    Error attach_receiver(SuperposSpawner *p_spawner, const Dictionary &p_configuration);
    Error detach_receiver();
    Dictionary read_receiver_status() const;
    SuperposSession();
    ~SuperposSession();
    Error configure(const TypedArray<SuperposSchema> &p_schemas, uint32_t p_max_objects = 4096,
        uint64_t p_authority_epoch = 1, uint64_t p_authority_peer = 0, uint64_t p_state_budget = 268435456);
    Error close_checked();
    void close();
    String get_state() const;
    Error get_last_error() const;
    // Local checked C++ access preserves its output on failure. The bound
    // Dictionary contains value only after successful owner/world validation.
    Error query_tick(uint64_t &r_value) const;
    Dictionary read_tick() const;
    // No value is present when the live Core authority query fails.
    Dictionary read_authority_epoch() const;
    Error query_binding_generation(uint64_t &r_value) const;
    Dictionary read_binding_generation() const;
    Error advance_tick();
    // Native providers are compiled into this engine module. Registration is
    // intentionally unbound: no script callbacks qualify replay capabilities.
    Error register_native_simulation_checked(const Ref<SuperposSimulationProvider> &provider,
        uint64_t binding, uint64_t epoch, uint32_t history_ticks, uint32_t required_history_ticks,
        uint64_t initial_tick, uint64_t revision, const PackedByteArray &initial, uint64_t history_budget);
    Error predict_checked(uint64_t binding, uint64_t epoch, uint64_t tick, const PackedByteArray &input);
    Dictionary reconcile_checked(uint64_t binding, uint64_t epoch, uint64_t tick,
        uint64_t revision, const PackedByteArray &canonical);
    Error copy_prediction_checked(uint64_t binding, uint64_t epoch, PackedByteArray &output, uint64_t &tick);
    Dictionary read_prediction(uint64_t binding, uint64_t epoch);
    Dictionary read_prediction_info(uint64_t binding, uint64_t epoch);
    Dictionary read_simulation_profile() const;
    // One pre-provisioned, connected native UDP association. The key is never
    // retained in a Resource property or exposed by a getter.
    Error configure_udp(bool p_server, const String &p_local_address, uint32_t p_local_port,
        const String &p_remote_address, uint32_t p_remote_port, uint64_t p_session_id,
        uint64_t p_peer_identity, const PackedByteArray &p_admission_key, const Dictionary &p_transport = Dictionary());
    Dictionary enqueue_packet(const PackedByteArray &p_payload, uint32_t p_channel = 0);
    Dictionary read_packet(uint32_t p_channel = 0) const;
    Error acknowledge_packet(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel = 0);
    Dictionary get_packet_outcome(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel = 0) const;
    Error retire_packet(uint64_t p_message, uint64_t p_binding_generation, uint32_t p_channel = 0);
    uint64_t spawn_object(uint64_t p_schema, uint64_t p_owner, const PackedByteArray &p_canonical);
    Error destroy_object(uint64_t p_handle);
    Dictionary read_object(uint64_t p_handle) const;
    // Field IDs carry unsigned bit patterns. Reads use the frozen native schema;
    // no property getters, resource hooks or gameplay methods execute here.
    Dictionary read_fields(uint64_t p_handle, const PackedInt64Array &p_fields) const;
    Error publish_fields(uint64_t p_handle, uint64_t p_expected_revision, const Dictionary &p_values);
    Error publish_packed(const PackedByteArray &p_operations);
    Error transfer_ownership(uint64_t p_handle, uint64_t p_owner, uint64_t p_expected_revision);
    Dictionary get_statistics() const;
    Dictionary get_admission_state() const;
    // Direct C++ access uses identical native implementation. No C++23 core types
    // appear in this header or in generated godot-cpp consumer headers.
};
