// SPDX-License-Identifier: MIT
#pragma once
#include "scene/main/node.h"
#include "superpos_session.h"

class SuperposWorld : public Node {
    GDCLASS(SuperposWorld, Node);
    friend class SuperposWorldMutation;
    Ref<SuperposSession> session;
    TypedArray<SuperposSchema> schemas;
    uint32_t max_objects = 4096;
    uint64_t authority_epoch = 1;
    uint64_t authority_peer = 0;
    bool automatic_ticks = false;
    bool mutation_in_flight = false;
protected:
    static void _bind_methods();
    void _notification(int p_what);
public:
    // Authored configuration, not a credential or a statement of live authority.
    struct Configuration {
        TypedArray<SuperposSchema> schemas;
        uint32_t max_objects = 4096;
        uint64_t authority_epoch = 1;
        uint64_t authority_peer = 0;
        bool automatic_ticks = false;
    };
    ~SuperposWorld();
    // EGP properties are owner-only editor/scene accessors. Runtime consumers
    // use the checked reads below; these preserve output on rejection.
    void set_schemas(const TypedArray<SuperposSchema> &p_value);
    TypedArray<SuperposSchema> get_schemas() const;
    void set_max_objects(uint32_t p_value);
    uint32_t get_max_objects() const;
    void set_authority_epoch(uint64_t p_value);
    uint64_t get_authority_epoch() const;
    void set_authority_peer(uint64_t p_value);
    uint64_t get_authority_peer() const;
    void set_automatic_ticks(bool p_value);
    bool get_automatic_ticks() const;
    Error query_configuration(Configuration &r_value) const;
    Dictionary read_configuration() const;
    Error query_session(Ref<SuperposSession> &r_value) const;
    // Callback-free native identity; local ownership metadata only.
    Error query_session_identity(uint64_t &r_value) const;
    Dictionary read_session() const;
    Ref<SuperposSession> get_session() const;
    Error configure();
    Error advance_tick();
    Error close_checked();
    void close();
};
