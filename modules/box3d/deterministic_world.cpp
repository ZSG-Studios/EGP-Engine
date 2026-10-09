// SPDX-License-Identifier: MIT
#include "deterministic_world.h"

#include "simulation_guard.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>

namespace egp::box3d {
namespace {
constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;
constexpr uint64_t SNAPSHOT_MAGIC = 0x3150414E53334745ull; // EG3SNAP1, little endian.
// EG3SNAP2 appends a joint table (count, then id/body A/body B/kind per joint) so joint
// identifiers survive restore and joints take part in the state hash.
constexpr uint64_t SNAPSHOT_MAGIC_JOINTS = 0x3250414E53334745ull;
constexpr uint64_t PROFILE_ID = 0xe77352cd606dc1a3ull;
// Box3D world-slot allocation and replay length-scale updates are process globals.
// Serialize entry from independent owners and managed finalizers. Solver workers
// still run in parallel inside a step. Recursive locking permits boundary helpers.

bool finite(b3Vec3 v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
void append_u64(std::vector<uint8_t> &out, uint64_t v) {
	for (unsigned i = 0; i < 8; ++i) {
		out.push_back(static_cast<uint8_t>(v >> (i * 8)));
	}
}
uint64_t read_u64(const std::vector<uint8_t> &in, size_t offset) {
	uint64_t v = 0;
	for (unsigned i = 0; i < 8; ++i) {
		v |= uint64_t(in[offset + i]) << (i * 8);
	}
	return v;
}
void hash_u64(uint64_t &hash, uint64_t v) {
	for (unsigned i = 0; i < 8; ++i) {
		hash = (hash ^ uint8_t(v >> (i * 8))) * FNV_PRIME;
	}
}
uint32_t float_bits(float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	return bits;
}
void hash_float(uint64_t &h, float v) {
	uint32_t bits = float_bits(v);
	for (unsigned i = 0; i < 4; ++i) {
		h = (h ^ uint8_t(bits >> (i * 8))) * FNV_PRIME;
	}
}
void hash_vector(uint64_t &h, b3Vec3 v) {
	hash_float(h, v.x);
	hash_float(h, v.y);
	hash_float(h, v.z);
}
uint64_t checksum(const std::vector<uint8_t> &bytes, size_t end) {
	uint64_t h = FNV_OFFSET;
	for (size_t i = 0; i < end; ++i) {
		h = (h ^ bytes[i]) * FNV_PRIME;
	}
	return h;
}
bool parse_entity(const char *name, uint64_t &entity) {
	if (!name || std::strncmp(name, "egp:", 4) != 0 || !name[4]) {
		return false;
	}
	entity = 0;
	for (const char *p = name + 4; *p; ++p) {
		if (*p < '0' || *p > '9' || entity > (uint64_t(INT64_MAX) - uint64_t(*p - '0')) / 10) {
			return false;
		}
		entity = entity * 10 + uint64_t(*p - '0');
	}
	return entity > 0;
}
bool creates(Operation op) {
	return op == Operation::CREATE_BOX || op == Operation::CREATE_SPHERE || op == Operation::CREATE_CAPSULE;
}
bool finite_scalar(float v) {
	return std::isfinite(v);
}
// Rotation taking the body-local +X axis onto a unit axis (prismatic joint frames).
b3Quat axis_frame(b3Vec3 axis) {
	const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
	const b3Vec3 unit = { axis.x / length, axis.y / length, axis.z / length };
	if (unit.x < -0.999999f) {
		return { { 0.0f, 1.0f, 0.0f }, 0.0f };
	}
	b3Quat q = { { 0.0f, -unit.z, unit.y }, 1.0f + unit.x };
	const float norm = std::sqrt(q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z + q.s * q.s);
	return { { q.v.x / norm, q.v.y / norm, q.v.z / norm }, q.s / norm };
}
} // namespace

DeterministicWorld::~DeterministicWorld() {
	release_world();
}
void DeterministicWorld::release_world() {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (snapshot_owner) {
		b3DestroyPlayer(snapshot_owner);
		snapshot_owner = nullptr;
	} else if (b3World_IsValid(world)) {
		b3DestroyWorld(world);
	}
	world = b3_nullWorldId;
}

Result DeterministicWorld::configure(uint32_t rate, uint32_t steps, uint32_t worker_count, b3Vec3 g) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (b3World_IsValid(world) || rate < 1 || rate > 240 || steps < 1 || steps > 16 || worker_count < 1 || worker_count > B3_MAX_WORKERS || !finite(g) || std::fegetround() != FE_TONEAREST || b3GetLengthUnitsPerMeter() != 1.0f) {
		return Result::INVALID_ARGUMENT;
	}
	if (b3GetWorldCount() >= 127) {
		return Result::LIMIT_REACHED;
	}
	tick_rate = rate;
	substeps = steps;
	workers = worker_count;
	gravity = g;
	b3WorldDef def = b3DefaultWorldDef();
	def.gravity = gravity;
	def.workerCount = workers;
	def.enableSleep = true;
	def.enableContinuous = true;
	world = b3CreateWorld(&def);
	return Result::OK;
}

Result DeterministicWorld::queue(const Command &c) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (pending.size() >= MAX_COMMANDS) {
		return Result::LIMIT_REACHED;
	}
	if (c.entity == 0 || c.entity > uint64_t(INT64_MAX) || !finite(c.value) || !finite(c.size) || !std::isfinite(c.density)) {
		return Result::INVALID_ARGUMENT;
	}
	if (c.operation < Operation::CREATE_BOX || c.operation > Operation::DESTROY_JOINT) {
		return Result::INVALID_ARGUMENT;
	}
	if (c.operation == Operation::CREATE_JOINT) {
		const float axis_length = c.axis.x * c.axis.x + c.axis.y * c.axis.y + c.axis.z * c.axis.z;
		if (c.joint_kind > JointKind::PRISMATIC || c.body_a == 0 || c.body_b == 0 || c.body_a == c.body_b || c.body_a > uint64_t(INT64_MAX) || c.body_b > uint64_t(INT64_MAX) ||
				!finite(c.anchor_a) || !finite(c.anchor_b) || !finite(c.axis) || !finite_scalar(c.length) || !finite_scalar(c.hertz) || !finite_scalar(c.damping_ratio) || !finite_scalar(c.lower) || !finite_scalar(c.upper) ||
				c.hertz < 0.0f || c.damping_ratio < 0.0f || c.lower > c.upper || (c.joint_kind == JointKind::DISTANCE && c.length <= 0.0f) || (c.joint_kind == JointKind::PRISMATIC && axis_length < 1.0e-6f)) {
			return Result::INVALID_ARGUMENT;
		}
	}
	if (c.operation == Operation::BODY_STATE) {
		float norm = c.rotation.v.x * c.rotation.v.x + c.rotation.v.y * c.rotation.v.y + c.rotation.v.z * c.rotation.v.z + c.rotation.s * c.rotation.s;
		if (!finite(c.rotation.v) || !std::isfinite(c.rotation.s) || !finite(c.linear_velocity) || !finite(c.angular_velocity) || !std::isfinite(norm) || std::fabs(norm - 1.0f) > 0.0001f) {
			return Result::INVALID_ARGUMENT;
		}
	}
	if (creates(c.operation) && (c.size.x <= 0 || c.size.y <= 0 || c.size.z <= 0 || c.density <= 0 || c.body_type < b3_staticBody || c.body_type > b3_dynamicBody)) {
		return Result::INVALID_ARGUMENT;
	}
	if (!pending_keys.emplace(c.entity, c.sequence).second) {
		return Result::DUPLICATE_COMMAND;
	}
	pending.push_back(c);
	return Result::OK;
}

