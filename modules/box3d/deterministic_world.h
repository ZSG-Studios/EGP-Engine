// SPDX-License-Identifier: MIT
#pragma once

#include "box3d/box3d.h"
#include "egp_box3d_api.h"

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
	DESTROY_JOINT,
	// Table-driven commands (egp_box3d_api.h): every Box3D body, shape, joint and world
	// field by name. Bodies hold any number of shapes addressed by a stable index.
	CREATE_BODY,
	SET_BODY,
	ADD_SHAPE,
	SET_SHAPE,
	DESTROY_SHAPE,
	CREATE_TYPED_JOINT,
	SET_JOINT,
	APPLY,
	SET_WORLD,
	EXPLODE,
	// Character drive: sets the velocity components selected by axes (bit 0 x, 1 y,
	// 2 z) from value, keeping the solver's position and the other components.
	DRIVE };

// APPLY: value is the force, torque or impulse; point is the world point for the
// *_AT_POINT kinds. Forces act over the next step.
enum class ApplyKind : uint8_t { FORCE,
	FORCE_AT_POINT,
	TORQUE,
	LINEAR_IMPULSE,
	IMPULSE_AT_POINT,
	ANGULAR_IMPULSE };

struct ShapeSpec {
	Geometry geometry;
	Props props;
};

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
	// Table-driven commands. props holds body fields (CREATE_BODY, SET_BODY), shape fields
	// (SET_SHAPE), joint fields (CREATE_TYPED_JOINT, SET_JOINT) or world fields (SET_WORLD).
	// CREATE_BODY creates shapes 0..n-1; ADD_SHAPE adds shapes[0] at shape_index.
	// World commands (SET_WORLD, EXPLODE) use entity DeterministicWorld::WORLD_KEY.
	Props props;
	std::vector<ShapeSpec> shapes;
	uint32_t shape_index = 0;
	JointType joint_type = JointType::DISTANCE;
	ApplyKind apply = ApplyKind::LINEAR_IMPULSE;
	b3Vec3 point = {};
	// EXPLODE: value is the centre; radius, falloff and impulse per area; mask bits.
	float radius = 1.0f;
	float falloff = 0.0f;
	float impulse_per_area = 0.0f;
	uint64_t mask = UINT64_MAX;
	// SET_SHAPE: -1 changes the shape; 0.. one entry of a mesh or height field's
	// material table (0 is the shape's own material).
	int32_t material_index = -1;
	// DRIVE: velocity axes to set (bit 0 x, bit 1 y, bit 2 z).
	uint8_t axes = 5;
};

// A shape by stable identity: body entity and shape index (entity 0: no longer exists).
struct ShapeRef {
	uint64_t entity = 0;
	uint32_t shape = 0;
	bool operator<(const ShapeRef &p_other) const { return entity != p_other.entity ? entity < p_other.entity : shape < p_other.shape; }
	bool operator==(const ShapeRef &p_other) const { return entity == p_other.entity && shape == p_other.shape; }
};

// Box3D events from the latest step, mapped to stable identities.
struct StepEvents {
	struct Pair {
		ShapeRef a;
		ShapeRef b;
	};
	struct Hit {
		ShapeRef a;
		ShapeRef b;
		b3Vec3 point = {};
		b3Vec3 normal = {};
		float approach_speed = 0.0f;
	};
	struct Move {
		uint64_t entity = 0;
		b3WorldTransform transform = {};
		bool fell_asleep = false;
	};
	std::vector<Pair> contact_begin;
	std::vector<Pair> contact_end;
	std::vector<Hit> contact_hit;
	// Sensor pairs: a is the sensor shape, b the visitor.
	std::vector<Pair> sensor_begin;
	std::vector<Pair> sensor_end;
	std::vector<Move> moved;
	// Joints whose force or torque threshold was exceeded.
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
	// Shapes per body by stable index; Box3D shape names are "egp:<entity>:<index>".
	std::map<uint64_t, std::map<uint32_t, b3ShapeId>> shapes;
	GeometryStore geometry;
	StepEvents events;
	b3Vec3 configured_gravity = { 0.0f, -9.8f, 0.0f };
	void create_joint(const Command &p_command);
	b3ShapeId add_shape(uint64_t p_entity, uint32_t p_index, const b3BodyId &p_body, b3ShapeDef p_def, const Geometry &p_geometry);
	void create_body(const Command &p_command);
	void apply_body_command(const Command &p_command, b3BodyId p_body);
	void capture_events();
	ShapeRef shape_ref(b3ShapeId p_shape) const;
	bool rebuild_shapes(const std::map<uint64_t, b3BodyId> &p_bodies, std::map<uint64_t, std::map<uint32_t, b3ShapeId>> &r_shapes) const;
	std::vector<Command> pending;
	std::set<std::pair<uint64_t, uint32_t>> pending_keys;
	uint64_t tick = 0;
	uint32_t tick_rate = 60;
	uint32_t substeps = 4;
	uint32_t workers = 1;
	b3EnqueueTaskCallback *enqueue_task = nullptr;
	b3FinishTaskCallback *finish_task = nullptr;
	b3Vec3 gravity = { 0.0f, -9.8f, 0.0f };
	// Fluid volume: buoyancy and drag for opted-in bodies, applied inside step_tick.
	struct Fluid {
		bool enabled = false;
		b3Vec3 lower = {};
		b3Vec3 upper = {};
		float density = 1.0f;
		float linear_drag = 0.0f;
		float angular_drag = 0.0f;
	};
	Fluid fluid;
	std::set<uint64_t> buoyant;
	void apply_fluid();
	void tag_bodies();
	void release_world();

public:
	static constexpr uint32_t MAX_COMMANDS = 65536;
	static constexpr uint32_t MAX_BODIES = 65536;
	static constexpr uint32_t MAX_SNAPSHOT_BYTES = 64 * 1024 * 1024;
	static constexpr uint32_t MAX_SHAPES_PER_BODY = 256;
	// Entity key of world commands (SET_WORLD, EXPLODE): after all bodies in a batch.
	static constexpr uint64_t WORLD_KEY = uint64_t(1) << 63;
	DeterministicWorld() = default;
	~DeterministicWorld();
	DeterministicWorld(const DeterministicWorld &) = delete;
	DeterministicWorld &operator=(const DeterministicWorld &) = delete;
	// Optional task callbacks run solver work on the host's thread pool; without them a
	// worker count above 1 uses Box3D's internal scheduler.
	Result configure(uint32_t p_tick_rate = 60, uint32_t p_substeps = 4, uint32_t p_workers = 1, b3Vec3 p_gravity = { 0.0f, -9.8f, 0.0f }, b3EnqueueTaskCallback *p_enqueue = nullptr, b3FinishTaskCallback *p_finish = nullptr);
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

