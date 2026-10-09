/**************************************************************************/
/*  replication_policy_check.cpp                                          */
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
void check(bool ok, const char *text) {
	if (!ok) {
		std::cerr << "FAILED: " << text << std::endl;
		std::exit(1);
	}
}
int main() {
	Options options;
	options.allow_insecure_loopback = true;
	options.max_players = 2;
	options.max_entities = 8;
	Session server(options), first(options), second(options);
	std::array<Session *, 2> clients{ &first, &second };
	auto until = [&](auto done, int milliseconds = 5000) {
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
		do {
			check(server.pump() == Result::Ok && first.pump() == Result::Ok && second.pump() == Result::Ok, "native pumps");
			if (done()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		} while (std::chrono::steady_clock::now() < end);
		return false;
	};
	check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
	check(first.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "first native client admission");
	check(until([&] { return first.state() == "Connected" && server.peers().size() == 1; }), "first peer identity established");
	const int64_t constrained = server.peers()[0].id;
	check(second.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "second native client admission");
	check(until([&] { return first.state() == "Connected" && second.state() == "Connected"; }), "native baselines");
	std::array<uint64_t, 4> handles{};
	for (auto &handle : handles) {
		check(server.spawn(1, std::vector<uint8_t>(16), -1, handle) == Result::Ok, "spawn gameplay state");
	}
	check(until([&] { return first.entities().size() == 4 && second.entities().size() == 4; }), "initial membership");
	auto peers = server.peers();
	check(peers.size() == 2, "independent peers");
	// Capture identity before the second admission; priority delivery rates may tie across peers.
	const int64_t generous_peer = peers[0].id == constrained ? peers[1].id : peers[0].id;
	check(server.set_peer_replication_budget(constrained, 1024) == Result::Ok, "per-peer byte update budget");
	check(server.set_peer_replication_budget(generous_peer, 8192) == Result::Ok, "independent larger update budget");
	check(server.set_replication_priority(handles[0], 8) == Result::Ok, "higher priority gameplay state");
	check(server.set_replication_priority(handles[0], 0) == Result::Invalid && server.set_replication_priority(handles[0], 17) == Result::Invalid, "priority range validated");
	check(first.set_replication_priority(handles[0], 2) == Result::Unauthorized && first.set_peer_replication_budget(0, 1) == Result::Unauthorized, "clients cannot configure server admission");
	check(server.set_peer_replication_budget(constrained, -1) == Result::Invalid && server.set_peer_replication_budget(constrained, options.bytes_per_second + 1) == Result::Invalid, "byte policy range validated");
	check(server.set_replication_priority(UINT64_MAX, 2) == Result::NotFound && server.set_peer_replication_budget(INT64_MAX, 1) == Result::NotFound, "stale identity policies rejected");
	Result wrong_thread = Result::Ok;
	std::thread worker([&] { wrong_thread = server.set_replication_priority(handles[0], 2); });
	worker.join();
	check(wrong_thread == Result::Busy, "policy creator thread enforced");
	std::array<std::array<uint64_t, 4>, 2> revisions{}, deliveries{};
	server.simulation_tick = [&](uint64_t tick, bool) {
		for (int i = 0; i < 4; i++) {
			std::vector<uint8_t> state(16, uint8_t(i));
			state[0] = uint8_t(tick);
			state[1] = uint8_t(tick >> 8);
			check(server.update(handles[i], state) == Result::Ok, "continuous gameplay updates");
		}
	};
	const auto started = std::chrono::steady_clock::now();
	until([&] {
		for (int c = 0; c < 2; c++) {
			for (int i = 0; i < 4; i++) {
				auto entity = clients[c]->entity(handles[i]);
				if (entity && entity->revision != revisions[c][i]) {
					revisions[c][i] = entity->revision;
					deliveries[c][i]++;
				}
			}
		}
		return std::chrono::steady_clock::now() - started > std::chrono::seconds(4);
	},
			4500);
	auto limited = server.replication_statistics(constrained), generous = server.replication_statistics(generous_peer);
	check(limited && generous && limited->budget_deferrals > 0, "budget deferrals observable");
	check(limited->sent_bytes <= 4160 + 1024 * 5 && generous->sent_bytes > limited->sent_bytes * 2, "independent peer envelope byte budgets");
	// Peer callback order is normally first→second but resolve the constrained receive counters from traffic.
	const int low = deliveries[0][0] < deliveries[1][0] ? 0 : 1;
	for (int i = 1; i < 4; i++) {
		check(deliveries[low][i] >= 2, "low priority flows make progress without starvation");
	}
	check(deliveries[low][0] > deliveries[low][1] * 2, "priority receives greater service under pressure");
	server.simulation_tick = nullptr;
	check(server.set_peer_replication_budget(constrained, 1) == Result::Ok, "very small finite budget accepted with one-packet burst");
	for (auto handle : handles) {
		check(server.set_replication_priority(handle, 1) == Result::Ok, "restore equal priority");
		check(server.update(handle, std::vector<uint8_t>(Session::MaxStateBytes, 7)) == Result::Ok, "maximum envelope pending");
	}
	check(until([&] { return server.replication_statistics(constrained)->budget_deferrals > limited->budget_deferrals + 10; }, 1000), "large packet reserves budget rather than starving behind smaller packets");
	uint64_t fresh = 0;
	check(server.spawn(9, { 42 }, -1, fresh) == Result::Ok, "new membership while update budget exhausted");
	check(until([&] { return first.entity(fresh).has_value() && second.entity(fresh).has_value(); }), "new membership baseline bypasses gameplay subbudget");
	check(server.despawn(fresh) == Result::Ok, "destroy membership under exhausted update budget");
	check(until([&] { return !first.entity(fresh) && !second.entity(fresh); }), "teardown not starved by update budget");
	check(server.set_visible(handles[3], constrained, false) == Result::Ok, "hide under budget pressure");
	check(until([&] { return !clients[low]->entity(handles[3]); }), "hidden membership removed promptly");
	check(server.set_visible(handles[3], constrained, true) == Result::Ok, "interest re-entry");
	check(until([&] { return clients[low]->entity(handles[3]).has_value(); }), "re-entry gets current baseline outside update subbudget");
	check(server.set_peer_replication_budget(constrained, 0) == Result::Ok, "zero restores existing global quota policy");
	check(until([&] { for (auto handle : handles) { auto entity = clients[low]->entity(handle); if (!entity || entity->state.size() != Session::MaxStateBytes){ return false;
} } return true; }), "deferred latest state converges after removing subbudget");
	auto final_stats = server.replication_statistics(constrained);
	for (auto *client : clients) {
		client->stop();
	}
	server.stop();
	check(!server.replication_statistics(constrained), "peer policies expire with connection generation");
	std::cout << "EGP_REPLICATION_POLICY=passed high=" << deliveries[low][0] << " low=" << deliveries[low][1] << ',' << deliveries[low][2] << ',' << deliveries[low][3] << " limited_bytes=" << limited->sent_bytes << " generous_bytes=" << generous->sent_bytes << " deferrals=" << final_stats->budget_deferrals << std::endl;
}
