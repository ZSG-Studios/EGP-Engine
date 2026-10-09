// SPDX-License-Identifier: MIT
#include "deterministic_world.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <thread>

using namespace egp::box3d;
#define CHECK(c) \
	do { \
		if (!(c)) { \
			std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); \
			std::exit(1); \
		} \
	} while (false)

static std::vector<Command> commands(uint64_t tick) {
	std::vector<Command> result;
	if (tick == 1) {
		Command floor;
		floor.entity = 1;
		floor.operation = Operation::CREATE_BOX;
		floor.body_type = b3_staticBody;
		floor.value = { 0, -0.5f, 0 };
		floor.size = { 20, 0.5f, 20 };
		result.push_back(floor);
		for (uint64_t i = 2; i < 82; ++i) {
			Command c;
			c.entity = i;
			c.operation = i % 3 == 0 ? Operation::CREATE_SPHERE : Operation::CREATE_BOX;
			c.value = { float((i - 2) % 8) * 1.15f - 4, float((i - 2) / 8) * 1.1f + 0.55f, 0 };
			result.push_back(c);
		}
	}
	if (tick == 93 || tick == 155 || tick == 365 || tick == 480) {
		for (uint64_t i = 2; i < 12; ++i) {
			Command c;
			c.entity = i;
			c.sequence = 1;
			c.operation = Operation::IMPULSE;
			c.value = { 0.1f * float(i), 3, 0.2f };
			result.push_back(c);
		}
	}
	if (tick == 180 || tick == 400) {
		Command c;
		c.entity = 7;
		c.operation = Operation::DESTROY;
		c.sequence = 0;
		result.push_back(c);
		c.operation = Operation::CREATE_CAPSULE;
		c.sequence = 1;
		c.value = { 0, 9, 1 };
		c.size = { 0.3f, 0.6f, 0.3f };
		result.push_back(c);
	}
	return result;
}
static void advance(DeterministicWorld &world, uint64_t tick, bool reverse) {
	auto batch = commands(tick);
	if (reverse) {
		std::reverse(batch.begin(), batch.end());
	}
	for (const auto &c : batch) {
		CHECK(world.queue(c) == Result::OK);
	}
	CHECK(world.step_tick(tick) == Result::OK);
}
int main(int argc, char **argv) {
	DeterministicWorld serial, parallel;
	CHECK(serial.configure() == Result::OK);
	CHECK(parallel.configure(60, 4, 4) == Result::OK);
	CHECK(serial.get_simulation_fingerprint() == parallel.get_simulation_fingerprint());
	std::vector<uint64_t> hashes(601);
	std::vector<uint8_t> snapshot;
	for (uint64_t tick = 1; tick <= 600; ++tick) {
		advance(serial, tick, false);
		advance(parallel, tick, true);
		hashes[tick] = serial.get_state_hash();
		CHECK(hashes[tick] == parallel.get_state_hash());
		if (tick == 150) {
			CHECK(serial.capture_snapshot(snapshot) == Result::OK);
		}
	}
	CHECK(serial.get_body_count() == 81);
	// Commit the entire reference trajectory, not only a resting final pose.
	if (argc == 3 && std::string(argv[1]) == "--write-golden") {
		std::ofstream file(argv[2]);
		CHECK(file.good());
		for (uint64_t i = 1; i <= 600; ++i) {
			file << std::hex << hashes[i] << '\n';
		}
	} else {
		std::ifstream file(EGP_BOX3D_GOLDEN_PATH);
		CHECK(file.good());
		for (uint64_t i = 1; i <= 600; ++i) {
			uint64_t expected = 0;
			file >> std::hex >> expected;
			CHECK(file.good());
			CHECK(expected == hashes[i]);
		}
	}
	CHECK(serial.restore_snapshot(snapshot) == Result::OK);
	CHECK(serial.get_tick() == 150);
	CHECK(serial.get_state_hash() == hashes[150]);
	for (uint64_t tick = 151; tick <= 600; ++tick) {
		advance(serial, tick, true);
		CHECK(serial.get_state_hash() == hashes[tick]);
	}
	// Capture again from a restored world, then restore repeatedly without leaking world slots.
	std::vector<uint8_t> second;
	CHECK(serial.capture_snapshot(second) == Result::OK);
	for (int i = 0; i < 140; ++i) {
		CHECK(serial.restore_snapshot(second) == Result::OK);
		CHECK(serial.get_state_hash() == hashes[600]);
	}
	CHECK(serial.step_tick(602) == Result::WRONG_TICK);
	CHECK(serial.get_state_hash() == hashes[600]);
	Command bad;
	bad.entity = 999;
	bad.operation = Operation::DESTROY;
	CHECK(serial.queue(bad) == Result::OK);
	CHECK(serial.step_tick(601) == Result::INVALID_BATCH);
	CHECK(serial.get_state_hash() == hashes[600]);
	CHECK(serial.capture_snapshot(second) == Result::PENDING_COMMANDS);
	serial.clear_pending_commands();
	bad.value.x = std::numeric_limits<float>::quiet_NaN();
	CHECK(serial.queue(bad) == Result::INVALID_ARGUMENT);
	bad.value.x = 0;
	bad.entity = 2;
	bad.operation = Operation::IMPULSE;
	CHECK(serial.queue(bad) == Result::OK);
	CHECK(serial.queue(bad) == Result::DUPLICATE_COMMAND);
	serial.clear_pending_commands();
	auto corrupt = snapshot;
	corrupt[90] ^= 1;
	CHECK(serial.restore_snapshot(corrupt) == Result::INVALID_SNAPSHOT);
	CHECK(serial.get_state_hash() == hashes[600]);
	DeterministicWorld wrong_profile;
	CHECK(wrong_profile.configure(120) == Result::OK);
	CHECK(wrong_profile.restore_snapshot(snapshot) == Result::INVALID_SNAPSHOT);
	DeterministicWorld empty;
	CHECK(empty.configure() == Result::OK);
	CHECK(empty.capture_snapshot(second) == Result::OK);
	CHECK(empty.restore_snapshot(second) == Result::OK);
	CHECK(empty.get_body_count() == 0);
	// Corrections and baseline creation apply at a boundary without advancing physics.
	Command correction;
	correction.entity = 7;
	correction.operation = Operation::BODY_STATE;
	correction.value = { 3, 5, 2 };
	correction.linear_velocity = { 1, 2, 3 };
	CHECK(serial.queue(correction) == Result::OK);
	CHECK(serial.apply_queued_commands() == Result::OK);
	CHECK(serial.get_tick() == 600);
	CHECK(serial.capture_snapshot(second) == Result::OK);
	uint64_t corrected = serial.get_state_hash();
	CHECK(serial.restore_snapshot(second) == Result::OK);
	CHECK(serial.get_state_hash() == corrected);
	correction.rotation.s = 2;
	CHECK(serial.queue(correction) == Result::INVALID_ARGUMENT);
	CHECK(std::fesetround(FE_DOWNWARD) == 0);
	CHECK(serial.step_tick(601) == Result::INVALID_ARGUMENT);
	CHECK(serial.get_state_hash() == corrected);
	CHECK(std::fesetround(FE_TONEAREST) == 0);
	// Independent owner threads and snapshot/destructor lifetimes share Box3D globals.
	std::vector<std::thread> owners;
	for (int owner = 0; owner < 4; ++owner) {
		owners.emplace_back([]() {
			for (int lifetime = 0; lifetime < 12; ++lifetime) {
				DeterministicWorld owned;
				CHECK(owned.configure() == Result::OK);
				for (uint64_t tick = 1; tick <= 60; ++tick) {
					advance(owned, tick, true);
				}
				std::vector<uint8_t> local_snapshot;
				CHECK(owned.capture_snapshot(local_snapshot) == Result::OK);
				for (uint64_t tick = 61; tick <= 120; ++tick) {
					advance(owned, tick, false);
				}
				uint64_t future = owned.get_state_hash();
				CHECK(owned.restore_snapshot(local_snapshot) == Result::OK);
				for (uint64_t tick = 61; tick <= 120; ++tick) {
					advance(owned, tick, true);
				}
				CHECK(owned.get_state_hash() == future);
			}
		});
	}
	for (auto &owner : owners) {
		owner.join();
	}
	b3WorldTransform t;
	b3Vec3 linear, angular;
	CHECK(serial.get_body_state(7, t, linear, angular));
	CHECK(std::isfinite(t.p.y));
	std::printf("PASS: 600 tick ordered replay, 1/4 workers, full-state rollback, lifecycle reuse, batch rejection. final=%016llx\n", static_cast<unsigned long long>(hashes[600]));
	return 0;
}
