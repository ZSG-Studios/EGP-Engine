/**************************************************************************/
/*  state_encoding_check.cpp                                              */
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
#include "netcode.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace egp::net;
void check(bool value, const char *name) {
	if (!value) {
		std::cerr << "FAILED: " << name << std::endl;
		std::exit(1);
	}
}
int main() {
	Options options;
	options.allow_insecure_loopback = true;
	options.max_entities = 8;
	options.messages_per_second = 32;
	options.bytes_per_second = 8192;
	Session server(options), client(options);
	auto until = [&](auto predicate) {
		const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do {
			check(server.pump() == Result::Ok && client.pump() == Result::Ok, "pump");
			if (predicate()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		} while (std::chrono::steady_clock::now() < end);
		return false;
	};
	check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
	check(client.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "connect");
	check(until([&] { return client.state() == "Connected"; }), "initial baseline");
	uint64_t handle = 0;
	check(server.spawn(1, {}, -1, handle) == Result::Ok, "empty state spawn");
	check(until([&] { return client.entities().count(handle) != 0; }), "empty state replication");
	int phase = 0;
	for (int size : { 1, 128, 129, 4096, 128, 0 }) {
		std::vector<uint8_t> payload(size, uint8_t(++phase));
		check(server.update(handle, payload) == Result::Ok, "state transition accepted");
		const uint64_t revision = server.entities().at(handle).revision;
		check(until([&] {
			const auto entities = client.entities();
			const auto it = entities.find(handle);
			return it != entities.end() && it->second.revision == revision && it->second.state == payload;
		}),
				"inline/block transition exact bytes and revision");
	}

	std::vector<uint8_t> token;
	check(server.issue_token(700, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "compatibility admission token");
	for (int variant : { 0, 1 }) {
		Options wrong = options;
		if (variant == 0) {
			wrong.game_protocol = "other-game";
		} else {
			wrong.simulation_fingerprint = "other-simulation";
		}
		Session mismatch(wrong);
		int diagnostics = 0;
		mismatch.diagnostic = [&](const std::string &text) { diagnostics += text.find("fingerprint") != std::string::npos; };
		check(mismatch.connect_token(700, token) == Result::Invalid, "mismatched trusted token rejected before admission");
		check(mismatch.state() == "Stopped" && diagnostics == 1 && server.peers().size() == 1, "mismatch leaves existing session healthy and reports diagnostic");
	}
	std::string old_wire = "egp-native-wire-1|yojimbo-272153a|state-4096|" + options.game_protocol + "|" + options.simulation_fingerprint + "|" + std::to_string(options.tick_rate);
	uint64_t old_protocol = 14695981039346656037ULL;
	for (unsigned char byte : old_wire) {
		old_protocol ^= byte;
		old_protocol *= 1099511628211ULL;
	}
	std::vector<uint8_t> old_token(2048);
	std::array<uint8_t, NETCODE_USER_DATA_BYTES> user_data{};
	std::string address = "127.0.0.1:" + std::to_string(server.statistics().local_port);
	const char *addresses[] = { address.c_str() };
	check(netcode_generate_connect_token(1, addresses, addresses, 30, 5, 701, old_protocol, options.private_key.data(), user_data.data(), old_token.data()) == NETCODE_OK, "version 1 token fixture");
	Session old_identity(options);
	check(old_identity.connect_token(701, old_token) == Result::Invalid && old_identity.state() == "Stopped", "version 1 wire token rejected before transport starts");

	check(client.stop() == Result::Ok && server.stop() == Result::Ok, "cleanup");
	std::cout << "EGP_STATE_ENCODING_CHECKS=passed sizes=0,1,128,129,4096,128,0 pump_ms=16 messages_per_second=32 incompatible_tokens=3" << std::endl;
}
