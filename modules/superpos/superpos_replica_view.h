// SPDX-License-Identifier: MIT
#pragma once
#include "core/io/resource.h"
#include "core/variant/dictionary.h"
#include <array>

class SuperposSpawner;
class SuperposReceiverPublicAccess;

// An immutable, weak reference to received canonical state. It never indexes
// the authoritative World. Old references remain stale after detach/rebind,
// incarnation reuse, ownership encoding reset or migration to another epoch.
class SuperposReplicaView : public Resource {
    GDCLASS(SuperposReplicaView, Resource);
    friend class SuperposSpawner;
    friend class SuperposReceiverPublicAccess;
    ObjectID session_id;
    uint64_t binding = 0, handle = 0;
    std::array<uint64_t, 6> identity{};
protected:
    static void _bind_methods();
public:
    uint64_t get_handle() const { return handle; }
    uint64_t get_binding_generation() const { return binding; }
    Dictionary read_status() const;
    Error retry_projection() const;
    Dictionary read_fields(const PackedInt64Array &p_fields) const;
    // Sends a registered schema RPC to the authority for this replica.
    Error call_rpc(uint64_t p_rpc_id, const PackedByteArray &p_payload) const;
};
