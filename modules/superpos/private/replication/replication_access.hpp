// SPDX-License-Identifier: MIT
#pragma once
// Private module bridge between the public SuperposReplicationServer and the
// SuperposSession implementation. Implemented in superpos_session.cpp
// (private/replication/session_replication.inc), where Session::Impl is
// complete. Owner/main thread only; no user callbacks.
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include <cstdint>

class SuperposSession;
class SuperposReplicationServer;
struct SuperposReplicationAccess {
    static bool idle(const SuperposSession &);
    static void *bound_publisher(SuperposSession &link, uint64_t incarnation);
    enum class Policy { Region, Dormant, Weight, Group };
    static Error set_policy(SuperposReplicationServer &, uint64_t handle, Policy, uint64_t value);
    static Dictionary read_policy(SuperposReplicationServer &, uint64_t handle);
    static Error set_interest(SuperposReplicationServer &, SuperposSession &link, uint64_t incarnation, const PackedInt64Array &regions, bool filter);
    // Frees the relevance state; safe on any thread once no publish runs.
    static void release(SuperposReplicationServer &) noexcept;
    static Error configure(SuperposReplicationServer &, SuperposSession &world, const Dictionary &);
    static Error attach(SuperposReplicationServer &, SuperposSession &link, const Dictionary &, uint64_t &r_incarnation);
    static Error detach(SuperposSession &link, uint64_t incarnation);
    static Dictionary publish(SuperposReplicationServer &);
    static Dictionary status(SuperposSession &link, uint64_t incarnation);
    static Error send_rpc(SuperposReplicationServer &, SuperposSession &link, uint64_t incarnation, uint64_t handle, uint64_t rpc_id, const PackedByteArray &payload);
    static Array take_rpcs(SuperposReplicationServer &, SuperposSession &link, uint64_t incarnation, uint32_t maximum);
};
