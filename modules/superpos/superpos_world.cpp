// SPDX-License-Identifier: MIT
#include "superpos_world.h"
#include "u64_bits.h"
#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "core/os/thread.h"
#include <utility>

// Only the native owner can destroy or look up this Node. A local Session
// pin can execute lifecycle callbacks after a method returns; this guard must
// never retain a reference into an already-freed World.
class SuperposWorldMutation {
    ObjectID identity;
public:
    explicit SuperposWorldMutation(SuperposWorld *world) : identity(world->get_instance_id()) {}
    bool alive() const { return Object::cast_to<SuperposWorld>(ObjectDB::get_instance(identity)) != nullptr; }
    ~SuperposWorldMutation() {
        auto *world = Object::cast_to<SuperposWorld>(ObjectDB::get_instance(identity));
        if (world) { world->mutation_in_flight = false; }
    }
};

SuperposWorld::~SuperposWorld() {
    // Worker Node destruction is outside this owner-only contract. Ordinary
    // checked close may refuse; permanent destruction cannot leave ownership
    // pointing at this Node or wait for another physics frame/reload.
    ERR_FAIL_COND(!Thread::is_main_thread());
    const uint64_t owner_identity = uint64_t(get_instance_id());
    mutation_in_flight = true;
    set_physics_process(false);
    // Ref's move constructor transfers without a reference callback. Revoke
    // before a local unreference can enter any managed lifecycle code.
    Ref<SuperposSession> previous(std::move(session));
    if (previous.is_valid()) { previous->_retire_world_owner(owner_identity); }
}

