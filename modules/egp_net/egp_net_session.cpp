/**************************************************************************/
/*  egp_net_session.cpp                                                   */
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

#include "egp_net_session.h"

#include "core/object/class_db.h"

#include <cstring>
namespace {
Error error(egp::net::Result result) {
	using R = egp::net::Result;
	switch (result) {
		case R::Ok:
			return OK;
		case R::Invalid:
			return ERR_INVALID_PARAMETER;
		case R::Busy:
			return ERR_BUSY;
		case R::Unconfigured:
			return ERR_UNCONFIGURED;
		case R::Unauthorized:
			return ERR_UNAUTHORIZED;
		case R::Full:
			return ERR_OUT_OF_MEMORY;
		case R::NotFound:
			return ERR_DOES_NOT_EXIST;
		case R::BadData:
			return ERR_INVALID_DATA;
		default:
			return FAILED;
	}
}
std::vector<uint8_t> bytes(const PackedByteArray &data) {
	return data.is_empty() ? std::vector<uint8_t>() : std::vector<uint8_t>(data.ptr(), data.ptr() + data.size());
}
PackedByteArray packed(const std::vector<uint8_t> &data) {
	PackedByteArray out;
	out.resize(data.size());
	if (!data.empty()) {
		std::memcpy(out.ptrw(), data.data(), data.size());
	}
	return out;
}
std::string text(const String &value) {
	return value.utf8().get_data();
}
Dictionary entity_row(const egp::net::Entity &entity) {
	Dictionary row;
	row["entity"] = int64_t(entity.handle);
	row["revision"] = int64_t(entity.revision);
	row["tick"] = int64_t(entity.tick);
	row["kind"] = entity.kind;
	row["authority_peer"] = entity.authority_peer;
	row["state"] = packed(entity.state);
	return row;
}
} //namespace
Error EGPNetSession::configure(const Dictionary &p_options) {
	if (Thread::get_caller_id() != owner_thread) {
		return ERR_BUSY;
	}
	if (session) {
		return ERR_ALREADY_IN_USE;
	}
	egp::net::Options options;
	const Array keys = p_options.keys();
	for (int i = 0; i < keys.size(); ++i) {
		const String name = keys[i];
		const Variant value = p_options[keys[i]];
		if (name == "game_protocol" || name == "simulation_fingerprint") {
			if (value.get_type() != Variant::STRING) {
				return ERR_INVALID_PARAMETER;
			}
			if (name == "game_protocol") {
				options.game_protocol = text(value);
			} else {
				options.simulation_fingerprint = text(value);
			}
		} else if (name == "allow_insecure_loopback") {
			if (value.get_type() != Variant::BOOL) {
				return ERR_INVALID_PARAMETER;
			}
			options.allow_insecure_loopback = value;
		} else if (name == "private_key") {
			if (value.get_type() != Variant::PACKED_BYTE_ARRAY) {
				return ERR_INVALID_PARAMETER;
			}
			PackedByteArray key = value;
			if (key.size() != 32) {
				return ERR_INVALID_PARAMETER;
			}
			std::memcpy(options.private_key.data(), key.ptr(), 32);
			options.has_private_key = true;
		} else if (name == "simulated_loss" || name == "simulated_latency_ms" || name == "simulated_jitter_ms") {
			if (value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) {
				return ERR_INVALID_PARAMETER;
			}
			if (name == "simulated_loss") {
				options.simulated_loss = value;
			} else if (name == "simulated_latency_ms") {
				options.simulated_latency_ms = value;
			} else {
				options.simulated_jitter_ms = value;
			}
		} else {
			if (value.get_type() != Variant::INT || int64_t(value) < 0 || int64_t(value) > INT32_MAX) {
				return ERR_INVALID_PARAMETER;
			}
			int number = value;
			if (name == "tick_rate") {
				options.tick_rate = number;
			} else if (name == "max_players") {
				options.max_players = number;
			} else if (name == "max_entities") {
				options.max_entities = number;
			} else if (name == "messages_per_second") {
				options.messages_per_second = number;
			} else if (name == "bytes_per_second") {
				options.bytes_per_second = number;
			} else if (name == "timeout_seconds") {
				options.timeout_seconds = number;
			} else if (name == "token_lifetime_seconds") {
				options.token_lifetime_seconds = number;
			} else {
				return ERR_INVALID_PARAMETER;
			}
		}
	}
	session = std::make_unique<egp::net::Session>(options);
	Error result = error(session->validation());
	if (result != OK) {
		session.reset();
		return result;
	}
	session->state_changed = [this](const std::string &state) { emit_signal("state_changed", String(state.c_str())); };
	session->peer_connected = [this](int64_t peer) { emit_signal("peer_connected", peer); };
	session->peer_disconnected = [this](int64_t peer) { emit_signal("peer_disconnected", peer); };
	session->application_received = [this](int64_t peer, const std::vector<uint8_t> &data) { emit_signal("application_received", peer, packed(data)); };
	session->packet_received = [this](int64_t peer, const std::vector<uint8_t> &data, int channel, int delivery) { emit_signal("packet_received", peer, packed(data), channel, delivery); };
	session->simulation_tick = [this](uint64_t tick, bool server) { emit_signal("simulation_tick", int64_t(tick), server); };
	session->diagnostic = [this](const std::string &message) { emit_signal("diagnostic", String(message.c_str())); };
	return OK;
}
Error EGPNetSession::listen(int p_port, const String &p_binding) {
	Ref<EGPNetSession> keep_alive(this);
	return session ? error(session->listen(p_port, text(p_binding))) : ERR_UNCONFIGURED;
}
Error EGPNetSession::connect_to_server(const String &p_address, int p_port) {
	Ref<EGPNetSession> keep_alive(this);
	return session ? error(session->connect_loopback(text(p_address), p_port)) : ERR_UNCONFIGURED;
}
Error EGPNetSession::connect_token(int64_t p_client_id, const PackedByteArray &p_token, const String &p_binding) {
	Ref<EGPNetSession> keep_alive(this);
	return session ? error(session->connect_token(uint64_t(p_client_id), bytes(p_token), text(p_binding))) : ERR_UNCONFIGURED;
}
Dictionary EGPNetSession::issue_token(int64_t p_client_id, const String &p_public_address) {
	Dictionary result;
	std::vector<uint8_t> token;
	result["error"] = session ? error(session->issue_token(uint64_t(p_client_id), text(p_public_address), token)) : ERR_UNCONFIGURED;
	if (!token.empty()) {
		result["token"] = packed(token);
	}
	return result;
}
Error EGPNetSession::poll() {
	if (!session) {
		return ERR_UNCONFIGURED;
	}
	// Keep the facade alive when a signal handler closes its last script reference.
	Ref<EGPNetSession> keep_alive(this);
	return error(session->pump());
}
void EGPNetSession::stop() {
	Ref<EGPNetSession> keep_alive(this);
	if (session) {
		session->stop();
	}
}
void EGPNetSession::close() {
	Ref<EGPNetSession> keep_alive(this);
	stop();
}
String EGPNetSession::get_state() const {
	return session ? String(session->state().c_str()) : "Unconfigured";
}
String EGPNetSession::get_fingerprint() const {
	return session ? String(session->fingerprint().c_str()) : String();
}
Dictionary EGPNetSession::get_statistics() const {
	Dictionary out;
	if (!session) {
		return out;
	}
	const auto s = session->statistics();
	out["tick"] = int64_t(s.tick);
	out["server_tick"] = int64_t(s.server_tick);
	out["peers"] = s.peers;
	out["local_port"] = s.local_port;
	out["received_messages"] = int64_t(s.received_messages);
	out["received_bytes"] = int64_t(s.received_bytes);
	out["rejected_messages"] = int64_t(s.rejected_messages);
	return out;
}
Error EGPNetSession::send_application(int64_t p_peer, const PackedByteArray &p_payload) {
	return session ? error(session->send(p_peer, bytes(p_payload), 0, 2, true)) : ERR_UNCONFIGURED;
}
Variant EGPNetSession::command(const StringName &p_operation, const Dictionary &args) {
	Ref<EGPNetSession> keep_alive(this);
	if (!session) {
		return ERR_UNCONFIGURED;
	}
	if (p_operation == "peers") {
		Array out;
		for (const auto &peer : session->peers()) {
			Dictionary row;
			row["peer_id"] = peer.id;
			row["client_id"] = int64_t(peer.client_id);
			row["ping_ms"] = peer.ping_ms;
			out.push_back(row);
		}
		return out;
	}
	if (p_operation == "entities") {
		Array out;
		for (const auto &entry : session->entities()) {
			out.push_back(entity_row(entry.second));
		}
		return out;
	}
	if (p_operation == "entity") {
		const auto entity = session->entity(int64_t(args.get("entity", 0)));
		return entity ? entity_row(*entity) : Dictionary();
	}
	if (p_operation == "spawn") {
		Dictionary out;
		uint64_t handle = 0;
		out["error"] = error(session->spawn(args.get("kind", -1), bytes(args.get("state", PackedByteArray())), args.get("authority_peer", -1), handle));
		out["entity"] = int64_t(handle);
		return out;
	}
	if (p_operation == "update_entity") {
		return error(session->update(int64_t(args.get("entity", 0)), bytes(args.get("state", PackedByteArray()))));
	}
	if (p_operation == "despawn") {
		return error(session->despawn(int64_t(args.get("entity", 0))));
	}
	if (p_operation == "disconnect") {
		return error(session->disconnect(args.get("peer", -1)));
	}
	if (p_operation == "set_visible") {
		return error(session->set_visible(int64_t(args.get("entity", 0)), args.get("peer", -1), args.get("visible", true)));
	}
	if (p_operation == "set_replication_priority") {
		const Variant entity = args.get("entity", 0), priority = args.get("priority", 1);
		if (entity.get_type() != Variant::INT || priority.get_type() != Variant::INT || int64_t(entity) < 1 || int64_t(priority) < 1 || int64_t(priority) > 16) {
			return ERR_INVALID_PARAMETER;
		}
		return error(session->set_replication_priority(int64_t(entity), int(priority)));
	}
	if (p_operation == "set_peer_replication_budget") {
		const Variant peer = args.get("peer", -1), budget = args.get("bytes_per_second", 0);
		if (peer.get_type() != Variant::INT || budget.get_type() != Variant::INT || int64_t(budget) < 0 || int64_t(budget) > INT32_MAX) {
			return ERR_INVALID_PARAMETER;
		}
		return error(session->set_peer_replication_budget(int64_t(peer), int(budget)));
	}
	if (p_operation == "replication_peer_statistics") {
		Dictionary out;
		const auto stats = session->replication_statistics(args.get("peer", -1));
		if (stats) {
			out["bytes_per_second"] = stats->bytes_per_second;
			out["available_bytes"] = stats->available_bytes;
			out["sent_updates"] = int64_t(stats->sent_updates);
			out["sent_bytes"] = int64_t(stats->sent_bytes);
			out["budget_deferrals"] = int64_t(stats->budget_deferrals);
		}
		return out;
	}
	if (p_operation == "send_packet") {
		return error(session->send(args.get("peer", -1), bytes(args.get("payload", PackedByteArray())), args.get("channel", 0), args.get("delivery", 2)));
	}
	return ERR_INVALID_PARAMETER;
}
void EGPNetSession::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure", "options"), &EGPNetSession::configure, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("listen", "port", "bind_address"), &EGPNetSession::listen, DEFVAL(10515), DEFVAL("0.0.0.0"));
	ClassDB::bind_method(D_METHOD("connect_to_server", "address", "port"), &EGPNetSession::connect_to_server, DEFVAL(10515));
	ClassDB::bind_method(D_METHOD("connect_token", "client_id", "token", "bind_address"), &EGPNetSession::connect_token, DEFVAL("0.0.0.0"));
	ClassDB::bind_method(D_METHOD("issue_token", "client_id", "public_address"), &EGPNetSession::issue_token);
	ClassDB::bind_method(D_METHOD("poll"), &EGPNetSession::poll);
	ClassDB::bind_method(D_METHOD("stop"), &EGPNetSession::stop);
	ClassDB::bind_method(D_METHOD("close"), &EGPNetSession::close);
	ClassDB::bind_method(D_METHOD("get_state"), &EGPNetSession::get_state);
	ClassDB::bind_method(D_METHOD("get_fingerprint"), &EGPNetSession::get_fingerprint);
	ClassDB::bind_method(D_METHOD("get_statistics"), &EGPNetSession::get_statistics);
	ClassDB::bind_method(D_METHOD("send_application", "peer_id", "payload"), &EGPNetSession::send_application);
	ClassDB::bind_method(D_METHOD("command", "operation", "arguments"), &EGPNetSession::command, DEFVAL(Dictionary()));
	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::STRING, "state")));
	ADD_SIGNAL(MethodInfo("peer_connected", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("peer_disconnected", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("application_received", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload")));
	ADD_SIGNAL(MethodInfo("packet_received", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload"), PropertyInfo(Variant::INT, "channel"), PropertyInfo(Variant::INT, "delivery")));
	ADD_SIGNAL(MethodInfo("simulation_tick", PropertyInfo(Variant::INT, "tick"), PropertyInfo(Variant::BOOL, "server")));
	ADD_SIGNAL(MethodInfo("diagnostic", PropertyInfo(Variant::STRING, "message")));
}
