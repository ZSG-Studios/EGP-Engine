// SPDX-License-Identifier: MIT
#pragma once

#include "box3d/box3d.h"

#if defined(BOX3D_DOUBLE_PRECISION) || defined(BOX3D_DISABLE_SIMD)
#error "EGP Box3D profile v1 requires float32 and SIMD width 4"
#endif

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace egp::box3d {

// This core has no Godot or networking dependency. Only the simulation owner thread calls it.
enum class Result { OK,
	NOT_CONFIGURED,
	INVALID_ARGUMENT,
	WRONG_TICK,
	DUPLICATE_COMMAND,
	INVALID_BATCH,
	PENDING_COMMANDS,
	INVALID_SNAPSHOT,
	LIMIT_REACHED };
enum class Operation { CREATE_BOX,
	CREATE_SPHERE,
	CREATE_CAPSULE,
	DESTROY,
	IMPULSE,
	VELOCITY,
	BODY_STATE,
	CREATE_JOINT,
	DESTROY_JOINT };

// Deterministic joint kinds. Frames are body-local; bodies are created unrotated.
enum class JointKind : uint32_t { DISTANCE,
	SPHERICAL,
	PRISMATIC };

struct Command {
	uint64_t entity = 0;
	uint32_t sequence = 0;
	Operation operation = Operation::IMPULSE;
	b3Vec3 value = {};
	b3Vec3 size = { 0.5f, 0.5f, 0.5f };
	b3BodyType body_type = b3_dynamicBody;
	float density = 1.0f;
	b3Quat rotation = { {}, 1.0f };
	b3Vec3 linear_velocity = {};
	b3Vec3 angular_velocity = {};
	// CREATE_JOINT only: entity is the joint identifier (a separate namespace from bodies).
	JointKind joint_kind = JointKind::DISTANCE;
	uint64_t body_a = 0;
	uint64_t body_b = 0;
	b3Vec3 anchor_a = {};
	b3Vec3 anchor_b = {};
	b3Vec3 axis = { 1.0f, 0.0f, 0.0f };
	float length = 1.0f;
	float hertz = 0.0f;
	float damping_ratio = 0.0f;
	float lower = 0.0f;
	float upper = 0.0f;
	bool enable_spring = false;
	bool enable_limit = false;
	bool collide_connected = false;
};

class DeterministicWorld {
	b3WorldId world = {};
	b3RecPlayer *snapshot_owner = nullptr;
	std::map<uint64_t, b3BodyId> bodies;
	struct JointRecord {
		b3JointId id = b3_nullJointId;
		uint64_t body_a = 0;
		uint64_t body_b = 0;
		uint32_t kind = 0;
	};
	std::map<uint64_t, JointRecord> joints;
	void create_joint(const Command &p_command);
	std::vector<Command> pending;
	std::set<std::pair<uint64_t, uint32_t>> pending_keys;
	uint64_t tick = 0;
	uint32_t tick_rate = 60;
	uint32_t substeps = 4;
	uint32_t workers = 1;
	b3Vec3 gravity = { 0.0f, -9.8f, 0.0f };
	void release_world();

public:
	static constexpr uint32_t MAX_COMMANDS = 65536;
	static constexpr uint32_t MAX_BODIES = 65536;
	static constexpr uint32_t MAX_SNAPSHOT_BYTES = 64 * 1024 * 1024;
	DeterministicWorld() = default;
	~DeterministicWorld();
	DeterministicWorld(const DeterministicWorld &) = delete;
	DeterministicWorld &operator=(const DeterministicWorld &) = delete;
	Result configure(uint32_t p_tick_rate = 60, uint32_t p_substeps = 4, uint32_t p_workers = 1, b3Vec3 p_gravity = { 0.0f, -9.8f, 0.0f });
	Result queue(const Command &p_command);
	Result apply_queued_commands();
	void clear_pending_commands() {
		pending.clear();
		pending_keys.clear();
	}
	Result step_tick(uint64_t p_expected_tick);
	Result capture_snapshot(std::vector<uint8_t> &r_bytes);
	Result restore_snapshot(const std::vector<uint8_t> &p_bytes);
	uint64_t get_tick() const { return tick; }
	size_t get_body_count() const { return bodies.size(); }
	size_t get_joint_count() const { return joints.size(); }
	bool get_body_state(uint64_t p_entity, b3WorldTransform &r_transform, b3Vec3 &r_linear, b3Vec3 &r_angular) const;
	// Diagnostic hash of tick, profile, stable IDs, poses, velocities, body type and awake state.
	// It is not a cryptographic digest or a hash of every latent solver field.
	uint64_t get_state_hash() const;
	std::string get_simulation_fingerprint() const;
};

} // namespace egp::box3d
