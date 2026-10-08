/**************************************************************************/
/*  superposition_world.cpp                                               */
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

#include "superposition_world.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

Error SuperpositionWorld::report(Error p_error, const String &p_action) {
	if (p_error != OK) {
		last_error = p_action + ": " + error_names[p_error];
		emit_signal("network_error", p_error, last_error);
	} else {
		last_error = String();
	}
	return p_error;
}

Error SuperpositionWorld::configure(const Dictionary &p_options) {
	if (session.is_valid()) {
		return report(ERR_ALREADY_IN_USE, "Session is already configured; stop it before reconfiguring");
	}
	Dictionary options;
	options["tick_rate"] = tick_rate;
	options["max_players"] = max_players;
	options["max_entities"] = max_entities;
	options["game_protocol"] = game_protocol;
	options["simulation_fingerprint"] = simulation_fingerprint;
	options["allow_insecure_loopback"] = allow_insecure_loopback;
	options.merge(p_options, true);
	Ref<EGPNetSession> candidate;
	candidate.instantiate();
	Error result = candidate->configure(options);
	if (result != OK) {
		return report(result, "Configure session");
	}
	last_error = String();
	session = candidate;
	const uint64_t generation = ++session_generation;
	const ObjectID self_id = get_instance_id();
	configured_tick_rate = options["tick_rate"];
	configured_simulation_fingerprint = options["simulation_fingerprint"];
	tick_rate = configured_tick_rate;
	simulation_fingerprint = configured_simulation_fingerprint;
	max_players = options["max_players"];
	max_entities = options["max_entities"];
	game_protocol = options["game_protocol"];
	allow_insecure_loopback = options["allow_insecure_loopback"];
	session->connect("state_changed", callable_mp(this, &SuperpositionWorld::on_state));
	session->connect("peer_connected", callable_mp(this, &SuperpositionWorld::on_connected));
	session->connect("peer_disconnected", callable_mp(this, &SuperpositionWorld::on_disconnected));
	session->connect("application_received", callable_mp(this, &SuperpositionWorld::on_application));
	session->connect("packet_received", callable_mp(this, &SuperpositionWorld::on_packet));
	session->connect("simulation_tick", callable_mp(this, &SuperpositionWorld::on_tick));
	session->connect("diagnostic", callable_mp(this, &SuperpositionWorld::on_diagnostic));
	emit_signal("session_changed");
	if (ObjectDB::get_instance(self_id) != this) {
		return ERR_BUSY;
	}
	return generation == session_generation && session == candidate && !is_queued_for_deletion() ? OK : ERR_BUSY;
}

Error SuperpositionWorld::start() {
	return role == 0 ? start_server() : join_loopback();
}

Error SuperpositionWorld::start_server() {
	const ObjectID self_id = get_instance_id();
	if (session.is_null()) {
		Error result = configure();
		if (result != OK) {
			return result;
		}
	}
	Ref<EGPNetSession> operation = session;
	const uint64_t generation = session_generation;
	Error result = operation->listen(port, allow_insecure_loopback ? "127.0.0.1" : "0.0.0.0");
	if (ObjectDB::get_instance(self_id) != this) {
		return ERR_BUSY;
	}
	return generation == session_generation && !is_queued_for_deletion() ? report(result, "Listen") : ERR_BUSY;
}

Error SuperpositionWorld::join_loopback() {
	const ObjectID self_id = get_instance_id();
	if (!allow_insecure_loopback) {
		return report(ERR_UNAUTHORIZED, "Local testing requires Allow Insecure Loopback; use join_token for authenticated clients");
	}
	if (session.is_null()) {
		Error result = configure();
		if (result != OK) {
			return result;
		}
	}
	Ref<EGPNetSession> operation = session;
	const uint64_t generation = session_generation;
	Error result = operation->connect_to_server(address, port);
	if (ObjectDB::get_instance(self_id) != this) {
		return ERR_BUSY;
	}
	return generation == session_generation && !is_queued_for_deletion() ? report(result, "Join local server") : ERR_BUSY;
}

