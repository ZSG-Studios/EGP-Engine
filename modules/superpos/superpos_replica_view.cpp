// SPDX-License-Identifier: MIT
#include "superpos_replica_view.h"
#include "private/spawning/receiver_public_access.hpp"
#include "superpos_session.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

void SuperposReceiverPublicAccess::initialize(SuperposReplicaView &p_view,
        SuperposSession &p_session, uint64_t p_binding, uint64_t p_handle,
        const std::array<uint64_t, 6> &p_identity) {
    p_view.session_id = p_session.get_instance_id();
    p_view.binding = p_binding;
    p_view.handle = p_handle;
    p_view.identity = p_identity;
}

Dictionary SuperposReplicaView::read_status() const {
    if (!Thread::is_main_thread()) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    Ref<SuperposSession> owner(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_id)));
    if (owner.is_valid()) { return SuperposReceiverPublicAccess::read(*owner.ptr(), binding, handle, identity, nullptr); }
    Dictionary result; result["error"] = ERR_UNCONFIGURED; return result;
}

Dictionary SuperposReplicaView::read_fields(const PackedInt64Array &p_fields) const {
    if (!Thread::is_main_thread()) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    // The Ref constructor can run reference notifications. The native reader
    // therefore revalidates the binding after this pin has been acquired.
    Ref<SuperposSession> owner(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_id)));
    if (owner.is_valid()) { return SuperposReceiverPublicAccess::read(*owner.ptr(), binding, handle, identity, &p_fields); }
    Dictionary result; result["error"] = ERR_UNCONFIGURED; return result;
}

Error SuperposReplicaView::retry_projection() const {
    if (!Thread::is_main_thread()) return ERR_BUSY;
    Ref<SuperposSession> owner(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_id)));
    return owner.is_valid() ? SuperposReceiverPublicAccess::retry(*owner.ptr(), binding, handle, identity) : ERR_UNCONFIGURED;
}
Error SuperposReplicaView::call_rpc(uint64_t p_rpc_id, const PackedByteArray &p_payload) const {
    if (!Thread::is_main_thread()) { return ERR_BUSY; }
    Ref<SuperposSession> owner(Object::cast_to<SuperposSession>(ObjectDB::get_instance(session_id)));
    if (owner.is_null()) { return ERR_UNCONFIGURED; }
    return SuperposReceiverPublicAccess::call_rpc(*owner.ptr(), binding, handle, identity, p_rpc_id, p_payload);
}

void SuperposReplicaView::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_handle"), &SuperposReplicaView::get_handle);
    ClassDB::bind_method(D_METHOD("get_binding_generation"), &SuperposReplicaView::get_binding_generation);
    ClassDB::bind_method(D_METHOD("retry_projection"), &SuperposReplicaView::retry_projection);
    ClassDB::bind_method(D_METHOD("read_status"), &SuperposReplicaView::read_status);
    ClassDB::bind_method(D_METHOD("read_fields", "fields"), &SuperposReplicaView::read_fields);
    ClassDB::bind_method(D_METHOD("call_rpc", "rpc_id", "payload"), &SuperposReplicaView::call_rpc);
}