Result DeterministicWorld::apply_queued_commands() {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (std::fegetround() != FE_TONEAREST || b3GetLengthUnitsPerMeter() != 1.0f) {
		return Result::INVALID_ARGUMENT;
	}
	if (pending.empty()) {
		return Result::OK;
	}
	std::sort(pending.begin(), pending.end(), [](const Command &a, const Command &b) { return a.entity != b.entity ? a.entity < b.entity : a.sequence < b.sequence; });
	// Validate every command before mutating, visiting each referenced body once.
	// Sorted entity groups eliminate a full-world temporary set on every tick.
	size_t live_count = bodies.size();
	uint64_t current_entity = 0;
	bool entity_live = false;
	std::set<uint64_t> created, destroyed, new_joints;
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT) {
			continue;
		}
		if (creates(c.operation)) {
			created.insert(c.entity);
		} else if (c.operation == Operation::DESTROY) {
			destroyed.insert(c.entity);
		}
	}
	auto body_live = [&](uint64_t entity) {
		return !destroyed.count(entity) && (bodies.count(entity) || created.count(entity));
	};
	for (const Command &c : pending) {
		if (c.operation != Operation::CREATE_JOINT) {
			continue;
		}
		if (joints.count(c.entity) || !new_joints.insert(c.entity).second || !body_live(c.body_a) || !body_live(c.body_b)) {
			return Result::INVALID_BATCH;
		}
	}
	std::set<uint64_t> removed_joints;
	for (const Command &c : pending) {
		if (c.operation == Operation::DESTROY_JOINT && (!joints.count(c.entity) || !removed_joints.insert(c.entity).second)) {
			return Result::INVALID_BATCH;
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT || c.operation == Operation::DESTROY_JOINT) {
			continue;
		}
		if (current_entity != c.entity) {
			current_entity = c.entity;
			entity_live = bodies.find(c.entity) != bodies.end();
		}
		if (creates(c.operation)) {
			if (entity_live) {
				return Result::INVALID_BATCH;
			}
			entity_live = true;
			if (++live_count > MAX_BODIES) {
				return Result::LIMIT_REACHED;
			}
		} else {
			if (!entity_live) {
				return Result::INVALID_BATCH;
			}
			if (c.operation == Operation::DESTROY) {
				entity_live = false;
				--live_count;
			}
		}
	}
	// Joint removals first, then body commands, then joint creation: a batch can rebuild
	// a constraint graph in one deterministic step.
	for (const Command &c : pending) {
		if (c.operation == Operation::DESTROY_JOINT) {
			auto joint = joints.find(c.entity);
			if (joint != joints.end()) {
				if (b3Joint_IsValid(joint->second.id)) {
					b3DestroyJoint(joint->second.id, true);
				}
				joints.erase(joint);
			}
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT || c.operation == Operation::DESTROY_JOINT) {
			continue;
		}
		if (creates(c.operation)) {
			char name[32];
			std::snprintf(name, sizeof(name), "egp:%llu", static_cast<unsigned long long>(c.entity));
			b3BodyDef def = b3DefaultBodyDef();
			def.type = c.body_type;
			def.position = { c.value.x, c.value.y, c.value.z };
			def.name = name;
			b3BodyId body = b3CreateBody(world, &def);
			b3ShapeDef shape = b3DefaultShapeDef();
			shape.density = c.density;
			if (c.operation == Operation::CREATE_BOX) {
				b3BoxHull box = b3MakeBoxHull(c.size.x, c.size.y, c.size.z);
				b3CreateHullShape(body, &shape, &box.base);
			} else if (c.operation == Operation::CREATE_SPHERE) {
				b3Sphere sphere = { {}, c.size.x };
				b3CreateSphereShape(body, &shape, &sphere);
			} else {
				b3Capsule capsule = { { 0, -c.size.y, 0 }, { 0, c.size.y, 0 }, c.size.x };
				b3CreateCapsuleShape(body, &shape, &capsule);
			}
			bodies[c.entity] = body;
		} else {
			b3BodyId body = bodies.find(c.entity)->second;
			if (c.operation == Operation::DESTROY) {
				// Box3D destroys attached joints with the body; forget them deterministically.
				for (auto joint = joints.begin(); joint != joints.end();) {
					joint = joint->second.body_a == c.entity || joint->second.body_b == c.entity ? joints.erase(joint) : std::next(joint);
				}
				b3DestroyBody(body);
				bodies.erase(c.entity);
			} else if (c.operation == Operation::IMPULSE) {
				b3Body_ApplyLinearImpulseToCenter(body, c.value, true);
			} else if (c.operation == Operation::VELOCITY) {
				b3Body_SetLinearVelocity(body, c.value);
			} else {
				b3Body_SetTransform(body, { c.value.x, c.value.y, c.value.z }, c.rotation);
				b3Body_SetLinearVelocity(body, c.linear_velocity);
				b3Body_SetAngularVelocity(body, c.angular_velocity);
			}
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT) {
			create_joint(c);
		}
	}
	clear_pending_commands();
	return Result::OK;
}

