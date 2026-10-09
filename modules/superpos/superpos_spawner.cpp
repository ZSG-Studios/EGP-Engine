// SPDX-License-Identifier: MIT
#include "superpos_spawner.h"
#include "private/spawning/spawn_runtime.hpp"
#include "private/spawning/receiver_public_access.hpp"
#include "core/object/class_db.h"
#include "core/os/thread.h"

namespace {
Ref<superpos_egp::spawning::SuperposSpawnRuntime> resolve_runtime(ObjectID identity) {
    return Ref<superpos_egp::spawning::SuperposSpawnRuntime>(Object::cast_to<superpos_egp::spawning::SuperposSpawnRuntime>(ObjectDB::get_instance(identity)));
}
}
void SuperposSpawner::set_session(const Ref<SuperposSession> &value) {
    ERR_FAIL_COND(!Thread::is_main_thread() || runtime_id.is_valid()); session = value;
}
void SuperposSpawner::set_catalog(const Ref<SuperposSpawnCatalog> &value) {
    ERR_FAIL_COND(!Thread::is_main_thread() || runtime_id.is_valid()); catalog = value;
}
void SuperposSpawner::set_projection_budget(uint32_t value) {
    ERR_FAIL_COND(!Thread::is_main_thread() || value == 0 || value > 64); projection_budget = value;
}
Error SuperposSpawner::start(const Dictionary &configuration) {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    if (runtime_id.is_valid()) return ERR_ALREADY_IN_USE;
    if (session.is_null() || catalog.is_null() || !is_inside_tree()) return ERR_UNCONFIGURED;
    const ObjectID self = get_instance_id();
    const Error result = SuperposReceiverPublicAccess::attach(*session.ptr(), *this, configuration);
    if (ObjectDB::get_instance(self) != this) return ERR_UNCONFIGURED;
    if (result == OK) set_process(true);
    return result;
}
Error SuperposSpawner::stop() {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    if (runtime.is_null()) { runtime_id = ObjectID{}; set_process(false); return OK; }
    runtime->request_stop();
    const ObjectID self = get_instance_id();
    const Error result = session.is_valid() ? SuperposReceiverPublicAccess::detach(*session.ptr()) : OK;
    if (ObjectDB::get_instance(self) != this) return ERR_UNCONFIGURED;
    if (result == OK) { runtime_id = ObjectID{}; set_process(false); }
    return result;
}
Dictionary SuperposSpawner::project_pending() {
    if (!Thread::is_main_thread()) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    if (runtime.is_valid()) return runtime->project(projection_budget);
    Dictionary result; result["error"] = ERR_UNCONFIGURED; return result;
}
Dictionary SuperposSpawner::read_status() const {
    if (!Thread::is_main_thread()) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    if (runtime.is_valid()) return runtime->status();
    Dictionary result; result["error"] = ERR_UNCONFIGURED; return result;
}
TypedArray<SuperposReplicaView> SuperposSpawner::get_replicas(uint32_t offset, uint32_t limit) const {
    if (!Thread::is_main_thread()) return {};
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    return runtime.is_valid() ? runtime->views(offset, limit) : TypedArray<SuperposReplicaView>{};
}
void SuperposSpawner::_notification(int what) {
    if (what != NOTIFICATION_PROCESS) return;
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    if (runtime.is_null()) { runtime_id = ObjectID{}; set_process(false); return; }
    if (bool(runtime->status()["stopping"])) (void)stop();
    else (void)runtime->project(projection_budget);
}
SuperposSpawner::~SuperposSpawner() {
    // No scene destruction or user callbacks here. Main-thread abandonment
    // transfers Session custody to the existing native retirement queue.
    if (!Thread::is_main_thread()) return;
    Ref<superpos_egp::spawning::SuperposSpawnRuntime> runtime = resolve_runtime(runtime_id);
    if (runtime.is_valid()) {
        runtime->abandon_spawner();
        if (session.is_valid()) SuperposReceiverPublicAccess::abandon(*session.ptr());
    }
}
void SuperposSpawner::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_session", "value"), &SuperposSpawner::set_session);
    ClassDB::bind_method(D_METHOD("get_session"), &SuperposSpawner::get_session);
    ClassDB::bind_method(D_METHOD("set_catalog", "value"), &SuperposSpawner::set_catalog);
    ClassDB::bind_method(D_METHOD("get_catalog"), &SuperposSpawner::get_catalog);
    ClassDB::bind_method(D_METHOD("set_projection_budget", "value"), &SuperposSpawner::set_projection_budget);
    ClassDB::bind_method(D_METHOD("get_projection_budget"), &SuperposSpawner::get_projection_budget);
    ClassDB::bind_method(D_METHOD("start", "receiver_config"), &SuperposSpawner::start);
    ClassDB::bind_method(D_METHOD("stop"), &SuperposSpawner::stop);
    ClassDB::bind_method(D_METHOD("project_pending"), &SuperposSpawner::project_pending);
    ClassDB::bind_method(D_METHOD("read_status"), &SuperposSpawner::read_status);
    ClassDB::bind_method(D_METHOD("get_replicas", "offset", "limit"), &SuperposSpawner::get_replicas, DEFVAL(0), DEFVAL(64));
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "session", PROPERTY_HINT_RESOURCE_TYPE, "SuperposSession"), "set_session", "get_session");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "catalog", PROPERTY_HINT_RESOURCE_TYPE, "SuperposSpawnCatalog"), "set_catalog", "get_catalog");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "projection_budget", PROPERTY_HINT_RANGE, "1,64,1"), "set_projection_budget", "get_projection_budget");
}
