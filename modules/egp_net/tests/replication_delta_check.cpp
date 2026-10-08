/**************************************************************************/
/*  replication_delta_check.cpp                                           */
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

#include "../net_core.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace egp::net;
static int checks = 0;
static void check(bool ok, const char *text) {
	if (!ok) {
		std::cerr << "FAILED: " << text << std::endl;
		std::exit(1);
	}
	++checks;
}
int main() {
	Options options;
	options.allow_insecure_loopback = true;
	options.max_players = 3;
	options.max_entities = 300;
	options.bytes_per_second = 16 * 1024 * 1024;
	options.simulated_latency_ms = 35;
	options.simulated_jitter_ms = 10;
	options.simulated_loss = 8;
	Session server(options), first(options), second(options), late(options);
	bool connect_late = false;
	auto until = [&](auto done, int milliseconds = 10000) {
		const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
		do {
			check(server.pump() == Result::Ok && first.pump() == Result::Ok && second.pump() == Result::Ok && (!connect_late || late.pump() == Result::Ok), "native pump remains healthy");
			for (const auto &peer : server.peers()) {
				auto s = server.replication_statistics(peer.id);
				check(s && s->baseline_bytes <= Session::MaxDeltaBaselineBytesPerPeer, "retained acknowledged and pending memory bounded");
			}
			if (done()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		} while (std::chrono::steady_clock::now() < end);
		return false;
	};
	check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
	check(first.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok && second.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "connect independent peers");
	check(until([&] { return first.state() == "Connected" && second.state() == "Connected"; }), "initial admission");
	uint64_t handle = 0;
	std::vector<uint8_t> target(1024, 3);
	check(server.spawn(101, target, -1, handle) == Result::Ok, "spawn one entity");
	check(server.set_entity_delta_replication(handle, true) == Result::Ok, "opt in to ACK deltas");
	check(first.set_entity_delta_replication(handle, true) == Result::Unauthorized, "client cannot set policy");
	check(server.set_entity_delta_replication(UINT64_MAX, true) == Result::NotFound, "stale policy rejects");
	Result foreign = Result::Ok;
	std::thread worker([&] { foreign = server.set_entity_delta_replication(handle, true); });
	worker.join();
	check(foreign == Result::Busy, "creator thread enforced");
	auto same = [&](Session &c) {auto e=c.entity(handle);return e&&e->state==target; };
	check(until([&] { return same(first) && same(second); }), "full initial baselines");
	auto pause = [&](int ms) {const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);return until([&]{return std::chrono::steady_clock::now()>=end;},ms+1000); };
	pause(300);
	for (int round = 1; round <= 4; ++round) {
		for (int skipped = 0; skipped < 25; ++skipped) {
			target[round] = uint8_t(round + skipped);
			check(server.update(handle, target) == Result::Ok, "coalesced update");
			server.pump();
		}
		check(until([&] { return same(first) && same(second); }), "latest state reconstructs after skipped revisions and loss");
		pause(180);
	}
	auto peer = server.peers()[0].id;
	auto stats = server.replication_statistics(peer);
	check(stats && stats->delta_updates >= 3 && stats->delta_bytes_saved > 1000, "deltas materially reduce envelope bytes");
	check(server.set_visible(handle, peer, false) == Result::Ok, "hide membership");
	check(until([&] { return first.entities().empty() || second.entities().empty(); }), "interest exit removes state");
	target[9] = 99;
	server.update(handle, target);
	auto before_full = server.replication_statistics(peer)->full_state_updates;
	check(server.set_visible(handle, peer, true) == Result::Ok, "reenter membership");
	check(until([&] { return same(first) && same(second); }), "full re-entry recovers baseline");
	check(server.replication_statistics(peer)->full_state_updates > before_full, "re-entry sends full state");
	connect_late = true;
	check(late.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "late client joins");
	check(until([&] { return late.state() == "Connected" && same(late); }), "late join gets complete latest baseline");
	pause(180);
	target.assign(1024, 7);
	server.update(handle, target);
	before_full = server.replication_statistics(peer)->full_state_updates;
	check(until([&] { return same(first) && same(second) && same(late); }), "dense changes fall back safely");
	check(server.replication_statistics(peer)->full_state_updates > before_full, "non-beneficial delta uses full envelope");
	target.resize(1031, 11);
	server.update(handle, target);
	check(until([&] { return same(first) && same(second) && same(late); }), "size change full fallback");
	check(server.set_entity_delta_replication(handle, false) == Result::Ok, "disable ACK deltas");
	check(server.replication_statistics(peer)->baseline_bytes == 0, "disable frees retained snapshots");
	target[0] = 33;
	server.update(handle, target);
	check(until([&] { return same(first) && same(second) && same(late); }), "disabled default full-state path");
	check(server.set_entity_delta_replication(handle, true) == Result::Ok, "re-enable policy");
	target[0] = 34;
	server.update(handle, target);
	check(until([&] { return same(first) && same(second) && same(late); }), "reenabling safely seeds a full baseline");
	check(server.despawn(handle) == Result::Ok, "despawn");
	check(until([&] { return first.entities().empty() && second.entities().empty() && late.entities().empty(); }), "teardown after deltas");
	check(server.replication_statistics(peer)->baseline_bytes == 0, "teardown frees snapshots");
	server.stop();
	first.stop();
	second.stop();
	late.stop();
	// Separate no-impairment fill stresses the 1MiB retention cap without claiming WAN capacity.
	options.simulated_loss = 0;
	options.simulated_latency_ms = 0;
	options.simulated_jitter_ms = 0;
	Session memory_server(options), memory_client(options);
	memory_server.listen(0, "127.0.0.1");
	memory_client.connect_loopback("127.0.0.1", memory_server.statistics().local_port);
	std::vector<uint64_t> handles;
	for (int i = 0; i < 270; ++i) {
		uint64_t h = 0;
		check(memory_server.spawn(1, std::vector<uint8_t>(4096, uint8_t(i)), -1, h) == Result::Ok, "large baseline spawn");
		memory_server.set_entity_delta_replication(h, true);
		handles.push_back(h);
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(50);
	while (memory_client.entities().size() != handles.size() && std::chrono::steady_clock::now() < deadline) {
		check(memory_server.pump() == Result::Ok && memory_client.pump() == Result::Ok, "memory stress pump");
		for (const auto &p : memory_server.peers()) {
			check(memory_server.replication_statistics(p.id)->baseline_bytes <= Session::MaxDeltaBaselineBytesPerPeer, "capacity overflow remains bounded");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	check(memory_client.entities().size() == handles.size(), "capacity fallback delivers every full baseline");
	for (auto h : handles) {
		memory_server.despawn(h);
	}
	for (const auto &p : memory_server.peers()) {
		check(memory_server.replication_statistics(p.id)->baseline_bytes == 0, "capacity teardown releases all retained bytes");
	}
	std::cout << "ACK_DELTA_CHECKS_PASS " << checks << "\n";
	return 0;
}