void DeterministicWorld::create_joint(const Command &c) {
	const b3BodyId a = bodies.find(c.body_a)->second;
	const b3BodyId b = bodies.find(c.body_b)->second;
	b3JointId id = b3_nullJointId;
	if (c.joint_kind == JointKind::DISTANCE) {
		b3DistanceJointDef def = b3DefaultDistanceJointDef();
		def.base.bodyIdA = a;
		def.base.bodyIdB = b;
		def.base.localFrameA.p = c.anchor_a;
		def.base.localFrameB.p = c.anchor_b;
		def.base.collideConnected = c.collide_connected;
		def.length = c.length;
		def.enableSpring = c.enable_spring;
		def.hertz = c.hertz;
		def.dampingRatio = c.damping_ratio;
		def.enableLimit = c.enable_limit;
		def.minLength = c.enable_limit ? c.lower : def.minLength;
		def.maxLength = c.enable_limit ? c.upper : def.maxLength;
		id = b3CreateDistanceJoint(world, &def);
	} else if (c.joint_kind == JointKind::SPHERICAL) {
		b3SphericalJointDef def = b3DefaultSphericalJointDef();
		def.base.bodyIdA = a;
		def.base.bodyIdB = b;
		def.base.localFrameA.p = c.anchor_a;
		def.base.localFrameB.p = c.anchor_b;
		def.base.collideConnected = c.collide_connected;
		def.enableSpring = c.enable_spring;
		def.hertz = c.hertz;
		def.dampingRatio = c.damping_ratio;
		id = b3CreateSphericalJoint(world, &def);
	} else {
		b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
		def.base.bodyIdA = a;
		def.base.bodyIdB = b;
		const b3Quat frame = axis_frame(c.axis);
		def.base.localFrameA = { c.anchor_a, frame };
		def.base.localFrameB = { c.anchor_b, frame };
		def.base.collideConnected = c.collide_connected;
		def.enableSpring = c.enable_spring;
		def.hertz = c.hertz;
		def.dampingRatio = c.damping_ratio;
		def.enableLimit = c.enable_limit;
		def.lowerTranslation = c.lower;
		def.upperTranslation = c.upper;
		id = b3CreatePrismaticJoint(world, &def);
	}
	joints[c.entity] = { id, c.body_a, c.body_b, uint32_t(c.joint_kind) };
}

