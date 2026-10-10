// SPDX-License-Identifier: MIT
#include "superpos_replication_server.h"
#include "private/replication/replication_access.hpp"
#include "core/object/class_db.h"
#include "u64_bits.h"

void SuperposReplicationServer::_remove_peer(uint32_t p_index) {
    ERR_FAIL_UNSIGNED_INDEX(p_index, peer_count);
    for (uint32_t i = p_index + 1; i < peer_count; ++i) { peers[i - 1] = peers[i]; }
    peers[--peer_count] = Peer();
}

Error SuperposReplicationServer::configure(const Ref<SuperposSession> &p_world, const Dictionary &p_configuration) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_world.is_null()) { return last_error = ERR_INVALID_PARAMETER; }
    return last_error = SuperposReplicationAccess::configure(*this, *p_world.ptr(), p_configuration);
}

Error SuperposReplicationServer::attach_peer(const Ref<SuperposSession> &p_link, const Dictionary &p_configuration) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_link.is_null()) { return last_error = ERR_INVALID_PARAMETER; }
    uint64_t incarnation = 0;
    return last_error = SuperposReplicationAccess::attach(*this, *p_link.ptr(), p_configuration, incarnation);
}

Error SuperposReplicationServer::detach_peer(const Ref<SuperposSession> &p_link) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (publishing) { return last_error = ERR_BUSY; }
    if (p_link.is_null()) { return last_error = ERR_INVALID_PARAMETER; }
    for (uint32_t i = 0; i < peer_count; ++i) {
        if (peers[i].link != p_link->get_instance_id()) { continue; }
        const Error detached = SuperposReplicationAccess::detach(*p_link.ptr(), peers[i].incarnation);
        if (detached == OK) { _remove_peer(i); }
        return last_error = detached;
    }
    return last_error = ERR_DOES_NOT_EXIST;
}

Dictionary SuperposReplicationServer::publish() {
    if (Thread::get_caller_id() != owner_thread) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    Ref<SuperposReplicationServer> keep_alive(this);
    Dictionary result = SuperposReplicationAccess::publish(*this);
    last_error = Error(int(result.get("error", ERR_BUG)));
    return result;
}

Dictionary SuperposReplicationServer::read_peer_status(const Ref<SuperposSession> &p_link) const {
    Dictionary result;
    result["error"] = ERR_DOES_NOT_EXIST;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (p_link.is_null()) { result["error"] = ERR_INVALID_PARAMETER; return result; }
    for (uint32_t i = 0; i < peer_count; ++i) {
        if (peers[i].link == p_link->get_instance_id()) { return SuperposReplicationAccess::status(*p_link.ptr(), peers[i].incarnation); }
    }
    return result;
}

Error SuperposReplicationServer::send_rpc(const Ref<SuperposSession> &p_link, uint64_t p_handle, uint64_t p_rpc_id, const PackedByteArray &p_payload) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_link.is_null()) { return last_error = ERR_INVALID_PARAMETER; }
    for (uint32_t i = 0; i < peer_count; ++i) {
        if (peers[i].link == p_link->get_instance_id()) {
            return last_error = SuperposReplicationAccess::send_rpc(*this, *p_link.ptr(), peers[i].incarnation, p_handle, p_rpc_id, p_payload);
        }
    }
    return last_error = ERR_DOES_NOT_EXIST;
}

Array SuperposReplicationServer::read_rpcs(const Ref<SuperposSession> &p_link, uint32_t p_maximum) {
    if (Thread::get_caller_id() != owner_thread || p_link.is_null()) { return Array(); }
    for (uint32_t i = 0; i < peer_count; ++i) {
        if (peers[i].link == p_link->get_instance_id()) {
            return SuperposReplicationAccess::take_rpcs(*this, *p_link.ptr(), peers[i].incarnation, p_maximum);
        }
    }
    return Array();
}

Dictionary SuperposReplicationServer::read_status() const {
    Dictionary result;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    result["error"] = world.is_valid() ? OK : ERR_UNCONFIGURED;
    result["configured"] = world.is_valid();
    result["peers"] = peer_count;
    result["maximum_peers"] = peer_limit;
    result["publishes"] = superpos_egp::signed_bits(publishes);
    result["authority_epoch"] = superpos_egp::signed_bits(world_epoch);
    return result;
}

Error SuperposReplicationServer::get_last_error() const {
    return Thread::get_caller_id() != owner_thread ? ERR_BUSY : last_error;
}