Error SuperpositionWorld::join_token(int64_t p_client_id, const PackedByteArray &p_token, const String &p_binding) {
	const ObjectID self_id = get_instance_id();
	if (session.is_null()) {
		Error result = configure();
		if (result != OK) {
			return result;
		}
	}
	Ref<EGPNetSession> operation = session;
	const uint64_t generation = session_generation;
	Error result = operation->connect_token(p_client_id, p_token, p_binding);
	if (ObjectDB::get_instance(self_id) != this) {
		return ERR_BUSY;
	}
	return generation == session_generation && !is_queued_for_deletion() ? report(result, "Join authenticated server") : ERR_BUSY;
}

Error SuperpositionWorld::poll() {
	if (session.is_null()) {
		return ERR_UNCONFIGURED;
	}
	Ref<EGPNetSession> operation = session;
	const ObjectID self_id = get_instance_id();
	const uint64_t generation = session_generation;
	Error result = operation->poll();
	if (ObjectDB::get_instance(self_id) != this) {
		return ERR_BUSY;
	}
	if (generation != session_generation || is_queued_for_deletion()) {
		return ERR_BUSY;
	}
	return result == OK ? OK : report(result, "Poll session");
}

void SuperpositionWorld::stop() {
	if (session.is_null()) {
		return;
	}
	const ObjectID self_id = get_instance_id();
	Ref<EGPNetSession> previous = session;
	const uint64_t generation = ++session_generation;
	session.unref();
	if (previous.is_valid()) {
		previous->disconnect("state_changed", callable_mp(this, &SuperpositionWorld::on_state));
		previous->disconnect("peer_connected", callable_mp(this, &SuperpositionWorld::on_connected));
		previous->disconnect("peer_disconnected", callable_mp(this, &SuperpositionWorld::on_disconnected));
		previous->disconnect("application_received", callable_mp(this, &SuperpositionWorld::on_application));
		previous->disconnect("packet_received", callable_mp(this, &SuperpositionWorld::on_packet));
		previous->disconnect("simulation_tick", callable_mp(this, &SuperpositionWorld::on_tick));
		previous->disconnect("diagnostic", callable_mp(this, &SuperpositionWorld::on_diagnostic));
		previous->stop();
	}
	if (ObjectDB::get_instance(self_id) != this || generation != session_generation) {
		return;
	}
	emit_signal("session_changed");
}

void SuperpositionWorld::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY && !Engine::get_singleton()->is_editor_hint()) {
		set_process_priority(-100);
		set_process(auto_poll);
		if (auto_start) {
			start();
		}
	} else if (p_what == NOTIFICATION_PROCESS && session.is_valid()) {
		poll();
	} else if (p_what == NOTIFICATION_EXIT_TREE && !Engine::get_singleton()->is_editor_hint()) {
		stop();
	}
}

Dictionary SuperpositionWorld::get_statistics() const {
	Dictionary result;
	result["state"] = session.is_valid() ? session->get_state() : String("Unconfigured");
	result["last_error"] = last_error;
	result["session"] = session.is_valid() ? session->get_statistics() : Dictionary();
	return result;
}

bool SuperpositionWorld::_get(const StringName &p_name, Variant &r_value) const {
	if (p_name == StringName("status/statistics")) {
		r_value = get_statistics();
		return true;
	}
	if (p_name == StringName("status/last_error")) {
		r_value = last_error;
		return true;
	}
	return false;
}