Result DeterministicWorld::step_tick(uint64_t expected) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (tick == uint64_t(INT64_MAX) || expected != tick + 1) {
		return Result::WRONG_TICK;
	}
	Result result = apply_queued_commands();
	if (result != Result::OK) {
		return result;
	}
	b3World_Step(world, 1.0f / static_cast<float>(tick_rate), static_cast<int>(substeps));
	tick = expected;
	return Result::OK;
}

std::string DeterministicWorld::get_simulation_fingerprint() const {
	char text[200];
	std::snprintf(text, sizeof(text), "egp-box3d-v1:e77352cd606dc1a34209094076199549a52ea0a1:egp-joints1:f32:simd4:precise:no-fma:hz%u:ss%u:g%08x,%08x,%08x:sleep1:ccd1", tick_rate, substeps, float_bits(gravity.x), float_bits(gravity.y), float_bits(gravity.z));
	return text;
}

Result DeterministicWorld::capture_snapshot(std::vector<uint8_t> &out) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (!pending.empty()) {
		return Result::PENDING_COMMANDS;
	}
	b3Recording *recording = b3CreateRecording(0);
	b3World_StartRecording(world, recording);
	b3World_StopRecording(world);
	int size = b3Recording_GetSize(recording);
	if (size < 1 || size > int(MAX_SNAPSHOT_BYTES - 88)) {
		b3DestroyRecording(recording);
		return Result::LIMIT_REACHED;
	}
	out.clear();
	append_u64(out, joints.empty() ? SNAPSHOT_MAGIC : SNAPSHOT_MAGIC_JOINTS);
	append_u64(out, PROFILE_ID);
	append_u64(out, tick);
	append_u64(out, tick_rate);
	append_u64(out, substeps);
	append_u64(out, float_bits(gravity.x));
	append_u64(out, float_bits(gravity.y));
	append_u64(out, float_bits(gravity.z));
	append_u64(out, bodies.size());
	append_u64(out, size);
	const uint8_t *data = b3Recording_GetData(recording);
	out.insert(out.end(), data, data + size);
	if (!joints.empty()) {
		append_u64(out, joints.size());
		for (const auto &joint : joints) {
			append_u64(out, joint.first);
			append_u64(out, joint.second.body_a);
			append_u64(out, joint.second.body_b);
			append_u64(out, joint.second.kind);
		}
	}
	append_u64(out, checksum(out, out.size()));
	b3DestroyRecording(recording);
	return Result::OK;
}

