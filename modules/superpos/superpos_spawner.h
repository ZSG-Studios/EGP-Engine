// SPDX-License-Identifier: MIT
#pragma once
#include "scene/main/node.h"
#include "superpos_spawn_catalog.h"
#include "superpos_replica_view.h"
#include "superpos_session.h"

class SuperposSpawner : public Node {
    GDCLASS(SuperposSpawner, Node);
    friend class SuperposReceiverPublicAccess;
    Ref<SuperposSession> session;
    Ref<SuperposSpawnCatalog> catalog;
    ObjectID runtime_id;
    uint32_t projection_budget = 32;
protected:
    static void _bind_methods();
    void _notification(int p_what);
public:
    void set_session(const Ref<SuperposSession> &p_session);
    Ref<SuperposSession> get_session() const { return session; }
    void set_catalog(const Ref<SuperposSpawnCatalog> &p_catalog);
    Ref<SuperposSpawnCatalog> get_catalog() const { return catalog; }
    void set_projection_budget(uint32_t p_budget);
    uint32_t get_projection_budget() const { return projection_budget; }
    Error start(const Dictionary &p_receiver_config);
    Error stop();
    Dictionary project_pending();
    Dictionary read_status() const;
    // At most 64 immutable weak views per explicit call. No implicit scene
    // scan or global World-handle lookup occurs on the receive path.
    TypedArray<SuperposReplicaView> get_replicas(uint32_t p_offset = 0, uint32_t p_limit = 64) const;
    ~SuperposSpawner();
};