void SuperpositionWorld::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::STRING, "status/last_error", PROPERTY_HINT_MULTILINE_TEXT, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
	p_list->push_back(PropertyInfo(Variant::DICTIONARY, "status/statistics", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
}

PackedStringArray SuperpositionWorld::get_configuration_warnings() const {
	PackedStringArray result;
	if (auto_start && role == 1 && !allow_insecure_loopback) {
		result.push_back("Auto Start with the Local Client role requires Allow Insecure Loopback. For authenticated clients, call join_token with a server-issued token at runtime.");
	}
	if (game_protocol.is_empty() || simulation_fingerprint.is_empty()) {
		result.push_back("Game protocol and simulation fingerprint must identify matching server and client builds.");
	}
	return result;
}

void SuperpositionWorld::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "options"), &SuperpositionWorld::configure, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("start"), &SuperpositionWorld::start);
	ClassDB::bind_method(D_METHOD("start_server"), &SuperpositionWorld::start_server);
	ClassDB::bind_method(D_METHOD("join_loopback"), &SuperpositionWorld::join_loopback);
	ClassDB::bind_method(D_METHOD("join_token", "client_id", "token", "binding"), &SuperpositionWorld::join_token, DEFVAL("0.0.0.0"));
	ClassDB::bind_method(D_METHOD("poll"), &SuperpositionWorld::poll);
	ClassDB::bind_method(D_METHOD("stop"), &SuperpositionWorld::stop);
	ClassDB::bind_method(D_METHOD("get_session"), &SuperpositionWorld::get_session);
	ClassDB::bind_method(D_METHOD("get_statistics"), &SuperpositionWorld::get_statistics);
#define WORLD_PROPERTY(TYPE, NAME, HINT, TEXT) \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &SuperpositionWorld::set_##NAME); \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &SuperpositionWorld::get_##NAME); \
	ADD_PROPERTY(PropertyInfo(TYPE, #NAME, HINT, TEXT), "set_" #NAME, "get_" #NAME)
#define WORLD_BOOL(NAME) \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "enabled"), &SuperpositionWorld::set_##NAME); \
	ClassDB::bind_method(D_METHOD("is_" #NAME), &SuperpositionWorld::is_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, #NAME), "set_" #NAME, "is_" #NAME)
	WORLD_BOOL(auto_start);
	WORLD_BOOL(auto_poll);
	WORLD_PROPERTY(Variant::INT, role, PROPERTY_HINT_ENUM, "Server,Local Client");
	WORLD_PROPERTY(Variant::INT, port, PROPERTY_HINT_RANGE, "0,65535,1");
	WORLD_PROPERTY(Variant::STRING, address, PROPERTY_HINT_NONE, "");
	WORLD_BOOL(allow_insecure_loopback);
	ADD_GROUP("Protocol", "");
	WORLD_PROPERTY(Variant::STRING, game_protocol, PROPERTY_HINT_NONE, "");
	WORLD_PROPERTY(Variant::STRING, simulation_fingerprint, PROPERTY_HINT_NONE, "");
	WORLD_PROPERTY(Variant::INT, tick_rate, PROPERTY_HINT_RANGE, "1,240,1");
	ADD_GROUP("Capacity", "");
	WORLD_PROPERTY(Variant::INT, max_players, PROPERTY_HINT_RANGE, "1,64,1");
	WORLD_PROPERTY(Variant::INT, max_entities, PROPERTY_HINT_RANGE, "1,4096,1");
#undef WORLD_PROPERTY
#undef WORLD_BOOL
	ADD_SIGNAL(MethodInfo("session_changed"));
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::STRING, "state")));
	ADD_SIGNAL(MethodInfo("peer_connected", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("peer_disconnected", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("application_received", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload")));
	ADD_SIGNAL(MethodInfo("packet_received", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload"), PropertyInfo(Variant::INT, "channel"), PropertyInfo(Variant::INT, "delivery")));
	ADD_SIGNAL(MethodInfo("simulation_tick", PropertyInfo(Variant::INT, "tick"), PropertyInfo(Variant::BOOL, "server")));
	ADD_SIGNAL(MethodInfo("diagnostic", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("network_error", PropertyInfo(Variant::INT, "error"), PropertyInfo(Variant::STRING, "message")));
}