Result DeterministicWorld::restore_snapshot(const std::vector<uint8_t> &bytes) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (!pending.empty()) {
		return Result::PENDING_COMMANDS;
	}
	const bool joint_table = bytes.size() >= 8 && read_u64(bytes, 0) == SNAPSHOT_MAGIC_JOINTS;
	const uint64_t recording_size = bytes.size() >= 80 ? read_u64(bytes, 72) : 0;
	const uint64_t table_offset = 80 + recording_size;
	const uint64_t joint_count = joint_table && bytes.size() >= table_offset + 16 ? read_u64(bytes, table_offset) : 0;
	const uint64_t expected_size = 88 + recording_size + (joint_table ? 8 + joint_count * 32 : 0);
	if (bytes.size() < 89 || bytes.size() > MAX_SNAPSHOT_BYTES || (read_u64(bytes, 0) != SNAPSHOT_MAGIC && !joint_table) || recording_size > MAX_SNAPSHOT_BYTES || joint_count > MAX_COMMANDS || read_u64(bytes, 8) != PROFILE_ID || read_u64(bytes, 24) != tick_rate || read_u64(bytes, 32) != substeps || read_u64(bytes, 40) != float_bits(gravity.x) || read_u64(bytes, 48) != float_bits(gravity.y) || read_u64(bytes, 56) != float_bits(gravity.z) || read_u64(bytes, 16) > uint64_t(INT64_MAX) || read_u64(bytes, 64) > MAX_BODIES || expected_size != bytes.size() || read_u64(bytes, bytes.size() - 8) != checksum(bytes, bytes.size() - 8)) {
		return Result::INVALID_SNAPSHOT;
	}
	if (b3GetWorldCount() >= 127) {
		return Result::LIMIT_REACHED;
	}
	// The recording player owns geometry and the restored world. Keep it alive while simulating.
	b3RecPlayer *candidate = b3CreatePlayer(bytes.data() + 80, int(recording_size), int(workers));
	if (!candidate) {
		return Result::INVALID_SNAPSHOT;
	}
	std::map<uint64_t, b3BodyId> restored;
	bool valid = true;
	for (int i = 0; i < b3RecPlayer_GetBodyCount(candidate); ++i) {
		b3BodyId id = b3RecPlayer_GetBodyId(candidate, i);
		if (!b3Body_IsValid(id)) {
			continue;
		}
		uint64_t entity;
		if (!parse_entity(b3Body_GetName(id), entity) || !restored.emplace(entity, id).second) {
			valid = false;
			break;
		}
	}
	if (!valid || restored.size() != read_u64(bytes, 64)) {
		b3DestroyPlayer(candidate);
		return Result::INVALID_SNAPSHOT;
	}
	// Rebind recorded joints to their identifiers through the body pairs they connect.
	std::map<uint64_t, JointRecord> restored_joints;
	std::set<std::pair<uint64_t, uint64_t>> claimed;
	for (uint64_t i = 0; valid && i < joint_count; ++i) {
		const size_t at = size_t(table_offset + 8 + i * 32);
		JointRecord record;
		const uint64_t id = read_u64(bytes, at);
		record.body_a = read_u64(bytes, at + 8);
		record.body_b = read_u64(bytes, at + 16);
		record.kind = uint32_t(read_u64(bytes, at + 24));
		auto a = restored.find(record.body_a);
		auto b = restored.find(record.body_b);
		if (a == restored.end() || b == restored.end() || restored_joints.count(id)) {
			valid = false;
			break;
		}
		std::vector<b3JointId> attached(size_t(b3Body_GetJointCount(a->second)));
		b3Body_GetJoints(a->second, attached.data(), int(attached.size()));
		bool found = false;
		for (const b3JointId joint : attached) {
			const b3BodyId other = B3_ID_EQUALS(b3Joint_GetBodyA(joint), a->second) ? b3Joint_GetBodyB(joint) : b3Joint_GetBodyA(joint);
			const auto key = std::make_pair(uint64_t(joint.index1), uint64_t(joint.generation));
			if (B3_ID_EQUALS(other, b->second) && !claimed.count(key)) {
				claimed.insert(key);
				record.id = joint;
				found = true;
				break;
			}
		}
		valid = found;
		restored_joints[id] = record;
	}
	if (!valid) {
		b3DestroyPlayer(candidate);
		return Result::INVALID_SNAPSHOT;
	}
	release_world();
	snapshot_owner = candidate;
	world = b3RecPlayer_GetWorldId(candidate);
	bodies = std::move(restored);
	joints = std::move(restored_joints);
	tick = read_u64(bytes, 16);
	return Result::OK;
}

