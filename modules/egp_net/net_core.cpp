/**************************************************************************/
/*  net_core.cpp                                                          */
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

// SPDX-License-Identifier: MIT
#include "net_core.h"

#include "netcode.h"
#include "sodium.h"
#include "yojimbo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace egp::net {
namespace {
std::recursive_mutex library_mutex;
int library_users = 0;
bool library_ready = false;
double now() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
enum { MetaType,
	StateType,
	DataType,
	InlineStateType,
	MessageTypes };
constexpr int MaxInlineStateBytes = 128;
enum { BeginBaseline,
	EndBaseline,
	DestroyEntity };
struct Meta : yojimbo::Message {
	int action = BeginBaseline;
	uint64_t handle = 0, tick = 0;
	template <typename Stream>
	bool Serialize(Stream &stream) {
		serialize_int(stream, action, BeginBaseline, DestroyEntity);
		serialize_uint64(stream, handle);
		serialize_uint64(stream, tick);
		return true;
	}
	YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
struct State : yojimbo::BlockMessage {
	// The queue retains this ticket until the reliable message is acknowledged
	// (or its connection is reset). Links keep only a weak reference, so they
	// can coalesce later revisions without holding a transport message alive.
	std::shared_ptr<uint8_t> in_flight;
	uint64_t handle = 0, revision = 0, tick = 0, authority = UINT64_MAX;
	int kind = 0;
	template <typename Stream>
	bool Serialize(Stream &stream) {
		serialize_uint64(stream, handle);
		serialize_uint64(stream, revision);
		serialize_uint64(stream, tick);
		serialize_uint64(stream, authority);
		serialize_int(stream, kind, 0, 0x7fffffff);
		return true;
	}
	YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
struct InlineState : yojimbo::Message {
	// The queue retains this ticket until the reliable message is acknowledged
	// (or its connection is reset). Links keep only a weak reference, so they
	// can coalesce later revisions without holding a transport message alive.
	std::shared_ptr<uint8_t> in_flight;
	uint64_t handle = 0, revision = 0, tick = 0, authority = UINT64_MAX;
	int kind = 0, size = 0;
	std::array<uint8_t, MaxInlineStateBytes> data{};
	template <typename Stream>
	bool Serialize(Stream &stream) {
		serialize_uint64(stream, handle);
		serialize_uint64(stream, revision);
		serialize_uint64(stream, tick);
		serialize_uint64(stream, authority);
		serialize_int(stream, kind, 0, 0x7fffffff);
		serialize_int(stream, size, 0, MaxInlineStateBytes);
		serialize_bytes(stream, data.data(), size);
		return true;
	}
	YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
struct Data : yojimbo::BlockMessage {
	int application = 0, user_channel = 0, delivery = 2;
	template <typename Stream>
	bool Serialize(Stream &stream) {
		serialize_int(stream, application, 0, 1);
		serialize_int(stream, user_channel, 0, 3);
		serialize_int(stream, delivery, 2, 4);
		return delivery == 2 || delivery == 4;
	}
	YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
YOJIMBO_MESSAGE_FACTORY_START(Factory, MessageTypes);
YOJIMBO_DECLARE_MESSAGE_TYPE(MetaType, Meta);
YOJIMBO_DECLARE_MESSAGE_TYPE(StateType, State);
YOJIMBO_DECLARE_MESSAGE_TYPE(DataType, Data);
YOJIMBO_DECLARE_MESSAGE_TYPE(InlineStateType, InlineState);
YOJIMBO_MESSAGE_FACTORY_FINISH();
bool loopback(const std::string &address) {
	return address == "127.0.0.1" || address == "::1";
}
uint64_t protocol_hash(const Options &o) {
	uint64_t hash = 14695981039346656037ULL;
	const std::string text = "egp-native-wire-2-inline-state-128|yojimbo-272153a|state-4096|" + o.game_protocol + "|" +
			o.simulation_fingerprint + "|" + std::to_string(o.tick_rate);
	for (unsigned char byte : text) {
		hash ^= byte;
		hash *= 1099511628211ULL;
	}
	return hash;
}
} //namespace

struct Session::Impl : yojimbo::Adapter {
	Session &facade;
	Options options;
	std::thread::id owner = std::this_thread::get_id();
	yojimbo::ClientServerConfig config;
	std::unique_ptr<yojimbo::Server> server;
	std::unique_ptr<yojimbo::Client> client;
	std::array<uint8_t, 32> server_key{};
	Result valid = Result::Ok;
	std::string status = "Stopped";
	bool acquired = false, pumping = false, stopping = false, stop_pending = false, client_connected = false;
	double last_time = 0, accumulator = 0, connected_since = 0;
	uint64_t peer_generation = 0, entity_generation = 0;
	std::map<uint64_t, Entity> entity_map;
	struct Incoming {
		int type = -1, action = -1, user_channel = 0, delivery = 2, application = 0;
		uint64_t handle = 0, tick = 0;
		Entity entity;
		std::vector<uint8_t> payload;
		size_t charge() const { return payload.size() + ((type == StateType || type == InlineStateType) ? 64 : 32); }
	};
	struct Link {
		int64_t handle = 0;
		bool begun = false, complete = false, rejected = false;
		uint64_t last_entity_sent = 0;
		std::map<uint64_t, uint64_t> revisions;
		std::map<uint64_t, std::weak_ptr<uint8_t>> in_flight_states;
		std::set<uint64_t> hidden;
		// Decoded traffic may arrive in bursts across sender budget windows.
		// Keep one copied envelope per channel while the receive budget drains;
		// remaining messages stay in Yojimbo's bounded queues.
		std::array<std::optional<Incoming>, 10> pending;
		int next_channel = 0;
		double window = 0, incoming_window = 0;
		int incoming_count = 0, outgoing_count = 0;
		uint64_t incoming_bytes = 0, outgoing_bytes = 0;
	};
	std::map<int, Link> links;
	Link client_link;
	std::vector<int64_t> disconnect_pending;
	Statistics stats;

