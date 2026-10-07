/**************************************************************************/
/*  fairness_check.cpp                                                    */
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

#include "net_core.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace egp::net;
void check(bool good, const char *name) {
	if (!good) {
		std::cerr << "FAILED: " << name << std::endl;
		std::exit(1);
	}
}
int main(int argc, char **argv) {
	bool abrupt = false, symmetric = false, frame_paced = false;
	for (int i = 1; i < argc; ++i) {
		const std::string argument = argv[i];
		if (argument == "--abrupt") {
			abrupt = true;
		} else if (argument == "--symmetric") {
			symmetric = true;
		} else if (argument == "--frame-paced") {
			frame_paced = true;
		} else {
			check(false, "unknown fairness fixture option");
		}
	}
	Options options;
	options.max_players = 2;
	options.max_entities = 64;
	options.messages_per_second = 32;
	options.bytes_per_second = 8192;
	Options receiver = options;
	receiver.messages_per_second = symmetric ? 32 : 1000;
	Session server(options), first(receiver), second(receiver);
	std::array<Session *, 2> clients{ &first, &second };
	std::array<int64_t, 2> owners{};
	std::array<std::vector<uint64_t>, 2> handles;
	bool pump_first = true;
	auto until = [&](auto done, int milliseconds = 5000) {
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
		do {
			check(server.pump() == Result::Ok, "server pump");
			for (auto *client : clients) {
				if (client == &first && !pump_first) {
					continue;
				}
				check(client->pump() == Result::Ok, "client pump");
			}
			if (done()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(frame_paced ? 16 : 2));
		} while (std::chrono::steady_clock::now() < end);
		return false;
	};
	auto admit = [&](int index) {
		std::vector<uint8_t> token;
		check(server.issue_token(10000 + index, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "fresh encrypted admission");
		check(clients[index]->connect_token(10000 + index, token) == Result::Ok, "encrypted join");
	};
	server.peer_connected = [&](int64_t peer) {
		for (const auto &record : server.peers()) {
			if (record.id != peer) {
				continue;
			}
			const int index = int(record.client_id - 10000);
			check(index >= 0 && index < 2, "account identity");
			owners[index] = peer;
			if (!handles[index].empty()) {
				return;
			}
			for (int entity = 0; entity < 32; ++entity) {
				uint64_t handle = 0;
				check(server.spawn(1, { uint8_t(index), uint8_t(entity), 0, 0 }, peer, handle) == Result::Ok, "owned spawn");
				handles[index].push_back(handle);
			}
		}
	};
	server.simulation_tick = [&](uint64_t tick, bool) {
		for (int index = 0; index < 2; ++index) {
			for (int entity = 0; entity < int(handles[index].size()); ++entity) {
				check(server.update(handles[index][entity], { uint8_t(index), uint8_t(entity), uint8_t(tick), uint8_t(tick >> 8) }) == Result::Ok, "continuous 60 Hz update");
			}
		}
	};
	check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
	admit(0);
	admit(1);
	check(until([&] {
		return first.state() == "Connected" && second.state() == "Connected" && first.entities().size() == 64 && second.entities().size() == 64;
	}),
			"both rate-limited baselines complete while all entities keep changing");
	for (int index = 0; index < 2; ++index) {
		for (uint64_t handle : handles[index]) {
			for (auto *client : clients) {
				check(client->entities().at(handle).authority_peer == owners[index], "all owned entities replicated");
			}
		}
	}
	const uint64_t tick = server.statistics().tick;
	check(until([&] {
		for (auto *client : clients) {
			for (const auto &record : client->entities()) {
				if (record.second.tick <= tick) {
					return false;
				}
			}
		}
		return true;
	}),
			"every entity advances under the minimum outgoing message budget");
	for (int cycle = 0; cycle < 2; ++cycle) {
		for (uint64_t handle : handles[0]) {
			check(server.set_visible(handle, owners[1], false) == Result::Ok, "interest hide");
		}
		check(until([&] { return second.entities().size() == 32; }), "hidden entities removed under pressure");
		for (uint64_t handle : handles[0]) {
			check(!second.entities().count(handle), "no hidden membership");
		}
		for (uint64_t handle : handles[0]) {
			check(server.set_visible(handle, owners[1], true) == Result::Ok, "interest reentry");
		}
		check(until([&] { return second.entities().size() == 64; }), "all reentered entities arrive under pressure");
		const int64_t revoked = owners[0];
		if (abrupt) {
			pump_first = false; // No disconnect packet or subsequent heartbeat.
		} else {
			check(first.stop() == Result::Ok, "disconnect original owner");
		}
		// UDP disconnect notifications may be lost. Detection must have its
		// configured timeout before the independent replication deadline starts.
		check(until([&] { return server.peers().size() == 1; }, options.timeout_seconds * 1000 + 1000),
				"original owner disconnect detected within its transport timeout");
		for (uint64_t handle : handles[0]) {
			check(server.entities().at(handle).authority_peer == -1, "server revokes authority when the owner disconnects");
		}
		check(until([&] {
			const auto records = second.entities();
			for (uint64_t handle : handles[0]) {
				if (records.at(handle).authority_peer != -1) {
					return false;
				}
			}
			return true;
		}),
				"ownership revocation reaches the other client under pressure");
		if (abrupt) {
			check(first.stop() == Result::Ok, "retired client cleanup");
			pump_first = true;
		}
		admit(0);
		check(until([&] { return first.state() == "Connected" && first.entities().size() == 64; }), "fresh reconnect baseline completes under pressure");
		check(owners[0] != revoked, "reused transport slot receives a fresh peer identity");
		for (auto *client : clients) {
			for (uint64_t handle : handles[0]) {
				check(client->entities().at(handle).authority_peer == -1, "reconnected account does not inherit revoked authority");
			}
		}
	}
	server.simulation_tick = nullptr;
	for (auto *client : clients) {
		check(client->stop() == Result::Ok, "client cleanup");
	}
	check(server.stop() == Result::Ok, "server cleanup");
	check(server.entities().empty() && server.peers().empty(), "world and peers clear on stop");
	std::cout << "EGP_FAIRNESS_CHECKS=passed entities=64 clients=2 reconnects=2 interest_cycles=2 messages_per_second=32 abrupt=" << abrupt << " pump_ms=" << (frame_paced ? 16 : 2) << std::endl;
}