bool DeterministicWorld::get_body_state(uint64_t entity, b3WorldTransform &transform, b3Vec3 &linear, b3Vec3 &angular) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	auto entry = bodies.find(entity);
	if (entry == bodies.end()) {
		return false;
	}
	transform = b3Body_GetTransform(entry->second);
	linear = b3Body_GetLinearVelocity(entry->second);
	angular = b3Body_GetAngularVelocity(entry->second);
	return true;
}

uint64_t DeterministicWorld::get_state_hash() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return 0;
	}
	uint64_t h = FNV_OFFSET;
	hash_u64(h, PROFILE_ID);
	hash_u64(h, tick_rate);
	hash_u64(h, substeps);
	hash_vector(h, gravity);
	hash_u64(h, tick);
	hash_u64(h, bodies.size());
	for (const auto &entry : bodies) {
		hash_u64(h, entry.first);
		hash_u64(h, b3Body_GetType(entry.second));
		hash_u64(h, b3Body_IsAwake(entry.second));
		b3WorldTransform t = b3Body_GetTransform(entry.second);
		hash_vector(h, t.p);
		hash_vector(h, t.q.v);
		hash_float(h, t.q.s);
		hash_vector(h, b3Body_GetLinearVelocity(entry.second));
		hash_vector(h, b3Body_GetAngularVelocity(entry.second));
	}
	hash_u64(h, joints.size());
	for (const auto &joint : joints) {
		hash_u64(h, joint.first);
		hash_u64(h, joint.second.body_a);
		hash_u64(h, joint.second.body_b);
		hash_u64(h, joint.second.kind);
		if (b3Joint_IsValid(joint.second.id)) {
			hash_vector(h, b3Joint_GetConstraintForce(joint.second.id));
		}
	}
	return h;
}
} // namespace egp::box3d
