// SPDX-License-Identifier: MIT
#pragma once

#include "egp_box2d_api.h"

#if defined(BOX2D_DOUBLE_PRECISION) || defined(BOX2D_DISABLE_SIMD)
#error "EGP Box2D deterministic worlds require float32 and SIMD width 4"
#endif

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace egp::box2d {

// Fixed-tick Box2D world with stable entity identifiers, a sorted command batch per
// tick and in-place snapshots. No Godot or networking dependency. Only the
// simulation owner thread calls it.
enum class Result { OK,
	NOT_CONFIGURED,
	INVALID_ARGUMENT,
	WRONG_TICK,
	DUPLICATE_COMMAND,
	INVALID_BATCH,
	PENDING_COMMANDS,
	INVALID_SNAPSHOT,
	LIMIT_REACHED };

enum class Operation { CREATE_BODY,
	DESTROY_BODY,
	SET_BODY,
	ADD_SHAPE,
	SET_SHAPE,
	DESTROY_SHAPE,
	CREATE_JOINT,
	SET_JOINT,
	DESTROY_JOINT,
	APPLY,
	SET_WORLD,
	EXPLODE };

// APPLY: value is the force or impulse (scalar for torque and angular impulse); point
// is the world point for the *_AT_POINT kinds. WIND applies Box2D's aerodynamic wind
// (value) to shape_index with drag and lift coefficients.
enum class ApplyKind : uint8_t { FORCE,
	FORCE_AT_POINT,
	TORQUE,
	LINEAR_IMPULSE,
	IMPULSE_AT_POINT,
	ANGULAR_IMPULSE,
	WIND };

struct ShapeSpec {
	Geometry geometry;
	Props props;
};

struct Command {
	// Body entity, joint identifier (joint commands) or WORLD_KEY (world commands).
	uint64_t entity = 0;
	uint32_t sequence = 0;
	Operation operation = Operation::APPLY;
	Props props;
	std::vector<ShapeSpec> shapes;
	uint32_t shape_index = 0;
	JointType joint_type = JointType::DISTANCE;
	uint64_t body_a = 0;
	uint64_t body_b = 0;
	ApplyKind apply = ApplyKind::LINEAR_IMPULSE;
	b2Vec2 value = { 0.0f, 0.0f };
	b2Vec2 point = { 0.0f, 0.0f };
	float scalar = 0.0f;
	float drag = 0.0f;
	float lift = 0.0f;
	// EXPLODE: value is the centre.
	float radius = 1.0f;
	float falloff = 0.0f;
	float impulse_per_length = 0.0f;
	uint64_t mask = UINT64_MAX;
};

// A shape by stable identity: body entity and shape index (entity 0: no longer exists).
struct ShapeRef {
	uint64_t entity = 0;
	uint32_t shape = 0;
	bool operator<(const ShapeRef &p_other) const { return entity != p_other.entity ? entity < p_other.entity : shape < p_other.shape; }
	bool operator==(const ShapeRef &p_other) const { return entity == p_other.entity && shape == p_other.shape; }
};

struct StepEvents {
	struct Pair {
		ShapeRef a;
		ShapeRef b;
	};
	struct Hit {
		ShapeRef a;
		ShapeRef b;
		b2Vec2 point = { 0.0f, 0.0f };
		b2Vec2 normal = { 0.0f, 0.0f };
		float approach_speed = 0.0f;
	};
	struct Move {
		uint64_t entity = 0;
		b2Vec2 position = { 0.0f, 0.0f };
		float angle = 0.0f;
		bool fell_asleep = false;
	};
	std::vector<Pair> contact_begin;
	std::vector<Pair> contact_end;
	std::vector<Hit> contact_hit;
	std::vector<Pair> sensor_begin;
	std::vector<Pair> sensor_end;
	std::vector<Move> moved;
	std::vector<uint64_t> joints;
	void clear() {
		contact_begin.clear();
		contact_end.clear();
		contact_hit.clear();
		sensor_begin.clear();
		sensor_end.clear();
		moved.clear();
		joints.clear();
	}
};

class DeterministicWorld2D {
	b2WorldId world = b2_nullWorldId;
	std::map<uint64_t, b2BodyId> bodies;
	struct ShapeSlot {
		b2ShapeId shape = b2_nullShapeId;
		b2ChainId chain = b2_nullChainId;
	};
	std::map<uint64_t, std::map<uint32_t, ShapeSlot>> shapes;
	struct JointRecord {
		b2JointId id = b2_nullJointId;
		uint64_t body_a = 0;
		uint64_t body_b = 0;
		uint32_t kind = 0;
	};
	std::map<uint64_t, JointRecord> joints;
	std::vector<Command> pending;
	std::set<std::pair<uint64_t, uint32_t>> pending_keys;
	StepEvents events;
	uint64_t tick = 0;
	uint32_t tick_rate = 60;
	uint32_t substeps = 4;
	uint32_t workers = 1;
	b2Vec2 configured_gravity = { 0.0f, -10.0f };
	// Box2D length units are process global (EGP sets physics/box_2d/pixels_per_meter);
	// tolerances derive from them, so they are part of the deterministic profile.
	float length_units = 1.0f;
	void release_world();
	void tag_all();
	void add_shape(uint64_t p_entity, uint32_t p_index, b2BodyId p_body, const ShapeSpec &p_spec);
	void destroy_shape(ShapeSlot &p_slot);
	void apply_body_command(const Command &p_command, b2BodyId p_body);
	void capture_events();
	ShapeRef shape_ref(b2ShapeId p_shape) const;

public:
	static constexpr uint32_t MAX_COMMANDS = 65536;
	static constexpr uint32_t MAX_BODIES = 65536;
	static constexpr uint32_t MAX_SHAPES_PER_BODY = 256;
	static constexpr uint32_t MAX_SNAPSHOT_BYTES = 64 * 1024 * 1024;
	// Entity key of world commands (SET_WORLD, EXPLODE): after all bodies in a batch.
	static constexpr uint64_t WORLD_KEY = uint64_t(1) << 63;

	DeterministicWorld2D() = default;
	~DeterministicWorld2D();
	DeterministicWorld2D(const DeterministicWorld2D &) = delete;
	DeterministicWorld2D &operator=(const DeterministicWorld2D &) = delete;

	// Optional task callbacks run solver work on the host's thread pool.
	Result configure(uint32_t p_tick_rate, uint32_t p_substeps, uint32_t p_workers, b2Vec2 p_gravity, b2EnqueueTaskCallback *p_enqueue = nullptr, b2FinishTaskCallback *p_finish = nullptr);
	Result queue(const Command &p_command);
	// Validates the whole sorted batch, then applies: joint removals, body commands
	// (entity order), world commands, joint creation, joint changes.
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
	uint64_t get_state_hash() const;
	std::string get_simulation_fingerprint() const;

	bool get_body_state(uint64_t p_entity, b2Vec2 &r_position, float &r_angle, b2Vec2 &r_linear, float &r_angular) const;
	bool read_world(Values &r_values) const;
	bool read_body(uint64_t p_entity, Values &r_values) const;
	bool read_shape(uint64_t p_entity, uint32_t p_index, Values &r_values) const;
	bool read_joint(uint64_t p_joint, Values &r_values, JointType &r_type, uint64_t &r_body_a, uint64_t &r_body_b) const;
	std::vector<uint32_t> get_shape_indices(uint64_t p_entity) const;
	std::vector<uint64_t> get_entities() const;
	std::vector<uint64_t> get_joint_ids() const;
	const StepEvents &get_events() const { return events; }

	struct RayHit {
		bool hit = false;
		float fraction = 1.0f;
		b2Vec2 point = { 0.0f, 0.0f };
		b2Vec2 normal = { 0.0f, 0.0f };
		uint64_t entity = 0;
		uint32_t shape = 0;
	};
	void cast_rays(const b2Vec2 *p_origins, const b2Vec2 *p_translations, size_t p_count, RayHit *r_hits, uint64_t p_mask) const;
	void overlap_aabb(b2Vec2 p_lower, b2Vec2 p_upper, uint64_t p_mask, std::vector<ShapeRef> &r_shapes) const;
	bool overlap_shape(const Geometry &p_geometry, b2Vec2 p_position, float p_angle, uint64_t p_mask, std::vector<ShapeRef> &r_shapes) const;
	bool cast_shape(const Geometry &p_geometry, b2Vec2 p_position, float p_angle, b2Vec2 p_translation, uint64_t p_mask, uint64_t p_ignore, RayHit &r_hit) const;
	// Box2D character mover: collide, solve planes and sweep a capsule (point_a/point_b
	// relative to p_position) through shapes matching p_mask. Returns the final position;
	// r_clipped is the translation clipped by the contact planes.
	b2Vec2 move_capsule(b2Vec2 p_position, b2Vec2 p_point_a, b2Vec2 p_point_b, float p_radius, b2Vec2 p_translation, uint64_t p_mask, b2Vec2 *r_clipped) const;
};

} // namespace egp::box2d