	Impl(Session &p, const Options &o) : facade(p), options(o) {
		std::lock_guard<std::recursive_mutex> guard(library_mutex);
		if (o.tick_rate < 1 || o.tick_rate > 240 || o.max_players < 1 || o.max_players > yojimbo::MaxClients ||
				o.max_entities < 1 || o.max_entities > 4096 || o.messages_per_second < 32 || o.bytes_per_second < 8192 ||
				o.timeout_seconds < 1 || o.timeout_seconds > 60 || o.token_lifetime_seconds < 1 || o.token_lifetime_seconds > 120 ||
				o.game_protocol.empty() || o.game_protocol.size() > 256 || o.simulation_fingerprint.empty() ||
				o.simulation_fingerprint.size() > 256 || !std::isfinite(o.simulated_loss) || o.simulated_loss < 0 || o.simulated_loss > 100 ||
				!std::isfinite(o.simulated_latency_ms) || o.simulated_latency_ms < 0 || o.simulated_latency_ms > 5000 ||
				!std::isfinite(o.simulated_jitter_ms) || o.simulated_jitter_ms < 0 || o.simulated_jitter_ms > 5000) {
			valid = Result::Invalid;
			return;
		}
		if (library_users == 0) {
			library_ready = InitializeYojimbo();
		}
		if (!library_ready) {
			valid = Result::Failed;
			return;
		}
		++library_users;
		acquired = true;
		config.protocolId = protocol_hash(o);
		config.timeout = o.timeout_seconds;
		config.maxConnectTokenLifetime = o.token_lifetime_seconds;
		config.numChannels = 10;
		config.clientMemory = config.serverPerClientMemory = 4 * 1024 * 1024;
		config.serverGlobalMemory = 10 * 1024 * 1024;
		config.networkSimulator = o.simulated_loss > 0 || o.simulated_latency_ms > 0 || o.simulated_jitter_ms > 0;
		config.maxSimulatorPackets = 512;
		for (int i = 0; i < config.numChannels; ++i) {
			auto &c = config.channel[i];
			c.type = i >= 3 && i % 2 == 1 ? yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED : yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
			c.messageSendQueueSize = c.messageReceiveQueueSize = 128;
			c.maxMessagesPerPacket = 32;
			c.maxBlockSize = c.type == yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED ? MaxReliableBytes + 1 : MaxUnreliableBytes + 1;
			c.blockFragmentSize = 1000;
			c.sentPacketBufferSize = 1024;
		}
	}
	~Impl() {
		std::lock_guard<std::recursive_mutex> guard(library_mutex);
		shutdown();
		sodium_memzero(server_key.data(), server_key.size());
		sodium_memzero(options.private_key.data(), options.private_key.size());
		if (acquired && --library_users == 0) {
			ShutdownYojimbo();
			library_ready = false;
		}
	}
	bool on_owner() const { return owner == std::this_thread::get_id(); }
	void change(const std::string &s) {
		if (status != s) {
			status = s;
			if (facade.state_changed) {
				facade.state_changed(s);
			}
		}
	}
	yojimbo::MessageFactory *CreateMessageFactory(yojimbo::Allocator &a) override { return YOJIMBO_NEW(a, Factory, a); }
	void OnServerClientConnected(int slot) override {
		Link link;
		link.handle = int64_t((++peer_generation << 8) | uint64_t(slot + 1));
		link.window = now();
		links[slot] = std::move(link);
		if (facade.peer_connected) {
			facade.peer_connected(links[slot].handle);
		}
	}
	void OnServerClientDisconnected(int slot) override {
		auto it = links.find(slot);
		if (it == links.end()) {
			return;
		}
		const int64_t peer = it->second.handle;
		links.erase(it);
		// Revoked ownership cannot transfer to a new connection reusing this slot.
		for (auto &pair : entity_map) {
			if (pair.second.authority_peer == peer) {
				pair.second.authority_peer = -1;
				++pair.second.revision;
			}
		}
		if (facade.peer_disconnected) {
			facade.peer_disconnected(peer);
		}
	}
	int slot_for(int64_t peer) const {
		for (const auto &pair : links) {
			if (pair.second.handle == peer) {
				return pair.first;
			}
		}
		return -1;
	}
	void shutdown() {
		if (stopping) {
			return;
		}
		stopping = true;
		if (server) {
			server->Stop();
		}
		if (client) {
			client->Disconnect();
		}
		server.reset();
		client.reset();
		links.clear();
		entity_map.clear();
		client_link = Link();
		client_connected = false;
		accumulator = 0;
		stats.tick = stats.server_tick = 0;
		stopping = false;
		stop_pending = false;
		change("Stopped");
	}
	void window(Link &link) {
		const double time = now();
		if (time - link.window >= 1) {
			link.window = time;
			link.outgoing_count = 0;
			link.outgoing_bytes = 0;
		}
	}
	bool room(int slot, int channel, size_t bytes) {
		Link &link = server ? links.at(slot) : client_link;
		window(link);
		if (link.outgoing_count >= options.messages_per_second || link.outgoing_bytes + bytes > uint64_t(options.bytes_per_second)) {
			return false;
		}
		return server ? server->CanSendMessage(slot, channel) : client && client->IsConnected() && client->CanSendMessage(channel);
	}
	yojimbo::Message *create(int slot, int type) { return server ? server->CreateMessage(slot, type) : client->CreateMessage(type); }
	void release(int slot, yojimbo::Message *message) {
		if (server) {
			server->ReleaseMessage(slot, message);
		} else {
			client->ReleaseMessage(message);
		}
	}
	bool attach(int slot, yojimbo::Message *message, const std::vector<uint8_t> &data) {
		uint8_t *block = server ? server->AllocateBlock(slot, int(data.size()) + 1) : client->AllocateBlock(int(data.size()) + 1);
		if (!block) {
			return false;
		}
		block[0] = 1; // Explicit frame version, including an empty application/state payload.
		if (!data.empty()) {
			std::memcpy(block + 1, data.data(), data.size());
		}
		if (server) {
			server->AttachBlockToMessage(slot, message, block, int(data.size()) + 1);
		} else {
			client->AttachBlockToMessage(message, block, int(data.size()) + 1);
		}
		return true;
	}
	void queue(int slot, int channel, yojimbo::Message *message, size_t bytes) {
		Link &link = server ? links.at(slot) : client_link;
		++link.outgoing_count;
		link.outgoing_bytes += bytes;
		if (server) {
			server->SendMessage(slot, channel, message);
		} else {
			client->SendMessage(channel, message);
		}
	}
	bool meta(int slot, int action, uint64_t handle = 0) {
		if (!room(slot, 0, 32)) {
			return false;
		}
		auto *m = static_cast<Meta *>(create(slot, MetaType));
		if (!m) {
			return false;
		}
		m->action = action;
		m->handle = handle;
		m->tick = stats.tick;
		queue(slot, 0, m, 32);
		return true;
	}
	void replicate() {
		for (auto &pair : links) {
			const int slot = pair.first;
			Link &link = pair.second;
			if (link.rejected) {
				continue;
			}
			if (!link.begun) {
				if (!meta(slot, BeginBaseline)) {
					continue;
				}
				link.begun = true;
			}
			for (auto it = link.revisions.begin(); it != link.revisions.end();) {
				if (!entity_map.count(it->first) || link.hidden.count(it->first)) {
					if (!meta(slot, DestroyEntity, it->first)) {
						break;
					}
					link.in_flight_states.erase(it->first);
					it = link.revisions.erase(it);
				} else {
					++it;
				}
			}
			auto baseline_ready = [&] {
				return std::all_of(entity_map.begin(), entity_map.end(), [&](const auto &record) {
					return link.hidden.count(record.first) || link.revisions.count(record.first);
				});
			};
			// Finish the ordered initial snapshot before updates consume the next
			// budget window. It need not wait for a changing world to become idle.
			if (!link.complete && baseline_ready() && meta(slot, EndBaseline)) {
				link.complete = true;
			}
			auto next = entity_map.upper_bound(link.last_entity_sent);
			for (size_t remaining = entity_map.size(); remaining > 0; --remaining) {
				if (next == entity_map.end()) {
					next = entity_map.begin();
				}
				const Entity &entity = (next++)->second;
				if (link.hidden.count(entity.handle)) {
					continue;
				}
				auto revision = link.revisions.find(entity.handle);
				if (revision != link.revisions.end() && revision->second == entity.revision) {
					continue;
				}
				// One queued state per entity/peer bounds stale revisions under
				// latency and leaves capacity for newly spawned owned entities.
				auto pending = link.in_flight_states.find(entity.handle);
				if (pending != link.in_flight_states.end() && !pending->second.expired()) {
					continue;
				}
				if (!room(slot, 0, entity.state.size() + 64)) {
					break;
				}
				auto fill = [&](auto *message) {
					message->handle = entity.handle;
					message->revision = entity.revision;
					message->tick = entity.tick;
					message->authority = uint64_t(entity.authority_peer);
					message->kind = entity.kind;
					message->in_flight = std::make_shared<uint8_t>(0);
					link.in_flight_states[entity.handle] = message->in_flight;
				};
				if (entity.state.size() <= MaxInlineStateBytes) {
					auto *m = static_cast<InlineState *>(create(slot, InlineStateType));
					if (!m) {
						break;
					}
					fill(m);
					m->size = int(entity.state.size());
					std::copy(entity.state.begin(), entity.state.end(), m->data.begin());
					queue(slot, 0, m, entity.state.size() + 64);
				} else {
					auto *m = static_cast<State *>(create(slot, StateType));
					if (!m) {
						break;
					}
					if (!attach(slot, m, entity.state)) {
						release(slot, m);
						break;
					}
					fill(m);
					queue(slot, 0, m, entity.state.size() + 64);
				}
				link.revisions[entity.handle] = entity.revision;
				// Resume after the last admitted entity when the bounded queue
				// or rate budget fills; lower IDs cannot monopolize each reset.
				link.last_entity_sent = entity.handle;
			}
			if (!link.complete && baseline_ready() && meta(slot, EndBaseline)) {
				link.complete = true;
			}
		}
	}
	void reject(int slot) {
		++stats.rejected_messages;
		if (server) {
			auto it = links.find(slot);
			if (it != links.end()) {
				it->second.rejected = true;
				disconnect_pending.push_back(it->second.handle);
			}
		} else {
			stop_pending = true;
		}
	}
	std::optional<Incoming> decode(int slot, int channel, yojimbo::Message *message) {
		Incoming incoming;
		auto &type = incoming.type;
		auto &entity = incoming.entity;
		auto &action = incoming.action;
		auto &handle = incoming.handle;
		auto &tick = incoming.tick;
		auto &user_channel = incoming.user_channel;
		auto &delivery = incoming.delivery;
		auto &application = incoming.application;
		auto &payload = incoming.payload;
		type = message->GetType();
		bool good = true;
		if (type == MetaType) {
			const auto *m = static_cast<Meta *>(message);
			action = m->action;
			handle = m->handle;
			tick = m->tick;
			good = !server && channel == 0;
		} else if (type == StateType) {
			const auto *m = static_cast<State *>(message);
			entity.handle = m->handle;
			entity.revision = m->revision;
			entity.tick = m->tick;
			entity.authority_peer = int64_t(m->authority);
			entity.kind = m->kind;
			good = !server && channel == 0 && entity.handle > 0 && entity.handle <= 0x7fffffffffffffffULL && entity.revision > 0;
		} else if (type == InlineStateType) {
			const auto *m = static_cast<InlineState *>(message);
			entity.handle = m->handle;
			entity.revision = m->revision;
			entity.tick = m->tick;
			entity.authority_peer = int64_t(m->authority);
			entity.kind = m->kind;
			good = !server && channel == 0 && entity.handle > 0 && entity.handle <= 0x7fffffffffffffffULL && entity.revision > 0 && m->size >= 0 && m->size <= MaxInlineStateBytes;
			if (good) {
				payload.assign(m->data.begin(), m->data.begin() + m->size);
			}
		} else if (type == DataType) {
			const auto *m = static_cast<Data *>(message);
			application = m->application;
			user_channel = m->user_channel;
			delivery = m->delivery;
			good = application ? channel == 1 && delivery == 2 : channel == 2 + user_channel * 2 + (delivery == 4 ? 1 : 0);
		} else {
			good = false;
		}
		if (type == StateType || type == DataType) {
			const auto *m = static_cast<yojimbo::BlockMessage *>(message);
			const int max = delivery == 4 ? MaxUnreliableBytes : MaxReliableBytes;
			const int size = m->GetBlockSize();
			good = good && size >= 1 && size <= max + 1 && m->GetBlockData() && m->GetBlockData()[0] == 1;
			if (good) {
				payload.assign(m->GetBlockData() + 1, m->GetBlockData() + size);
			}
		}
		// Release factory ownership before calling any game code.
		release(slot, message);
		if (!good) {
			++stats.received_messages;
			stats.received_bytes += payload.size();
			reject(slot);
			return std::nullopt;
		}
		return incoming;
	}
	bool receive_one(int slot, int channel) {
		Link &link = server ? links.at(slot) : client_link;
		if (stop_pending || link.rejected) {
			return false;
		}
		const double time = now();
		if (time - link.incoming_window >= 1) {
			link.incoming_window = time;
			link.incoming_count = 0;
			link.incoming_bytes = 0;
		}
		if (link.incoming_count >= options.messages_per_second ||
				link.incoming_bytes + 32 > uint64_t(options.bytes_per_second)) {
			return false;
		}
		auto &pending = link.pending[channel];
		if (!pending) {
			auto *message = server ? server->ReceiveMessage(slot, channel) : client->ReceiveMessage(channel);
			if (!message) {
				return false;
			}
			pending = decode(slot, channel, message);
			if (!pending) {
				return false;
			}
		}
		if (link.incoming_bytes + pending->charge() > uint64_t(options.bytes_per_second)) {
			return false;
		}
		Incoming incoming = std::move(*pending);
		pending.reset();
		++link.incoming_count;
		link.incoming_bytes += incoming.charge();
		++stats.received_messages;
		stats.received_bytes += incoming.payload.size();
		const int64_t peer = server ? link.handle : 0;
		const auto type = incoming.type;
		auto &entity = incoming.entity;
		const auto action = incoming.action;
		const auto handle = incoming.handle;
		const auto tick = incoming.tick;
		const auto user_channel = incoming.user_channel;
		const auto delivery = incoming.delivery;
		const auto application = incoming.application;
		auto &payload = incoming.payload;
		if (type == MetaType) {
			stats.server_tick = std::max(stats.server_tick, tick);
			if (action == BeginBaseline) {
				entity_map.clear();
				change("Synchronizing");
			} else if (action == EndBaseline) {
				change("Connected");
			} else {
				entity_map.erase(handle);
			}
		} else if (type == StateType || type == InlineStateType) {
			if (!entity_map.count(entity.handle) && entity_map.size() >= size_t(options.max_entities)) {
				reject(slot);
				return false;
			}
			stats.server_tick = std::max(stats.server_tick, entity.tick);
			entity.state = std::move(payload);
			auto old = entity_map.find(entity.handle);
			if (old == entity_map.end() || old->second.revision < entity.revision) {
				entity_map[entity.handle] = std::move(entity);
			}
		} else if (application) {
			if (facade.application_received) {
				facade.application_received(peer, payload);
			}
		} else if (facade.packet_received) {
			facade.packet_received(peer, payload, user_channel, delivery);
		}
		return true;
	}
};

Session::Session(const Options &options) : impl(std::make_unique<Impl>(*this, options)) {}
Session::~Session() {
	state_changed = nullptr;
	peer_connected = nullptr;
	peer_disconnected = nullptr;
	application_received = nullptr;
	packet_received = nullptr;
	simulation_tick = nullptr;
	diagnostic = nullptr;
	// Shutdown must run while the callback fields are still alive (they follow impl).
	impl.reset();
}
Result Session::validation() const {
	return impl->valid;
}
Result Session::listen(int port, const std::string &binding) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner() || p.pumping) {
		return Result::Busy;
	}
	if (p.valid != Result::Ok) {
		return p.valid;
	}
	if (p.server || p.client) {
		return Result::Busy;
	}
	if (port < 0 || port > 65535 || (p.options.allow_insecure_loopback && !loopback(binding))) {
		return Result::Invalid;
	}
	yojimbo::Address address(binding.c_str(), uint16_t(port));
	if (!address.IsValid()) {
		return Result::Invalid;
	}
	if (p.options.allow_insecure_loopback) {
		p.server_key.fill(0);
	} else if (p.options.has_private_key) {
		p.server_key = p.options.private_key;
	} else {
		netcode_random_bytes(p.server_key.data(), int(p.server_key.size()));
	}
	p.last_time = now();
	p.server = std::make_unique<yojimbo::Server>(yojimbo::GetDefaultAllocator(), p.server_key.data(), address, p.config, p, p.last_time);
	if (!p.server->Start(p.options.max_players)) {
		p.server.reset();
		return Result::Failed;
	}
	if (p.config.networkSimulator) {
		p.server->SetPacketLoss(p.options.simulated_loss);
		p.server->SetLatency(p.options.simulated_latency_ms);
		p.server->SetJitter(p.options.simulated_jitter_ms);
	}
	p.change("Listening");
	return Result::Ok;
}
Result Session::connect_loopback(const std::string &host, int port) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner() || p.pumping) {
		return Result::Busy;
	}
	if (p.valid != Result::Ok) {
		return p.valid;
	}
	if (p.server || p.client) {
		return Result::Busy;
	}
	if (!p.options.allow_insecure_loopback || !loopback(host)) {
		return Result::Unauthorized;
	}
	if (port < 1 || port > 65535) {
		return Result::Invalid;
	}
	p.last_time = p.connected_since = now();
	p.client = std::make_unique<yojimbo::Client>(yojimbo::GetDefaultAllocator(), yojimbo::Address(host == "::1" ? "::" : "0.0.0.0"), p.config, p, p.last_time);
	uint64_t client_id = 0;
	netcode_random_bytes(reinterpret_cast<uint8_t *>(&client_id), sizeof(client_id));
	std::array<uint8_t, 32> development_key{};
	if (!p.client->InsecureConnect(development_key.data(), client_id, yojimbo::Address(host.c_str(), uint16_t(port)))) {
		p.client.reset();
		return Result::Failed;
	}
	if (p.config.networkSimulator) {
		p.client->SetPacketLoss(p.options.simulated_loss);
		p.client->SetLatency(p.options.simulated_latency_ms);
		p.client->SetJitter(p.options.simulated_jitter_ms);
	}
	p.change("Connecting");
	return Result::Ok;
}
Result Session::connect_token(uint64_t client_id, const std::vector<uint8_t> &token, const std::string &binding) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner() || p.pumping) {
		return Result::Busy;
	}
	if (p.valid != Result::Ok) {
		return p.valid;
	}
	if (p.server || p.client) {
		return Result::Busy;
	}
	if (token.size() != yojimbo::ConnectTokenBytes) {
		return Result::Invalid;
	}
	// The pinned netcode public token header is version[13], then a little-endian
	// protocol ID. Yojimbo otherwise takes its protocol ID from the supplied token,
	// allowing a trusted token to bypass this client's configured wire identity.
	constexpr char token_version[] = "NETCODE 1.02";
	if (std::memcmp(token.data(), token_version, sizeof(token_version)) != 0) {
		return Result::Invalid;
	}
	uint64_t token_protocol = 0;
	for (size_t i = 0; i < sizeof(token_protocol); ++i) {
		token_protocol |= uint64_t(token[sizeof(token_version) + i]) << (i * 8);
	}
	if (token_protocol != p.config.protocolId) {
		if (diagnostic) {
			diagnostic("Admission token does not match this session's networking protocol or simulation fingerprint.");
		}
		return Result::Invalid;
	}
	yojimbo::Address local(binding.c_str());
	if (!local.IsValid()) {
		return Result::Invalid;
	}
	p.last_time = p.connected_since = now();
	p.client = std::make_unique<yojimbo::Client>(yojimbo::GetDefaultAllocator(), local, p.config, p, p.last_time);
	auto bytes = token;
	bool connected = p.client->Connect(client_id, bytes.data());
	sodium_memzero(bytes.data(), bytes.size());
	if (!connected) {
		p.client.reset();
		return Result::Failed;
	}
	if (p.config.networkSimulator) {
		p.client->SetPacketLoss(p.options.simulated_loss);
		p.client->SetLatency(p.options.simulated_latency_ms);
		p.client->SetJitter(p.options.simulated_jitter_ms);
	}
	p.change("Connecting");
	return Result::Ok;
}
Result Session::issue_token(uint64_t client_id, const std::string &public_address, std::vector<uint8_t> &out) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner()) {
		return Result::Busy;
	}
	if (!p.server || !p.server->IsRunning()) {
		return Result::Unauthorized;
	}
	yojimbo::Address address(public_address.c_str());
	if (!address.IsValid() || address.GetPort() == 0) {
		return Result::Invalid;
	}
	const char *addresses[] = { public_address.c_str() };
	char internal[yojimbo::MaxAddressLength];
	p.server->GetAddress().ToString(internal, sizeof(internal));
	const char *internal_addresses[] = { internal };
	std::array<uint8_t, NETCODE_USER_DATA_BYTES> user_data{};
	out.resize(yojimbo::ConnectTokenBytes);
	if (netcode_generate_connect_token(1, addresses, internal_addresses, p.options.token_lifetime_seconds, p.options.timeout_seconds,
				client_id, p.config.protocolId, p.server_key.data(), user_data.data(), out.data()) != NETCODE_OK) {
		out.clear();
		return Result::Failed;
	}
	return Result::Ok;
}
Result Session::pump() {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner() || p.pumping) {
		return Result::Busy;
	}
	if (!p.server && !p.client) {
		return p.valid;
	}
	p.pumping = true;
	const bool clock_was_running = p.server || p.status == "Connected";
	const double time = now(), elapsed = std::max(0.0, time - p.last_time);
	p.last_time = time;
	if (p.server) {
		p.server->AdvanceTime(time);
		p.server->ReceivePackets();
	} else {
		p.client->AdvanceTime(time);
		p.client->ReceivePackets();
		if (p.client->IsConnected() && !p.client_connected) {
			p.client_connected = true;
			p.change("Synchronizing");
			if (peer_connected) {
				peer_connected(0);
			}
		}
	}
	for (int slot = 0; slot < (p.server ? p.options.max_players : (p.client && p.client->IsConnected() ? 1 : 0)); ++slot) {
		if (p.server && !p.links.count(slot)) {
			continue;
		}
		auto &link = p.server ? p.links.at(slot) : p.client_link;
		// Give each channel one delivery per round. A busy replication channel
		// cannot consume every receive window ahead of application/raw traffic.
		int empty = 0;
		while (!p.stop_pending && !link.rejected && empty < p.config.numChannels) {
			const int channel = link.next_channel;
			link.next_channel = (channel + 1) % p.config.numChannels;
			if (p.receive_one(slot, channel)) {
				empty = 0;
			} else {
				++empty;
			}
		}
	}
	if (p.server || (p.client_connected && p.status == "Connected")) {
		// Start the client clock only after its authoritative baseline is ready.
		// Authentication/synchronization time is not simulated gameplay debt.
		if (clock_was_running) {
			p.accumulator += elapsed;
		}
		const double step = 1.0 / p.options.tick_rate;
		if (p.accumulator > 0.5) {
			if (diagnostic) {
				diagnostic("Fixed simulation exceeded its catch-up budget; resynchronization required.");
			}
			p.stop_pending = true;
		}
		for (int i = 0; i < 8 && p.accumulator >= step && !p.stop_pending; ++i) {
			p.accumulator -= step;
			++p.stats.tick;
			if (simulation_tick) {
				simulation_tick(p.stats.tick, bool(p.server));
			}
		}
	}
	if (p.server && !p.stop_pending) {
		p.replicate();
		p.server->SendPackets();
	} else if (p.client && !p.stop_pending) {
		p.client->SendPackets();
	}
	auto disconnects = std::move(p.disconnect_pending);
	p.disconnect_pending.clear();
	for (int64_t peer : disconnects) {
		int slot = p.slot_for(peer);
		if (slot >= 0 && p.server) {
			p.server->DisconnectClient(slot);
		}
	}
	if (p.client && (p.client->IsDisconnected() || p.client->ConnectionFailed() || (!p.client_connected && time - p.connected_since > p.options.timeout_seconds))) {
		bool had_peer = p.client_connected;
		p.shutdown();
		p.change("Disconnected");
		if (had_peer && peer_disconnected) {
			peer_disconnected(0);
		}
	}
	p.pumping = false;
	if (p.stop_pending) {
		p.shutdown();
		return Result::Failed;
	}
	return Result::Ok;
}
Result Session::stop() {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	if (!impl->on_owner()) {
		return Result::Busy;
	}
	if (impl->pumping) {
		impl->stop_pending = true;
		return Result::Ok;
	}
	impl->shutdown();
	return Result::Ok;
}
Result Session::disconnect(int64_t peer) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	if (!impl->on_owner()) {
		return Result::Busy;
	}
	if (impl->server) {
		int slot = impl->slot_for(peer);
		if (slot < 0) {
			return Result::NotFound;
		}
		if (impl->pumping) {
			impl->disconnect_pending.push_back(peer);
		} else {
			// Disconnect invokes game callbacks before Yojimbo finishes resetting the slot.
			// Keep teardown deferred until that operation has returned.
			impl->pumping = true;
			impl->server->DisconnectClient(slot);
			impl->pumping = false;
			if (impl->stop_pending) {
				impl->shutdown();
			}
		}
		return Result::Ok;
	}
	return peer == 0 ? stop() : Result::NotFound;
}
Result Session::send(int64_t peer, const std::vector<uint8_t> &data, int channel, int delivery, bool application) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner()) {
		return Result::Busy;
	}
	if (channel < 0 || channel > 3 || (delivery != 2 && delivery != 4) || (application && delivery != 2)) {
		return Result::Invalid;
	}
	if (data.size() > size_t(delivery == 4 ? MaxUnreliableBytes : MaxReliableBytes)) {
		return Result::Full;
	}
	int slot = p.server ? p.slot_for(peer) : 0;
	if (p.server ? slot < 0 : peer != 0 || !p.client || !p.client->IsConnected()) {
		return Result::NotFound;
	}
	const int transport_channel = application ? 1 : 2 + channel * 2 + (delivery == 4 ? 1 : 0);
	if (!p.room(slot, transport_channel, data.size() + 32)) {
		return Result::Busy;
	}
	auto *m = static_cast<Data *>(p.create(slot, DataType));
	if (!m) {
		return Result::Full;
	}
	m->application = application ? 1 : 0;
	m->user_channel = channel;
	m->delivery = delivery;
	if (!p.attach(slot, m, data)) {
		p.release(slot, m);
		return Result::Full;
	}
	p.queue(slot, transport_channel, m, data.size() + 32);
	return Result::Ok;
}
Result Session::spawn(int kind, const std::vector<uint8_t> &state, int64_t authority, uint64_t &handle) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner()) {
		return Result::Busy;
	}
	if (!p.server || !p.server->IsRunning()) {
		return Result::Unauthorized;
	}
	if (kind < 0 || (authority != -1 && p.slot_for(authority) < 0)) {
		return Result::Invalid;
	}
	if (state.size() > MaxStateBytes || p.entity_map.size() >= size_t(p.options.max_entities)) {
		return Result::Full;
	}
	if (p.entity_generation >= 0x7fffffffffffULL) {
		return Result::Full;
	}
	handle = ++p.entity_generation; // Monotonic across reconnects/stops; stale references never alias a new entity.
	Entity entity;
	entity.handle = handle;
	entity.kind = kind;
	entity.state = state;
	entity.authority_peer = authority;
	entity.tick = p.stats.tick;
	p.entity_map[handle] = std::move(entity);
	return Result::Ok;
}
Result Session::update(uint64_t handle, const std::vector<uint8_t> &state) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	auto &p = *impl;
	if (!p.on_owner()) {
		return Result::Busy;
	}
	if (!p.server) {
		return Result::Unauthorized;
	}
	if (state.size() > MaxStateBytes) {
		return Result::Full;
	}
	auto found = p.entity_map.find(handle);
	if (found == p.entity_map.end()) {
		return Result::NotFound;
	}
	if (found->second.state != state) {
		found->second.state = state;
		++found->second.revision;
		found->second.tick = p.stats.tick;
	}
	return Result::Ok;
}
Result Session::despawn(uint64_t handle) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	if (!impl->on_owner()) {
		return Result::Busy;
	}
	if (!impl->server) {
		return Result::Unauthorized;
	}
	if (!impl->entity_map.erase(handle)) {
		return Result::NotFound;
	}
	for (auto &link : impl->links) {
		link.second.hidden.erase(handle);
	}
	return Result::Ok;
}
Result Session::set_visible(uint64_t handle, int64_t peer, bool visible) {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	if (!impl->on_owner()) {
		return Result::Busy;
	}
	if (!impl->server) {
		return Result::Unauthorized;
	}
	int slot = impl->slot_for(peer);
	if (slot < 0 || !impl->entity_map.count(handle)) {
		return Result::NotFound;
	}
	if (visible) {
		impl->links[slot].hidden.erase(handle);
	} else {
		impl->links[slot].hidden.insert(handle);
	}
	return Result::Ok;
}
std::map<uint64_t, Entity> Session::entities() const {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	return impl->on_owner() ? impl->entity_map : std::map<uint64_t, Entity>();
}
std::vector<Peer> Session::peers() const {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	std::vector<Peer> records;
	if (!impl->on_owner()) {
		return records;
	}
	if (impl->server) {
		for (const auto &link : impl->links) {
			yojimbo::NetworkInfo info{};
			impl->server->GetNetworkInfo(link.first, info);
			records.push_back({ link.second.handle, impl->server->GetClientId(link.first), info.RTT });
		}
	} else if (impl->client && impl->client->IsConnected()) {
		yojimbo::NetworkInfo info{};
		impl->client->GetNetworkInfo(info);
		records.push_back({ 0, 0, info.RTT });
	}
	return records;
}
Statistics Session::statistics() const {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	if (!impl->on_owner()) {
		return {};
	}
	Statistics s = impl->stats;
	s.peers = int(peers().size());
	if (impl->server) {
		s.local_port = impl->server->GetAddress().GetPort();
		s.server_tick = s.tick;
	} else if (impl->client) {
		s.local_port = impl->client->GetAddress().GetPort();
	}
	return s;
}
std::string Session::state() const {
	std::lock_guard<std::recursive_mutex> guard(library_mutex);
	return impl->on_owner() ? impl->status : "WrongThread";
}
std::string Session::fingerprint() const {
	std::ostringstream text;
	text << std::hex << impl->config.protocolId;
	return text.str();
}
bool Session::is_pumping() const {
	return impl->pumping;
}
} //namespace egp::net
