/**************************************************************************/
/*  superposition_world.h                                                 */
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

#pragma once

#include "egp_net_session.h"

#include "scene/main/node.h"

/// Inspector session owner. Authentication tokens are supplied at runtime.
class SuperpositionWorld : public Node {
	GDCLASS(SuperpositionWorld, Node);
	Ref<EGPNetSession> session;
	uint64_t session_generation = 0;
	bool auto_start = false;
	bool auto_poll = true;
	bool allow_insecure_loopback = false;
	int role = 0;
	int port = 10515;
	int tick_rate = 60;
	int max_players = 64;
	int max_entities = 4096;
	String address = "127.0.0.1";
	String game_protocol = "superposition-v1";
	String simulation_fingerprint = "script-state-v1";
	int configured_tick_rate = 60;
	String configured_simulation_fingerprint;
	String last_error;
	Error report(Error p_error, const String &p_action);
	void on_state(const String &p_state) { emit_signal("state_changed", p_state); }
	void on_connected(int64_t p_peer) { emit_signal("peer_connected", p_peer); }
	void on_disconnected(int64_t p_peer) { emit_signal("peer_disconnected", p_peer); }
	void on_application(int64_t p_peer, const PackedByteArray &p_payload) { emit_signal("application_received", p_peer, p_payload); }
	void on_packet(int64_t p_peer, const PackedByteArray &p_payload, int p_channel, int p_delivery) { emit_signal("packet_received", p_peer, p_payload, p_channel, p_delivery); }
	void on_tick(int64_t p_tick, bool p_server) { emit_signal("simulation_tick", p_tick, p_server); }
	void on_diagnostic(const String &p_message) { emit_signal("diagnostic", p_message); }

protected:
	static void _bind_methods();
	void _notification(int p_what);
	bool _get(const StringName &p_name, Variant &r_value) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	void set_auto_start(bool p_value) { auto_start = p_value; }
	bool is_auto_start() const { return auto_start; }
	void set_auto_poll(bool p_value) {
		auto_poll = p_value;
		if (is_inside_tree()) {
			set_process(p_value);
		}
	}
	bool is_auto_poll() const { return auto_poll; }
	void set_allow_insecure_loopback(bool p_value) {
		allow_insecure_loopback = p_value;
		update_configuration_warnings();
	}
	bool is_allow_insecure_loopback() const { return allow_insecure_loopback; }
	void set_role(int p_value) {
		role = CLAMP(p_value, 0, 1);
		update_configuration_warnings();
	}
	int get_role() const { return role; }
	void set_port(int p_value) { port = CLAMP(p_value, 0, 65535); }
	int get_port() const { return port; }
	void set_tick_rate(int p_value) { tick_rate = CLAMP(p_value, 1, 240); }
	int get_tick_rate() const { return session.is_valid() ? configured_tick_rate : tick_rate; }
	void set_max_players(int p_value) { max_players = CLAMP(p_value, 1, 64); }
	int get_max_players() const { return max_players; }
	void set_max_entities(int p_value) { max_entities = CLAMP(p_value, 1, 4096); }
	int get_max_entities() const { return max_entities; }
	void set_address(const String &p_value) { address = p_value; }
	String get_address() const { return address; }
	void set_game_protocol(const String &p_value) { game_protocol = p_value; }
	String get_game_protocol() const { return game_protocol; }
	void set_simulation_fingerprint(const String &p_value) { simulation_fingerprint = p_value; }
	String get_simulation_fingerprint() const { return session.is_valid() ? configured_simulation_fingerprint : simulation_fingerprint; }
	Error configure(const Dictionary &p_options = Dictionary());
	Error start();
	Error start_server();
	Error join_token(int64_t p_client_id, const PackedByteArray &p_token, const String &p_binding = "0.0.0.0");
	Error join_loopback();
	Error poll();
	void stop();
	Ref<EGPNetSession> get_session() const { return session; }
	Dictionary get_statistics() const;
	PackedStringArray get_configuration_warnings() const override;
};
