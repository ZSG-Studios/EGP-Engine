// SPDX-License-Identifier: MIT
#pragma once
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

// Deterministic-simulation input streaming for an authoritative server. Clients
// record one input per simulation tick and resend every input the server has not
// reported received; the server plays inputs out through an adaptive per-peer jitter
// buffer and relays everyone's consumed inputs as a delta-compressed command
// stream so each client advances the same deterministic world. Payloads travel on
// any SuperposSession channel; an unreliable channel suffices because inputs and
// commands are resent until acknowledged. Owner-thread only.
class SuperposLockstepClient : public RefCounted {
    GDCLASS(SuperposLockstepClient, RefCounted);
    struct Impl;
    Impl *impl = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
protected:
    static void _bind_methods();
public:
    SuperposLockstepClient();
    ~SuperposLockstepClient();
    Error configure(int64_t p_input_bytes, int64_t p_slots, int64_t p_buffer_ticks = 128);
    Error record_input(int64_t p_tick, const PackedByteArray &p_input);
    Error acknowledge_inputs(int64_t p_tick);
    int64_t discard_oldest_inputs(int64_t p_count);
    PackedByteArray pack_inputs(int64_t p_max_ticks = 60, int64_t p_max_bytes = 880) const;
    Error accept_commands(const PackedByteArray &p_payload);
    Error load_keyframe(const PackedByteArray &p_table);
    int64_t advance_command();
    PackedByteArray get_input(int64_t p_slot) const;
    Array get_inputs() const;
    int64_t get_processed_tick() const;
    Dictionary get_status() const;
};

class SuperposSession;

class SuperposLockstepServer : public RefCounted {
    GDCLASS(SuperposLockstepServer, RefCounted);
    struct Impl;
    Impl *impl = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
protected:
    static void _bind_methods();
public:
    SuperposLockstepServer();
    ~SuperposLockstepServer();
    Error configure(int64_t p_input_bytes, int64_t p_slots, const Dictionary &p_playout = Dictionary());
    Error accept_inputs(int64_t p_slot, const PackedByteArray &p_payload);
    Array consume_inputs(int64_t p_slot);
    Dictionary get_playout_status(int64_t p_slot) const;
    Error begin_tick(int64_t p_tick);
    Error set_command(int64_t p_slot, const PackedByteArray &p_input, int64_t p_processed_tick = 0);
    Error end_tick();
    Dictionary pack_commands(int64_t p_after_tick, int64_t p_max_bytes = 880, int64_t p_recipient = -1) const;
    Dictionary pack_command_window(int64_t p_acknowledged_tick, int64_t p_sent_tick, int64_t p_max_bytes = 880, int64_t p_recipient = -1, int64_t p_redundant_ticks = 0) const;
    PackedByteArray pack_keyframe() const;
    void reset_stream(int64_t p_slot, int64_t p_keyframe_tick);
    Dictionary pack_stream(int64_t p_slot, int64_t p_srtt_usec, int64_t p_max_bytes = 880);
    Dictionary get_stream_status(int64_t p_slot) const;
    Dictionary get_streams_summary() const;
    // Native batch service: one call per phase of a server tick instead of
    // per-client script loops.
    void bind_session(int64_t p_slot, const Ref<SuperposSession> &p_session);
    void set_stream_enabled(int64_t p_slot, bool p_enabled);
    void set_stream_interval(int64_t p_slot, int64_t p_ticks);
    int64_t ingest_inputs(int64_t p_channel, int64_t p_max_per_session = 8);
    Array step_commands(int64_t p_tick, const PackedByteArray &p_hold_mask, const PackedByteArray &p_merge_mask);
    Dictionary publish_commands(int64_t p_channel, int64_t p_server_tick, int64_t p_max_bytes = 860, int64_t p_max_waiting = 2);
    int64_t get_command_tick() const;
    int64_t get_oldest_command_tick() const;
    Dictionary get_command_totals() const;
};
