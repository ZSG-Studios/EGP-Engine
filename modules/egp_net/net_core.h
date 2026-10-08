/**************************************************************************/
/*  net_core.h                                                            */
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
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace egp::net {
enum class Result { Ok,
	Invalid,
	Busy,
	Unconfigured,
	Unauthorized,
	Full,
	NotFound,
	BadData,
	Failed };
struct Options {
	int tick_rate = 60, max_players = 32, max_entities = 1024;
	// Per-peer admission/delivery quotas in independent one-second windows.
	// Bytes are estimated envelope charges, not UDP/retransmission bandwidth.
	int messages_per_second = 1000, bytes_per_second = 4 * 1024 * 1024;
	int timeout_seconds = 5, token_lifetime_seconds = 30;
	std::string game_protocol = "egp-game-v1", simulation_fingerprint = "script-state-v1";
	bool allow_insecure_loopback = false;
	std::array<uint8_t, 32> private_key{};
	bool has_private_key = false;
	float simulated_loss = 0, simulated_latency_ms = 0, simulated_jitter_ms = 0;
};
struct Entity {
	uint64_t handle = 0, revision = 1, tick = 0;
	int kind = 0;
	int64_t authority_peer = -1;
	std::vector<uint8_t> state;
};
struct ReplicationStatistics {
	int bytes_per_second = 0;
	double available_bytes = 0;
	uint64_t sent_updates = 0, sent_bytes = 0, budget_deferrals = 0;
	uint64_t delta_updates = 0, delta_bytes_saved = 0, full_state_updates = 0;
	size_t baseline_bytes = 0;
};
struct Peer {
	int64_t id;
	uint64_t client_id;
	float ping_ms;
};
struct Statistics {
	uint64_t received_messages = 0, received_bytes = 0, rejected_messages = 0, tick = 0, server_tick = 0;
	int peers = 0, local_port = 0;
};
/// Engine-independent native replication and bounded Yojimbo message transport.
/// All operations belong to the constructing thread. Tokens and keys are not diagnostics.
class Session {
	struct Impl;
	std::unique_ptr<Impl> impl;

public:
	static constexpr int MaxStateBytes = 4096, MaxReliableBytes = 4096, MaxUnreliableBytes = 900;
	static constexpr size_t MaxDeltaBaselineBytesPerPeer = 1024 * 1024;
	explicit Session(const Options &options = Options());
	~Session();
	Session(const Session &) = delete;
	Session &operator=(const Session &) = delete;
	Result validation() const;
	Result listen(int port = 10515, const std::string &binding = "0.0.0.0");
	Result connect_loopback(const std::string &host, int port);
	Result connect_token(uint64_t client_id, const std::vector<uint8_t> &token, const std::string &binding = "0.0.0.0");
	Result issue_token(uint64_t client_id, const std::string &public_address, std::vector<uint8_t> &out);
	Result pump();
	Result stop();
	Result disconnect(int64_t peer);
	Result send(int64_t peer, const std::vector<uint8_t> &data, int channel = 0, int delivery = 2, bool application = false);
	Result spawn(int kind, const std::vector<uint8_t> &state, int64_t authority, uint64_t &handle);
	Result update(uint64_t handle, const std::vector<uint8_t> &state);
	Result despawn(uint64_t handle);
	Result set_visible(uint64_t handle, int64_t peer, bool visible);
	Result set_replication_priority(uint64_t handle, int priority);
	Result set_entity_delta_replication(uint64_t handle, bool enabled);
	Result set_peer_replication_budget(int64_t peer, int bytes_per_second);
	std::optional<ReplicationStatistics> replication_statistics(int64_t peer) const;
	std::map<uint64_t, Entity> entities() const;
	std::optional<Entity> entity(uint64_t handle) const;
	std::vector<Peer> peers() const;
	Statistics statistics() const;
	std::string state() const;
	std::string fingerprint() const;
	bool is_pumping() const;
	std::function<void(const std::string &)> state_changed;
	std::function<void(int64_t)> peer_connected, peer_disconnected;
	std::function<void(int64_t, const std::vector<uint8_t> &)> application_received;
	std::function<void(int64_t, const std::vector<uint8_t> &, int, int)> packet_received;
	std::function<void(uint64_t, bool)> simulation_tick;
	std::function<void(const std::string &)> diagnostic;
};
} //namespace egp::net
