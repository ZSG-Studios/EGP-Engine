/**************************************************************************/
/*  superposition_rpc.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "superposition_rpc.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"

#include <cstring>

namespace {
bool rpc_type(int p_type) {
	return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT || p_type == Variant::STRING || p_type == Variant::VECTOR3;
}
} //namespace
void SuperpositionRPCMethod::_bind_methods() {
#define SP_PROP(name, setter, getter, type, hint, text) \
	ClassDB::bind_method(D_METHOD(#setter, "value"), &SuperpositionRPCMethod::setter); \
	ClassDB::bind_method(D_METHOD(#getter), &SuperpositionRPCMethod::getter); \
	ADD_PROPERTY(PropertyInfo(type, name, hint, text), #setter, #getter)
	SP_PROP("method_id", set_method_id, get_method_id, Variant::INT, PROPERTY_HINT_RANGE, "1,65535,1");
	SP_PROP("method", set_method, get_method, Variant::STRING_NAME, PROPERTY_HINT_NONE, "");
	SP_PROP("permission", set_permission, get_permission, Variant::INT, PROPERTY_HINT_ENUM, "Authority,Owner,Any Peer");
	SP_PROP("argument_types", set_argument_types, get_argument_types, Variant::PACKED_INT32_ARRAY, PROPERTY_HINT_ARRAY_TYPE, "2/2:Boolean:1,Integer:2,Float:3,String:4,Vector3:9");
	SP_PROP("calls_per_second", set_calls_per_second, get_calls_per_second, Variant::INT, PROPERTY_HINT_RANGE, "1,128,1");
#undef SP_PROP
	BIND_ENUM_CONSTANT(AUTHORITY);
	BIND_ENUM_CONSTANT(OWNER);
	BIND_ENUM_CONSTANT(ANY_PEER);
}
Error SuperpositionRPC::fail(Error p_error, const String &p_message) {
	last_error = p_message;
	++rejected;
	emit_signal("rpc_error", p_error, p_message);
	return p_error;
}
Ref<SuperpositionRPCMethod> SuperpositionRPC::find_method(int p_method_id) const {
	for (int i = 0; i < methods.size(); i++) {
		Ref<SuperpositionRPCMethod> rule = methods[i];
		if (rule.is_valid() && rule->get_method_id() == p_method_id) {
			return rule;
		}
	}
	return Ref<SuperpositionRPCMethod>();
}
Error SuperpositionRPC::validate_methods() const {
	if (methods.is_empty() || methods.size() > 64) {
		return ERR_UNCONFIGURED;
	}
	HashSet<int> ids;
	HashSet<StringName> names;
	for (int i = 0; i < methods.size(); i++) {
		Ref<SuperpositionRPCMethod> rule = methods[i];
		if (rule.is_null() || rule->get_method_id() < 1 || rule->get_method_id() > 65535 || String(rule->get_method()).is_empty() || String(rule->get_method()).utf8().length() > 128 || ids.has(rule->get_method_id()) || names.has(rule->get_method()) || rule->get_permission() < 0 || rule->get_permission() > 2 || rule->get_calls_per_second() < 1 || rule->get_calls_per_second() > 128 || rule->get_argument_types().size() > 8) {
			return ERR_INVALID_PARAMETER;
		}
		PackedInt32Array types = rule->get_argument_types();
		for (int t : types) {
			if (!rpc_type(t)) {
				return ERR_INVALID_PARAMETER;
			}
		}
		ids.insert(rule->get_method_id());
		names.insert(rule->get_method());
	}
	return OK;
}
bool SuperpositionRPC::valid_arguments(const Ref<SuperpositionRPCMethod> &p_rule, const Array &p_arguments) const {
	PackedInt32Array types = p_rule->get_argument_types();
	if (p_arguments.size() != types.size() || p_arguments.size() > 8) {
		return false;
	}
	for (int i = 0; i < types.size(); i++) {
		Variant value = p_arguments[i];
		if (value.get_type() != types[i] || !rpc_type(types[i])) {
			return false;
		}
		if (types[i] == Variant::FLOAT && !Math::is_finite(double(value))) {
			return false;
		}
		if (types[i] == Variant::VECTOR3 && !Vector3(value).is_finite()) {
			return false;
		}
		if (types[i] == Variant::STRING && String(value).utf8().length() > 256) {
			return false;
		}
	}
	return true;
}
bool SuperpositionRPC::permit(const Ref<SuperpositionRPCMethod> &p_rule, SuperpositionSpawner *p_spawner, int64_t p_entity, bool p_authority, int64_t p_identity) const {
	if (!p_spawner->has_entity(p_entity)) {
		return false;
	}
	switch (p_rule->get_permission()) {
		case SuperpositionRPCMethod::AUTHORITY:
			return p_authority;
		case SuperpositionRPCMethod::OWNER:
			return !p_authority && p_spawner->get_owner_client_id(p_entity) != -1 && p_spawner->get_owner_client_id(p_entity) == p_identity;
		case SuperpositionRPCMethod::ANY_PEER:
			return true;
	}
	return false;
}
bool SuperpositionRPC::rate_limit(int64_t p_peer, int p_method, int p_limit) {
	uint64_t second = OS::get_singleton()->get_ticks_msec() / 1000;
	// Bound aggregate traffic before per-rule accounting; peers cannot expand this map with wire method IDs.
	String key = itos(p_peer) + "/" + itos(p_method);
	if (!rates.has(key) && rates.size() >= 8192) {
		return false;
	}
	Rate &rate = rates[key];
	if (rate.second != second) {
		rate.second = second;
		rate.count = 0;
	}
	return ++rate.count <= p_limit;
}
SuperpositionSpawner *SuperpositionRPC::get_spawner() const {
	return is_inside_tree() ? Object::cast_to<SuperpositionSpawner>(get_node_or_null(spawner_path)) : nullptr;
}
void SuperpositionRPC::bind_session(const Ref<EGPNetSession> &p_session) {
	if (session == p_session) {
		return;
	}
	if (session.is_valid() && session->is_connected("application_received", callable_mp(this, &SuperpositionRPC::receive_application))) {
		session->disconnect("application_received", callable_mp(this, &SuperpositionRPC::receive_application));
	}
	if (session.is_valid() && session->is_connected("peer_disconnected", callable_mp(this, &SuperpositionRPC::peer_disconnected))) {
		session->disconnect("peer_disconnected", callable_mp(this, &SuperpositionRPC::peer_disconnected));
	}
	session = p_session;
	pending.clear();
	rates.clear();
	++generation;
	if (session.is_valid()) {
		session->connect("application_received", callable_mp(this, &SuperpositionRPC::receive_application));
	}
	if (session.is_valid()) {
		session->connect("peer_disconnected", callable_mp(this, &SuperpositionRPC::peer_disconnected));
	}
}
void SuperpositionRPC::set_spawner_path(const NodePath &p_path) {
	if (spawner_path != p_path) {
		bind_session(Ref<EGPNetSession>());
	}
	spawner_path = p_path;
	update_configuration_warnings();
}
void SuperpositionRPC::set_methods(const TypedArray<SuperpositionRPCMethod> &p_methods) {
	pending.clear();
	++generation;
	methods = p_methods;
	update_configuration_warnings();
}
Error SuperpositionRPC::send_rpc(int64_t p_entity, int p_method_id, const Array &p_arguments, int64_t p_peer) {
	SuperpositionSpawner *spawner = get_spawner();
	if (!spawner) {
		return fail(ERR_UNCONFIGURED, "Choose a SuperpositionSpawner for RPC targets.");
	}
	bind_session(spawner->get_session());
	if (session.is_null() || (session->get_state() != "Listening" && session->get_state() != "Connected")) {
		return fail(ERR_UNCONFIGURED, "RPC requires an active native session.");
	}
	Ref<SuperpositionRPCMethod> rule = find_method(p_method_id);
	if (validate_methods() != OK || rule.is_null() || !valid_arguments(rule, p_arguments) || !spawner->has_entity(p_entity)) {
		return fail(ERR_INVALID_PARAMETER, "RPC target, allowlisted method or typed arguments are invalid.");
	}
	bool server = session->get_state() == "Listening";
	if (server && !permit(rule, spawner, p_entity, true, -1)) {
		return fail(ERR_UNAUTHORIZED, "Server may not invoke an owner-only method.");
	}
	if (!server && (rule->get_permission() == SuperpositionRPCMethod::AUTHORITY || (p_peer != -1 && p_peer != 0))) {
		return fail(ERR_UNAUTHORIZED, "Clients may only send permitted calls to the authenticated server.");
	}
	if (!rate_limit(-1, 0, 128) || !rate_limit(-1, p_method_id, rule->get_calls_per_second())) {
		return fail(ERR_BUSY, "Outgoing RPC rate limit exceeded.");
	}
	Array envelope;
	envelope.push_back("SPRPC1");
	envelope.push_back(spawner->get_replication_key());
	envelope.push_back(p_entity);
	envelope.push_back(p_method_id);
	envelope.push_back(p_arguments);
	envelope.push_back(get_method_fingerprint());
	int size = 0;
	if (encode_variant(envelope, nullptr, size, false) != OK || size > 4088) {
		return fail(ERR_INVALID_DATA, "RPC exceeds the 4096-byte wire limit.");
	}
	PackedByteArray bytes;
	bytes.resize(size + 8);
	memcpy(bytes.ptrw(), "EGPRPC01", 8);
	encode_variant(envelope, bytes.ptrw() + 8, size, false);
	if (!server || p_peer != -1) {
		if (server) {
			int64_t identity;
			if (!spawner->resolve_peer_identity(p_peer, identity) || !spawner->is_peer_compatible(p_peer)) {
				return fail(ERR_UNAUTHORIZED, "RPC recipient is not authenticated.");
			}
		}
		Error error = session->send_application(server ? p_peer : 0, bytes);
		if (error != OK) {
			return fail(error, "Native reliable RPC send failed.");
		}
		++sent;
		return OK;
	}
	Array peers = session->command("peers");
	for (int i = 0; i < peers.size(); i++) {
		Dictionary peer = peers[i];
		if (!spawner->is_peer_compatible(peer["peer_id"])) {
			continue;
		}
		Error error = session->send_application(peer["peer_id"], bytes);
		if (error != OK) {
			return fail(error, "Native reliable RPC broadcast failed.");
		}
		++sent;
	}
	return OK;
}
void SuperpositionRPC::receive_application(int64_t p_peer, const PackedByteArray &p_payload) {
	if (!is_inside_tree() || p_payload.size() < 8 || memcmp(p_payload.ptr(), "EGPRPC01", 8) != 0 || p_payload.size() > 4096) {
		return;
	}
	Variant decoded;
	int consumed = 0;
	if (decode_variant(decoded, p_payload.ptr() + 8, p_payload.size() - 8, &consumed, false) != OK || consumed != p_payload.size() - 8 || decoded.get_type() != Variant::ARRAY) {
		return;
	}
	Array envelope = decoded;
	if (envelope.size() != 6 || envelope[0].get_type() != Variant::STRING || String(envelope[0]) != "SPRPC1") {
		return;
	}
	SuperpositionSpawner *spawner = get_spawner();
	if (!spawner || envelope[1].get_type() != Variant::STRING || String(envelope[1]) != spawner->get_replication_key()) {
		return;
	}
	if (session.is_null() || validate_methods() != OK || envelope[2].get_type() != Variant::INT || envelope[3].get_type() != Variant::INT || int64_t(envelope[3]) < 1 || int64_t(envelope[3]) > 65535 || envelope[4].get_type() != Variant::ARRAY) {
		fail(ERR_INVALID_DATA, "Invalid RPC envelope or method configuration.");
		return;
	}
	if (envelope[5].get_type() != Variant::STRING || String(envelope[5]) != get_method_fingerprint() || !spawner->is_peer_compatible(p_peer)) {
		fail(ERR_INVALID_DATA, "RPC scene or method schema does not match the peer.");
		return;
	}
	bool authority = session->get_state() == "Connected" && p_peer == 0;
	int64_t identity = -1;
	if (!authority && !spawner->resolve_peer_identity(p_peer, identity)) {
		fail(ERR_UNAUTHORIZED, "RPC sender is not an authenticated live peer.");
		return;
	}
	Ref<SuperpositionRPCMethod> rule = find_method(int(envelope[3]));
	if (rule.is_null() || !valid_arguments(rule, envelope[4]) || !permit(rule, spawner, envelope[2], authority, identity)) {
		fail(ERR_UNAUTHORIZED, "RPC sender lacks target ownership or method permission, or arguments violate the allowlist.");
		return;
	}
	if (pending.size() >= 256 || !rate_limit(p_peer, 0, 128) || !rate_limit(p_peer, int(envelope[3]), rule->get_calls_per_second())) {
		fail(ERR_BUSY, "Incoming RPC rate or dispatch queue limit exceeded.");
		return;
	}
	Dictionary queued;
	queued["generation"] = int64_t(generation);
	queued["method_fingerprint"] = get_method_fingerprint();
	queued["spawner_id"] = int64_t(spawner->get_instance_id());
	queued["target_id"] = int64_t(spawner->get_spawned_node(envelope[2])->get_instance_id());
	queued["peer"] = p_peer;
	queued["identity"] = identity;
	queued["authority"] = authority;
	queued["entity"] = envelope[2];
	queued["method"] = envelope[3];
	queued["arguments"] = envelope[4];
	pending.push_back(queued);
}
void SuperpositionRPC::dispatch_pending() {
	const ObjectID self_id = get_instance_id();
	Array queued = pending;
	pending = Array();
	for (int i = 0; i < queued.size(); i++) {
		Dictionary call = queued[i];
		SuperpositionSpawner *spawner = get_spawner();
		if (!spawner || session.is_null() || uint64_t(int64_t(call["generation"])) != generation || spawner->get_session() != session) {
			continue;
		}
		if (int64_t(spawner->get_instance_id()) != int64_t(call["spawner_id"]) || get_method_fingerprint() != String(call["method_fingerprint"]) || !spawner->is_peer_compatible(call["peer"])) {
			continue;
		}
		bool authority = call["authority"];
		int64_t identity = -1;
		if (authority) {
			if (session->get_state() != "Connected" || int64_t(call["peer"]) != 0) {
				continue;
			}
		} else if (!spawner->resolve_peer_identity(call["peer"], identity) || identity != int64_t(call["identity"])) {
			continue;
		}
		Ref<SuperpositionRPCMethod> rule = find_method(call["method"]);
		Array args = call["arguments"];
		Node *target = spawner->get_spawned_node(call["entity"]);
		if (validate_methods() != OK || rule.is_null() || !target || int64_t(target->get_instance_id()) != int64_t(call["target_id"]) || !permit(rule, spawner, call["entity"], authority, identity) || !valid_arguments(rule, args) || !target->has_method(rule->get_method())) {
			fail(ERR_UNAUTHORIZED, "Queued RPC target or allowlist changed before dispatch.");
			if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion()) {
				return;
			}
			continue;
		}
		// Expose transport-authenticated sender to game handlers; never accept a claimed peer from wire arguments.
		target->set_meta("superposition_rpc_sender_peer", call["peer"]);
		target->set_meta("superposition_rpc_sender_client_id", identity);
		Vector<const Variant *> pointers;
		pointers.resize(args.size());
		for (int a = 0; a < args.size(); a++) {
			pointers.write[a] = &args[a];
		}
		Callable::CallError error;
		target->callp(rule->get_method(), pointers.ptrw(), pointers.size(), error);
		if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion()) {
			return;
		}
		if (error.error != Callable::CallError::CALL_OK) {
			fail(ERR_INVALID_PARAMETER, "Allowlisted target method rejected RPC arguments.");
			if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion()) {
				return;
			}
			continue;
		}
		++received;
		emit_signal("rpc_dispatched", call["peer"], call["entity"], call["method"]);
		if (ObjectDB::get_instance(self_id) != this || is_queued_for_deletion()) {
			return;
		}
	}
}
void SuperpositionRPC::peer_disconnected(int64_t p_peer) {
	pending.clear();
	rates.clear();
	++generation;
}
String SuperpositionRPC::get_method_fingerprint() const {
	if (validate_methods() != OK) {
		return String();
	}
	Vector<int> ids;
	for (int i = 0; i < methods.size(); i++) {
		Ref<SuperpositionRPCMethod> rule = methods[i];
		ids.push_back(rule->get_method_id());
	}
	ids.sort();
	String description = "SuperpositionRPC/1\n";
	for (int id : ids) {
		Ref<SuperpositionRPCMethod> rule = find_method(id);
		description += itos(id) + ":" + String(rule->get_method()) + ":" + itos(rule->get_permission()) + ":" + itos(rule->get_calls_per_second());
		for (int type : rule->get_argument_types()) {
			description += ":" + itos(type);
		}
		description += "\n";
	}
	return description.sha256_text();
}
Dictionary SuperpositionRPC::get_statistics() const {
	Dictionary out;
	out["method_fingerprint"] = get_method_fingerprint();
	out["sent"] = int64_t(sent);
	out["received"] = int64_t(received);
	out["rejected"] = int64_t(rejected);
	out["queued"] = pending.size();
	out["last_error"] = last_error;
	return out;
}
PackedStringArray SuperpositionRPC::get_configuration_warnings() const {
	PackedStringArray out;
	if (spawner_path.is_empty()) {
		out.push_back("Choose a SuperpositionSpawner; RPC may only target its spawned roots.");
	}
	if (validate_methods() != OK) {
		out.push_back("Assign unique method IDs and names, caller permissions, exact scalar argument types and rate limits.");
	}
	return out;
}
void SuperpositionRPC::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		set_process(true);
	} else if (p_what == NOTIFICATION_PROCESS && !Engine::get_singleton()->is_editor_hint()) {
		SuperpositionSpawner *spawner = get_spawner();
		bind_session(spawner ? spawner->get_session() : Ref<EGPNetSession>());
		if (session.is_null() || (session->get_state() != "Listening" && session->get_state() != "Connected")) {
			pending.clear();
			rates.clear();
			++generation;
		} else {
			dispatch_pending();
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		bind_session(Ref<EGPNetSession>());
	}
}
void SuperpositionRPC::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_spawner_path", "path"), &SuperpositionRPC::set_spawner_path);
	ClassDB::bind_method(D_METHOD("get_spawner_path"), &SuperpositionRPC::get_spawner_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spawner_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SuperpositionSpawner"), "set_spawner_path", "get_spawner_path");
	ClassDB::bind_method(D_METHOD("set_methods", "methods"), &SuperpositionRPC::set_methods);
	ClassDB::bind_method(D_METHOD("get_methods"), &SuperpositionRPC::get_methods);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "methods", PROPERTY_HINT_ARRAY_TYPE, "SuperpositionRPCMethod"), "set_methods", "get_methods");
	ClassDB::bind_method(D_METHOD("get_spawner"), &SuperpositionRPC::get_spawner);
	ClassDB::bind_method(D_METHOD("send_rpc", "entity", "method_id", "arguments", "peer"), &SuperpositionRPC::send_rpc, DEFVAL(Array()), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("get_method_fingerprint"), &SuperpositionRPC::get_method_fingerprint);
	ClassDB::bind_method(D_METHOD("get_statistics"), &SuperpositionRPC::get_statistics);
	ADD_SIGNAL(MethodInfo("rpc_error", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("rpc_dispatched", PropertyInfo(Variant::INT, "peer"), PropertyInfo(Variant::INT, "entity"), PropertyInfo(Variant::INT, "method_id")));
}
