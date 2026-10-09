// SPDX-License-Identifier: MIT
// Test-only C ABI; game code uses the Godot EGPBox3DWorld facade.
#include "deterministic_world.h"
#include <cstring>

#ifdef _WIN32
#define EGP_EXPORT extern "C" __declspec(dllexport)
#else
#define EGP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using egp::box3d::DeterministicWorld;
using egp::box3d::Result;
EGP_EXPORT DeterministicWorld *egp_test_create() {
	auto *world = new DeterministicWorld;
	if (world->configure() != Result::OK) { delete world; return nullptr; }
	return world;
}
EGP_EXPORT void egp_test_destroy(DeterministicWorld *world) { delete world; }
EGP_EXPORT uint64_t egp_test_tick(DeterministicWorld *world) { return world->get_tick(); }
EGP_EXPORT int egp_test_step(DeterministicWorld *world, uint64_t tick) { return int(world->step_tick(tick)); }
EGP_EXPORT int egp_test_spawn(DeterministicWorld *world, uint64_t entity, float y) {
	egp::box3d::Command command;
	command.entity = entity;
	command.operation = egp::box3d::Operation::CREATE_BOX;
	command.value = { 0.0f, y, 0.0f };
	return int(world->queue(command));
}
EGP_EXPORT int egp_test_flush(DeterministicWorld *world) { return int(world->apply_queued_commands()); }
EGP_EXPORT float egp_test_height(DeterministicWorld *world, uint64_t entity) {
	b3WorldTransform pose;
	b3Vec3 linear, angular;
	return world->get_body_state(entity, pose, linear, angular) ? float(pose.p.y) : -100000.0f;
}
EGP_EXPORT int egp_test_snapshot(DeterministicWorld *world, uint8_t *buffer, int capacity) {
	std::vector<uint8_t> snapshot;
	if (world->capture_snapshot(snapshot) != Result::OK) return -1;
	if (buffer && capacity >= int(snapshot.size())) std::memcpy(buffer, snapshot.data(), snapshot.size());
	return int(snapshot.size());
}
EGP_EXPORT int egp_test_restore(DeterministicWorld *world, const uint8_t *buffer, int size) {
	return int(world->restore_snapshot(std::vector<uint8_t>(buffer, buffer + size)));
}
