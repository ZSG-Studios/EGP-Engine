// SPDX-License-Identifier: MIT
#pragma once
// Public C++17 engine/SDK declaration. Rings are core SnapshotHistory instances
// with a schema-derived interpolation adapter in private C++23 code.
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "superpos_replica_view.h"
#include "superpos_session.h"

// Immutable canonical history of objects of one registered schema, recorded
// from the authority World (lag-compensation queries) or from received
// replicas (presentation smoothing). Queries copy history and never rewind
// the live World or scene. F32, F64 and QuantizedF32 fields interpolate;
// every other field steps from the earlier sample. No extrapolation.
class SuperposStateHistory : public RefCounted {
    GDCLASS(SuperposStateHistory, RefCounted);
    struct State;
    State *state = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
    Error _preflight() const;
protected:
    static void _bind_methods();
public:
    // Default window: 30 retained ticks (500 ms at 60 Hz).
    Error configure(const Ref<SuperposSession> &p_session, uint64_t p_schema_id, const Dictionary &p_configuration = Dictionary());
    // Authority World object: records its current canonical image at its tick.
    Error record_object(uint64_t p_handle);
    // Received replica of the configured client Session at its publication tick.
    Error record_replica(const Ref<SuperposReplicaView> &p_replica);
    Dictionary read_exact(uint64_t p_handle, uint64_t p_tick, const PackedInt64Array &p_fields = PackedInt64Array());
    Dictionary sample(uint64_t p_handle, uint64_t p_tick, double p_fraction, const PackedInt64Array &p_fields = PackedInt64Array());
    Dictionary read_object_history(uint64_t p_handle) const;
    Error forget(uint64_t p_handle);
    Error clear();
    Dictionary read_status() const;
    ~SuperposStateHistory();
};
