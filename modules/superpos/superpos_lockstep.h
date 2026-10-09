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
    Error configure(int64_t p_input_bytes, int64_t p_slots);
    Error record_input(int64_t p_tick, const PackedByteArray &p_input);
    Error acknowledge_inputs(int64_t p_tick);
    PackedByteArray pack_inputs(int64_t p_max_ticks = 60, int64_t p_max_bytes = 880) const;
    Error accept_commands(const PackedByteArray &p_payload);
    Error load_keyframe(const PackedByteArray &p_table);
    int64_t advance_command();
    PackedByteArray get_input(int64_t p_slot) const;
    Dictionary get_status() const;
};

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
    Error set_command(int64_t p_slot, const PackedByteArray &p_input);
    Error end_tick();
    Dictionary pack_commands(int64_t p_after_tick, int64_t p_max_bytes = 880) const;
    PackedByteArray pack_keyframe() const;
    int64_t get_command_tick() const;
};
