// SPDX-License-Identifier: MIT
#pragma once
// Public C++17 engine/SDK declaration. Private C++23 replication types stay in
// private/replication and are never visible to generated consumers.
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"
#include "superpos_session.h"

// Authority-side fan-out of one canonical World Session to admitted link
// Sessions (one per client, any authenticated carrier). Each link runs the
// core sender store and replication bridge; all work is native, bounded and
// callback-free. References are weak native object identities.
class SuperposReplicationServer : public RefCounted {
    GDCLASS(SuperposReplicationServer, RefCounted);
    friend struct SuperposReplicationAccess;
public:
    static constexpr uint32_t maximum_peers = 256;
private:
    struct Peer {
        ObjectID link;
        uint64_t incarnation = 0;
    };
    // Private native relevance state (attributes per World slot, RegionIndex).
    struct Relevance;
    Relevance *relevance = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
    ObjectID world;
    uint64_t world_instance = 0, world_epoch = 0, publishes = 0;
    Peer peers[maximum_peers];
    uint32_t peer_count = 0, peer_limit = 32, region_limit = 256, region_members = 1000;
    bool publishing = false;
    Error last_error = OK;
    void _remove_peer(uint32_t p_index);
protected:
    static void _bind_methods();
public:
    Error configure(const Ref<SuperposSession> &p_world, const Dictionary &p_configuration = Dictionary());
    Error attach_peer(const Ref<SuperposSession> &p_link, const Dictionary &p_configuration = Dictionary());
    Error detach_peer(const Ref<SuperposSession> &p_link);
    // Call once per frame after the World's canonical writes and before the
    // link Sessions advance. Queues bounded lifecycle/state/repair work.
    Dictionary publish();
    Dictionary read_peer_status(const Ref<SuperposSession> &p_link) const;
    // Typed schema RPCs. send_rpc targets one World object on one link;
    // read_rpcs returns client calls authorized at this call (execution).
    Error send_rpc(const Ref<SuperposSession> &p_link, uint64_t p_handle, uint64_t p_rpc_id, const PackedByteArray &p_payload);
    Array read_rpcs(const Ref<SuperposSession> &p_link, uint32_t p_maximum = 16);
    // Relevance and publication policy. Region 0 is global (every link);
    // a link with an interest filter sees region 0 plus its interest regions.
    Error set_object_region(uint64_t p_handle, uint32_t p_region);
    Error set_object_dormant(uint64_t p_handle, bool p_dormant);
    Error set_object_priority(uint64_t p_handle, uint32_t p_weight);
    Error set_object_group(uint64_t p_handle, uint64_t p_group);
    Dictionary read_object_policy(uint64_t p_handle) const;
    Error set_peer_interest(const Ref<SuperposSession> &p_link, const PackedInt64Array &p_regions, bool p_filter = true);
    Dictionary read_status() const;
    Error get_last_error() const;
    void close();
    ~SuperposReplicationServer();
};