void SuperposWorld::set_schemas(const TypedArray<SuperposSchema> &p_value) {
    if (!Thread::is_main_thread() || mutation_in_flight) { return; }
    schemas = p_value;
}
TypedArray<SuperposSchema> SuperposWorld::get_schemas() const {
    if (!Thread::is_main_thread() || mutation_in_flight) { return {}; }
    return schemas;
}
void SuperposWorld::set_max_objects(uint32_t p_value) {
    if (!Thread::is_main_thread() || mutation_in_flight) { return; }
    max_objects = p_value;
}
uint32_t SuperposWorld::get_max_objects() const {
    if (!Thread::is_main_thread() || mutation_in_flight) { return 0; }
    return max_objects;
}
void SuperposWorld::set_authority_epoch(uint64_t p_value) {
    if (!Thread::is_main_thread() || mutation_in_flight) { return; }
    authority_epoch = p_value;
}
uint64_t SuperposWorld::get_authority_epoch() const {
    if (!Thread::is_main_thread() || mutation_in_flight) { return 0; }
    return authority_epoch;
}
void SuperposWorld::set_authority_peer(uint64_t p_value) {
    if (!Thread::is_main_thread() || mutation_in_flight) { return; }
    authority_peer = p_value;
}
uint64_t SuperposWorld::get_authority_peer() const {
    if (!Thread::is_main_thread() || mutation_in_flight) { return 0; }
    return authority_peer;
}
void SuperposWorld::set_automatic_ticks(bool p_value) {
    if (!Thread::is_main_thread() || mutation_in_flight) { return; }
    if (session.is_valid() && (session->closing || session->managed_reload_paused || session->callbacks_in_flight)) { return; }
    automatic_ticks = p_value;
    if (session.is_valid()) { session->set_physics_owner(p_value ? uint64_t(get_instance_id()) : 0); }
    set_physics_process(p_value && session.is_valid());
}
bool SuperposWorld::get_automatic_ticks() const {
    if (!Thread::is_main_thread() || mutation_in_flight) { return false; }
    return automatic_ticks;
}
Error SuperposWorld::query_configuration(Configuration &r_value) const {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight) { return ERR_BUSY; }
    Configuration value;
    value.schemas = schemas;
    value.max_objects = max_objects;
    value.authority_epoch = authority_epoch;
    value.authority_peer = authority_peer;
    value.automatic_ticks = automatic_ticks;
    r_value = value;
    return OK;
}
Dictionary SuperposWorld::read_configuration() const {
    Dictionary result;
    Configuration configuration;
    const Error error = query_configuration(configuration);
    result["error"] = error;
    if (error != OK) { return result; }
    Dictionary value;
    value["schemas"] = configuration.schemas;
    value["max_objects"] = configuration.max_objects;
    value["authority_epoch"] = superpos_egp::signed_bits(configuration.authority_epoch);
    value["authority_peer"] = superpos_egp::signed_bits(configuration.authority_peer);
    value["automatic_ticks"] = configuration.automatic_ticks;
    result["value"] = value;
    return result;
}
Error SuperposWorld::query_session(Ref<SuperposSession> &r_value) const {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight) { return ERR_BUSY; }
    if (session.is_null()) { return ERR_UNCONFIGURED; }
    if (session->closing) { return ERR_BUSY; }
    const ObjectID identity = get_instance_id();
    Ref<SuperposSession> value = session;
    if (ObjectDB::get_instance(identity) != this || value->owner_retired) { return ERR_UNCONFIGURED; }
    // Commit without reference callbacks or writes through a source
    // member. Old caller-owned output cleanup follows this boundary.
    Ref<SuperposSession> previous(std::move(r_value));
    r_value = std::move(value);
    return OK;
}
Error SuperposWorld::query_session_identity(uint64_t &r_value) const {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight || (session.is_valid() && session->closing)) { return ERR_BUSY; }
    if (session.is_null() || session->owner_retired) { return ERR_UNCONFIGURED; }
    r_value = uint64_t(session->get_instance_id());
    return OK;
}
Dictionary SuperposWorld::read_session() const {
    Dictionary result;
    Ref<SuperposSession> value;
    const Error error = query_session(value);
    result["error"] = error;
    if (error == OK) { result["value"] = value; }
    return result;
}
Ref<SuperposSession> SuperposWorld::get_session() const {
    Ref<SuperposSession> value;
    query_session(value);
    return value;
}
Error SuperposWorld::configure() {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight) { return ERR_BUSY; }
    if (session.is_valid()) { return ERR_ALREADY_IN_USE; }
    mutation_in_flight = true;
    Ref<SuperposSession> candidate;
    SuperposWorldMutation mutation(this);
    Configuration authored;
    authored.schemas = schemas; authored.max_objects = max_objects;
    authored.authority_epoch = authority_epoch; authored.authority_peer = authority_peer;
    authored.automatic_ticks = automatic_ticks;
    const uint64_t owner_identity = uint64_t(get_instance_id());
    candidate.instantiate();
    if (!mutation.alive()) { return ERR_UNAVAILABLE; }
    Error result = candidate->configure(authored.schemas, authored.max_objects, authored.authority_epoch, authored.authority_peer);
    if (!mutation.alive()) {
        if (result == OK) { candidate->_retire_world_owner(0); }
        return ERR_UNAVAILABLE;
    }
    if (result == OK) {
        candidate->reload_world_owner = owner_identity;
        session = candidate;
        if (!mutation.alive()) { return ERR_UNAVAILABLE; }
        if (automatic_ticks) { session->set_physics_owner(uint64_t(get_instance_id())); }
        set_physics_process(automatic_ticks);
    }
    return result;
}
Error SuperposWorld::advance_tick() {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight) { return ERR_BUSY; }
    if (session.is_valid() && (session->closing || session->callbacks_in_flight)) { return ERR_BUSY; }
    if (automatic_ticks) { return ERR_BUSY; }
    Ref<SuperposSession> active = session;
    return active.is_valid() ? active->advance_tick() : ERR_UNCONFIGURED;
}
Error SuperposWorld::close_checked() {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    if (mutation_in_flight) { return ERR_BUSY; }
    if (session.is_valid() && (session->closing || session->managed_reload_paused || session->callbacks_in_flight)) { return ERR_BUSY; }
    mutation_in_flight = true;
    Ref<SuperposSession> previous;
    Ref<SuperposSession> detached;
    // Clear the mutation barrier before releasing the last local pin. Its
    // owner-side binding callback can then observe only the completed close.
    SuperposWorldMutation mutation(this);
    previous = session;
    if (!mutation.alive()) { return ERR_UNAVAILABLE; }
    if (previous.is_valid()) {
        const Error error = previous->close_checked();
        if (error != OK) { return error; }
        if (!mutation.alive()) { return ERR_UNAVAILABLE; }
        if (session.ptr() != previous.ptr()) { return ERR_ALREADY_IN_USE; }
    }
    // Empty local destination: move detaches without a native reference
    // callback or a post-callback write into this Node's member.
    detached = std::move(session);
    set_physics_process(false);
    return OK;
}
void SuperposWorld::close() { close_checked(); }
void SuperposWorld::_notification(int p_what) {
    if (!Thread::is_main_thread()) { return; }
    if (mutation_in_flight) { return; }
    if (p_what == NOTIFICATION_ENTER_TREE) { set_physics_process(automatic_ticks && session.is_valid()); }
    if (p_what == NOTIFICATION_PHYSICS_PROCESS && automatic_ticks) {
        const uint64_t owner_identity = uint64_t(get_instance_id());
        Ref<SuperposSession> active = session;
        if (active.is_valid()) { active->advance_owned_tick(owner_identity, Engine::get_singleton()->get_physics_frames()); }
    }
    if (p_what == NOTIFICATION_EXIT_TREE) {
        // Node predelete exits the tree before the native destructor. Keep the
        // owning member attached while closing canonical/transport state, so
        // that destructor can permanently retire every externally retained
        // Session. Explicit World close remains the operation that detaches.
        // Disable dispatch before callbacks; never access this after the pin.
        set_physics_process(false);
        Ref<SuperposSession> active = session;
        if (active.is_valid()) { active->close_checked(); }
    }
}
void SuperposWorld::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_schemas", "schemas"), &SuperposWorld::set_schemas);
    ClassDB::bind_method(D_METHOD("get_schemas"), &SuperposWorld::get_schemas);
    ClassDB::bind_method(D_METHOD("set_max_objects", "value"), &SuperposWorld::set_max_objects);
    ClassDB::bind_method(D_METHOD("get_max_objects"), &SuperposWorld::get_max_objects);
    ClassDB::bind_method(D_METHOD("set_authority_epoch", "value"), &SuperposWorld::set_authority_epoch);
    ClassDB::bind_method(D_METHOD("get_authority_epoch"), &SuperposWorld::get_authority_epoch);
    ClassDB::bind_method(D_METHOD("set_authority_peer", "value"), &SuperposWorld::set_authority_peer);
    ClassDB::bind_method(D_METHOD("get_authority_peer"), &SuperposWorld::get_authority_peer);
    ClassDB::bind_method(D_METHOD("set_automatic_ticks", "enabled"), &SuperposWorld::set_automatic_ticks);
    ClassDB::bind_method(D_METHOD("get_automatic_ticks"), &SuperposWorld::get_automatic_ticks);
    ClassDB::bind_method(D_METHOD("get_session"), &SuperposWorld::get_session);
    ClassDB::bind_method(D_METHOD("read_session"), &SuperposWorld::read_session);
    ClassDB::bind_method(D_METHOD("read_configuration"), &SuperposWorld::read_configuration);
    ClassDB::bind_method(D_METHOD("configure"), &SuperposWorld::configure);
    ClassDB::bind_method(D_METHOD("advance_tick"), &SuperposWorld::advance_tick);
    ClassDB::bind_method(D_METHOD("close_checked"), &SuperposWorld::close_checked);
    ClassDB::bind_method(D_METHOD("close"), &SuperposWorld::close);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "schemas", PROPERTY_HINT_ARRAY_TYPE, "SuperposSchema"), "set_schemas", "get_schemas");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_objects", PROPERTY_HINT_RANGE, "1,100000,1"), "set_max_objects", "get_max_objects");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "authority_epoch"), "set_authority_epoch", "get_authority_epoch");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "authority_peer"), "set_authority_peer", "get_authority_peer");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "automatic_ticks"), "set_automatic_ticks", "get_automatic_ticks");
}