Error SuperposReplicationServer::set_object_region(uint64_t p_handle, uint32_t p_region) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return last_error = SuperposReplicationAccess::set_policy(*this, p_handle, SuperposReplicationAccess::Policy::Region, p_region);
}
Error SuperposReplicationServer::set_object_dormant(uint64_t p_handle, bool p_dormant) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return last_error = SuperposReplicationAccess::set_policy(*this, p_handle, SuperposReplicationAccess::Policy::Dormant, p_dormant ? 1 : 0);
}
Error SuperposReplicationServer::set_object_priority(uint64_t p_handle, uint32_t p_weight) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return last_error = SuperposReplicationAccess::set_policy(*this, p_handle, SuperposReplicationAccess::Policy::Weight, p_weight);
}
Error SuperposReplicationServer::set_object_group(uint64_t p_handle, uint64_t p_group) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    return last_error = SuperposReplicationAccess::set_policy(*this, p_handle, SuperposReplicationAccess::Policy::Group, p_group);
}
Dictionary SuperposReplicationServer::read_object_policy(uint64_t p_handle) const {
    if (Thread::get_caller_id() != owner_thread) { Dictionary result; result["error"] = ERR_BUSY; return result; }
    return SuperposReplicationAccess::read_policy(*const_cast<SuperposReplicationServer *>(this), p_handle);
}
Error SuperposReplicationServer::set_peer_interest(const Ref<SuperposSession> &p_link, const PackedInt64Array &p_regions, bool p_filter) {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (p_link.is_null()) { return last_error = ERR_INVALID_PARAMETER; }
    for (uint32_t i = 0; i < peer_count; ++i) {
        if (peers[i].link == p_link->get_instance_id()) {
            return last_error = SuperposReplicationAccess::set_interest(*this, *p_link.ptr(), peers[i].incarnation, p_regions, p_filter);
        }
    }
    return last_error = ERR_DOES_NOT_EXIST;
}

void SuperposReplicationServer::close() {
    if (Thread::get_caller_id() != owner_thread || publishing) { return; }
    for (uint32_t i = peer_count; i; --i) {
        auto *link = Object::cast_to<SuperposSession>(ObjectDB::get_instance(peers[i - 1].link));
        Ref<SuperposSession> pin(link);
        if (pin.is_valid()) { (void)SuperposReplicationAccess::detach(*pin.ptr(), peers[i - 1].incarnation); }
        _remove_peer(i - 1);
    }
    world = ObjectID();
    world_instance = world_epoch = 0;
    SuperposReplicationAccess::release(*this);
    last_error = OK;
}

SuperposReplicationServer::~SuperposReplicationServer() {
    // Detach only on the owner thread; a foreign-thread last reference leaves
    // link publishers attached but inert (no further fan-out reaches them).
    if (Thread::get_caller_id() == owner_thread && Thread::is_main_thread()) { close(); }
    SuperposReplicationAccess::release(*this);
}

void SuperposReplicationServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "world", "configuration"), &SuperposReplicationServer::configure, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("attach_peer", "link", "configuration"), &SuperposReplicationServer::attach_peer, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("detach_peer", "link"), &SuperposReplicationServer::detach_peer);
    ClassDB::bind_method(D_METHOD("publish"), &SuperposReplicationServer::publish);
    ClassDB::bind_method(D_METHOD("read_peer_status", "link"), &SuperposReplicationServer::read_peer_status);
    ClassDB::bind_method(D_METHOD("read_status"), &SuperposReplicationServer::read_status);
    ClassDB::bind_method(D_METHOD("send_rpc", "link", "handle", "rpc_id", "payload"), &SuperposReplicationServer::send_rpc);
    ClassDB::bind_method(D_METHOD("read_rpcs", "link", "maximum"), &SuperposReplicationServer::read_rpcs, DEFVAL(16));
    ClassDB::bind_method(D_METHOD("set_object_region", "handle", "region"), &SuperposReplicationServer::set_object_region);
    ClassDB::bind_method(D_METHOD("set_object_dormant", "handle", "dormant"), &SuperposReplicationServer::set_object_dormant);
    ClassDB::bind_method(D_METHOD("set_object_priority", "handle", "weight"), &SuperposReplicationServer::set_object_priority);
    ClassDB::bind_method(D_METHOD("set_object_group", "handle", "group"), &SuperposReplicationServer::set_object_group);
    ClassDB::bind_method(D_METHOD("read_object_policy", "handle"), &SuperposReplicationServer::read_object_policy);
    ClassDB::bind_method(D_METHOD("set_peer_interest", "link", "regions", "filter"), &SuperposReplicationServer::set_peer_interest, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_last_error"), &SuperposReplicationServer::get_last_error);
    ClassDB::bind_method(D_METHOD("close"), &SuperposReplicationServer::close);
}