	// Readback of every readable Box3D field (egp_box3d_api.h names). False when unknown.
	bool read_world(Values &r_values) const;
	bool read_body(uint64_t p_entity, Values &r_values) const;
	bool read_shape(uint64_t p_entity, uint32_t p_index, Values &r_values) const;
	// Every entry of a shape's material table (index 0 is the shape's own material).
	bool read_shape_materials(uint64_t p_entity, uint32_t p_index, std::vector<Values> &r_materials) const;
	bool read_joint(uint64_t p_joint, Values &r_values, JointType &r_type, uint64_t &r_body_a, uint64_t &r_body_b) const;
	std::vector<uint32_t> get_shape_indices(uint64_t p_entity) const;
	std::vector<uint64_t> get_entities() const;
	std::vector<uint64_t> get_joint_ids() const;
	// Events produced by the latest step_tick.
	const StepEvents &get_events() const { return events; }

	// Read-only Box3D queries against the current deterministic state. Identical
	// state gives identical answers on every peer.
	struct RayHit {
		bool hit = false;
		float fraction = 1.0f;
		b3Vec3 point = {};
		b3Vec3 normal = {};
		uint64_t entity = 0;
		uint32_t shape = 0;
	};
	// Closest hit per ray; shapes overlapping a ray's origin are ignored (a caster
	// inside its own capsule never hits itself).
	void cast_rays(const b3Vec3 *p_origins, const b3Vec3 *p_translations, size_t p_count, RayHit *r_hits, uint64_t p_mask = UINT64_MAX) const;
	// Shapes overlapping a box or a placed sphere/capsule/box/hull, sorted by identity
	// (independent of broad-phase traversal order).
	void overlap_aabb(b3Vec3 p_lower, b3Vec3 p_upper, uint64_t p_mask, std::vector<ShapeRef> &r_shapes) const;
	bool overlap_shape(const Geometry &p_geometry, b3Vec3 p_position, b3Quat p_rotation, uint64_t p_mask, std::vector<ShapeRef> &r_shapes) const;
	// Closest hit of a placed shape swept along a translation, ignoring one body. Ties
	// break by identity.
	bool cast_shape(const Geometry &p_geometry, b3Vec3 p_position, b3Quat p_rotation, b3Vec3 p_translation, uint64_t p_mask, uint64_t p_ignore, RayHit &r_hit) const;
	// Ground support for many bodies: one downward ray per body from its position,
	// depth long, ignoring the body itself. Writes hit flags, surface normals and the
	// supporting entity (0 when none).
	void probe_ground(const uint64_t *p_entities, const float *p_depths, size_t p_count, uint8_t *r_hits, b3Vec3 *r_normals, uint64_t *r_supports) const;
	// Box3D character mover: collide, solve planes, cast and slide a vertical capsule
	// (centred on p_position) through the world, ignoring one body. Returns the final
	// position; r_clipped is the requested translation clipped by the contact planes.
	b3Vec3 move_capsule(b3Vec3 p_position, float p_half_height, float p_radius, b3Vec3 p_translation, uint64_t p_ignore, b3Vec3 *r_clipped = nullptr) const;
	// Fluid volume (axis-aligned) and the bodies it acts on. Configuration, not
	// commands: every peer sets it up identically after create or restore.
	Result configure_fluid(b3Vec3 p_lower, b3Vec3 p_upper, float p_density, float p_linear_drag, float p_angular_drag);
	Result set_buoyant(uint64_t p_entity, bool p_enabled);
};

} // namespace egp::box3d
