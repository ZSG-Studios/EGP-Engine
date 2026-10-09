// SPDX-License-Identifier: MIT
#include "deterministic_world.h"

#include "simulation_guard.h"

#include <algorithm>
#include <cfenv>
#include <cfloat>
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
	return op == Operation::CREATE_BOX || op == Operation::CREATE_SPHERE || op == Operation::CREATE_CAPSULE || op == Operation::CREATE_BODY;
}
bool joint_creation(Operation op) {
	return op == Operation::CREATE_JOINT || op == Operation::CREATE_TYPED_JOINT;
}
bool joint_operation(Operation op) {
	return joint_creation(op) || op == Operation::DESTROY_JOINT || op == Operation::SET_JOINT;
}
bool world_operation(Operation op) {
	return op == Operation::SET_WORLD || op == Operation::EXPLODE;
}
// Body commands address one body (create, destroy, modify, shapes, forces).
bool body_operation(Operation op) {
	return !joint_operation(op) && !world_operation(op);
}
void shape_name(char (&name)[48], uint64_t entity, uint32_t index) {
	std::snprintf(name, sizeof(name), "egp:%llu:%u", static_cast<unsigned long long>(entity), index);
}
bool parse_shape_name(const char *name, uint64_t entity, uint32_t &index) {
	char prefix[32];
	std::snprintf(prefix, sizeof(prefix), "egp:%llu:", static_cast<unsigned long long>(entity));
	const size_t length = std::strlen(prefix);
	if (!name || std::strncmp(name, prefix, length) != 0 || !name[length]) {
		return false;
	}
	uint64_t value = 0;
	for (const char *p = name + length; *p; ++p) {
		if (*p < '0' || *p > '9' || value > 0xffffffffull / 10) {
			return false;
		}
		value = value * 10 + uint64_t(*p - '0');
	}
	if (value > 0xffffffffull) {
		return false;
	}
	index = uint32_t(value);
	return true;
}
void *tag(uint64_t value) {
	return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
}
uint64_t untag(void *value) {
	return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}
bool finite_quat(b3Quat q) {
	const float norm = q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z + q.s * q.s;
	return std::isfinite(norm) && std::fabs(norm - 1.0f) <= 0.0001f;
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
	// Meshes and height fields are referenced by the destroyed world's shapes.
	geometry.clear();
	events.clear();
}

Result DeterministicWorld::configure(uint32_t rate, uint32_t steps, uint32_t worker_count, b3Vec3 g, b3EnqueueTaskCallback *p_enqueue, b3FinishTaskCallback *p_finish) {
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
	configured_gravity = g;
	b3WorldDef def = b3DefaultWorldDef();
	def.gravity = gravity;
	def.workerCount = workers;
	enqueue_task = p_enqueue;
	finish_task = p_finish;
	if (enqueue_task && finish_task) {
		def.enqueueTask = enqueue_task;
		def.finishTask = finish_task;
	}
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
	// World commands order under WORLD_KEY, above every body and joint identifier.
	if (c.entity == 0 || (c.entity > uint64_t(INT64_MAX) && !(world_operation(c.operation) && c.entity == WORLD_KEY)) || (world_operation(c.operation) && c.entity != WORLD_KEY) || !finite(c.value) || !finite(c.size) || !std::isfinite(c.density)) {
		return Result::INVALID_ARGUMENT;
	}
	if (c.operation < Operation::CREATE_BOX || c.operation > Operation::EXPLODE) {
		return Result::INVALID_ARGUMENT;
	}
	switch (c.operation) {
		case Operation::CREATE_BODY:
			if (!validate_props(FieldSet::BODY, c.props) || c.shapes.size() > MAX_SHAPES_PER_BODY) {
				return Result::INVALID_ARGUMENT;
			}
			for (const ShapeSpec &shape : c.shapes) {
				if (!valid_geometry(shape.geometry) || !validate_props(FieldSet::SHAPE, shape.props) || (shape.geometry.type == ShapeType::COMPOUND && props_sensor(shape.props))) {
					return Result::INVALID_ARGUMENT;
				}
			}
			break;
		case Operation::SET_BODY:
			if (!validate_props(FieldSet::BODY, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::ADD_SHAPE:
			if (c.shapes.size() != 1 || c.shape_index >= MAX_SHAPES_PER_BODY || !valid_geometry(c.shapes[0].geometry) || !validate_props(FieldSet::SHAPE, c.shapes[0].props) || (c.shapes[0].geometry.type == ShapeType::COMPOUND && props_sensor(c.shapes[0].props))) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_SHAPE:
			if (!validate_props(FieldSet::SHAPE, c.props) || c.material_index < -1 || c.material_index > 255) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::CREATE_TYPED_JOINT:
			if (uint32_t(c.joint_type) >= JOINT_TYPE_COUNT || c.body_a == 0 || c.body_b == 0 || c.body_a == c.body_b || c.body_a > uint64_t(INT64_MAX) || c.body_b > uint64_t(INT64_MAX) || !validate_props(FieldSet::JOINT, c.props) || !joint_limits_ordered(c.joint_type, nullptr, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_JOINT:
			if (!validate_props(FieldSet::JOINT, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::APPLY:
			if (c.apply > ApplyKind::ANGULAR_IMPULSE || !finite(c.point)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_WORLD:
			if (!validate_props(FieldSet::WORLD, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::EXPLODE:
			if (!(c.radius > 0.0f) || !(c.falloff >= 0.0f) || !finite_scalar(c.radius) || !finite_scalar(c.falloff) || !finite_scalar(c.impulse_per_area)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		default:
			break;
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
	if (creates(c.operation) && c.operation != Operation::CREATE_BODY && (c.size.x <= 0 || c.size.y <= 0 || c.size.z <= 0 || c.density <= 0 || c.body_type < b3_staticBody || c.body_type > b3_dynamicBody)) {
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
		if (!joint_creation(c.operation)) {
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
		// Joints set in a batch exist after it: kept, or created by it.
		if (c.operation == Operation::SET_JOINT && !new_joints.count(c.entity) && (!joints.count(c.entity) || removed_joints.count(c.entity))) {
			return Result::INVALID_BATCH;
		}
	}
	// Joint limits stay ordered through every change in the batch, merged in command order
	// over the creation fields or the joint's current values.
	{
		std::map<uint64_t, std::pair<JointType, Props>> merged;
		for (const Command &c : pending) {
			if (c.operation == Operation::CREATE_TYPED_JOINT) {
				merged[c.entity] = { c.joint_type, c.props };
			} else if (c.operation == Operation::CREATE_JOINT) {
				merged[c.entity] = { JointType(c.joint_kind), Props() };
			}
		}
		for (const Command &c : pending) {
			if (c.operation != Operation::SET_JOINT) {
				continue;
			}
			const auto created_here = merged.find(c.entity);
			if (created_here != merged.end()) {
				Props &props = created_here->second.second;
				props.insert(props.end(), c.props.begin(), c.props.end());
				if (!joint_limits_ordered(created_here->second.first, nullptr, props)) {
					return Result::INVALID_BATCH;
				}
				continue;
			}
			auto &entry = merged[c.entity];
			const JointRecord &record = joints.find(c.entity)->second;
			entry.first = JointType(record.kind);
			entry.second.insert(entry.second.end(), c.props.begin(), c.props.end());
			if (!b3Joint_IsValid(record.id) || !joint_limits_ordered(entry.first, &record.id, entry.second)) {
				return Result::INVALID_BATCH;
			}
		}
	}
	// Per body: static or not, and each shape's kind. Height fields and compounds need a
	// static body, a body holding a compound cannot change type, compounds take no
	// material changes, and material indices stay inside a shape's material table.
	struct ShapeKind {
		bool static_only = false;
		bool compound = false;
		int materials = 1;
	};
	std::map<uint32_t, ShapeKind> kinds;
	bool entity_static = false;
	auto kind_of = [](const ShapeSpec &spec) {
		ShapeKind kind;
		kind.static_only = static_only(spec.geometry.type);
		kind.compound = spec.geometry.type == ShapeType::COMPOUND;
		kind.materials = 1 + int(spec.geometry.extra_materials.size());
		return kind;
	};
	for (const Command &c : pending) {
		if (!body_operation(c.operation)) {
			continue;
		}
		if (current_entity != c.entity) {
			current_entity = c.entity;
			const auto found = bodies.find(c.entity);
			entity_live = found != bodies.end();
			kinds.clear();
			entity_static = entity_live && b3Body_GetType(found->second) == b3_staticBody;
			if (entity_live) {
				const auto indices = shapes.find(c.entity);
				if (indices != shapes.end()) {
					for (const auto &entry : indices->second) {
						ShapeKind kind;
						const b3ShapeType type = b3Shape_GetType(entry.second);
						kind.compound = type == b3_compoundShape;
						kind.static_only = kind.compound || type == b3_heightShape;
						kind.materials = b3Shape_GetMeshMaterialCount(entry.second);
						kinds[entry.first] = kind;
					}
				}
			}
		}
		if (creates(c.operation)) {
			if (entity_live) {
				return Result::INVALID_BATCH;
			}
			entity_live = true;
			if (++live_count > MAX_BODIES) {
				return Result::LIMIT_REACHED;
			}
			kinds.clear();
			if (c.operation == Operation::CREATE_BODY) {
				int type = b3_staticBody;
				props_body_type(c.props, type);
				entity_static = type == b3_staticBody;
				for (uint32_t index = 0; index < c.shapes.size(); ++index) {
					kinds[index] = kind_of(c.shapes[index]);
					if (kinds[index].static_only && !entity_static) {
						return Result::INVALID_BATCH;
					}
				}
			} else {
				entity_static = c.body_type == b3_staticBody;
				kinds[0] = ShapeKind();
			}
			continue;
		}
		if (!entity_live) {
			return Result::INVALID_BATCH;
		}
		switch (c.operation) {
			case Operation::DESTROY:
				entity_live = false;
				kinds.clear();
				--live_count;
				break;
			case Operation::SET_BODY: {
				int type = 0;
				if (props_body_type(c.props, type)) {
					for (const auto &entry : kinds) {
						if (entry.second.compound || (entry.second.static_only && type != b3_staticBody)) {
							return Result::INVALID_BATCH;
						}
					}
					entity_static = type == b3_staticBody;
				}
				break;
			}
			case Operation::ADD_SHAPE: {
				const ShapeKind kind = kind_of(c.shapes[0]);
				if (kinds.size() >= MAX_SHAPES_PER_BODY || kinds.count(c.shape_index) || (kind.static_only && !entity_static)) {
					return Result::INVALID_BATCH;
				}
				kinds[c.shape_index] = kind;
				break;
			}
			case Operation::SET_SHAPE: {
				const auto kind = kinds.find(c.shape_index);
				if (kind == kinds.end() || c.material_index >= kind->second.materials || (kind->second.compound && (c.material_index >= 0 || props_material(c.props)))) {
					return Result::INVALID_BATCH;
				}
				break;
			}
			case Operation::DESTROY_SHAPE:
				if (!kinds.erase(c.shape_index)) {
					return Result::INVALID_BATCH;
				}
				break;
			default:
				break;
		}
	}
	// Joint removals first, then body commands, then world commands, then joint creation
	// and joint changes: a batch can rebuild a constraint graph in one deterministic step.
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
		if (!body_operation(c.operation)) {
			continue;
		}
		if (c.operation == Operation::CREATE_BODY) {
			create_body(c);
		} else if (creates(c.operation)) {
			char name[32];
			std::snprintf(name, sizeof(name), "egp:%llu", static_cast<unsigned long long>(c.entity));
			b3BodyDef def = b3DefaultBodyDef();
			def.type = c.body_type;
			def.position = { c.value.x, c.value.y, c.value.z };
			def.name = name;
			b3BodyId body = b3CreateBody(world, &def);
			b3ShapeDef shape = b3DefaultShapeDef();
			shape.density = c.density;
			Geometry legacy;
			if (c.operation == Operation::CREATE_BOX) {
				legacy.type = ShapeType::BOX;
				legacy.half_extents = c.size;
			} else if (c.operation == Operation::CREATE_SPHERE) {
				legacy.type = ShapeType::SPHERE;
				legacy.radius = c.size.x;
			} else {
				legacy.type = ShapeType::CAPSULE;
				legacy.center1 = { 0, -c.size.y, 0 };
				legacy.center2 = { 0, c.size.y, 0 };
				legacy.radius = c.size.x;
			}
			bodies[c.entity] = body;
			// Queries map hit shapes back to stable entity identifiers.
			b3Body_SetUserData(body, tag(c.entity));
			add_shape(c.entity, 0, body, shape, legacy);
		} else {
			b3BodyId body = bodies.find(c.entity)->second;
			if (c.operation == Operation::DESTROY) {
				// Box3D destroys attached joints with the body; forget them deterministically.
				for (auto joint = joints.begin(); joint != joints.end();) {
					joint = joint->second.body_a == c.entity || joint->second.body_b == c.entity ? joints.erase(joint) : std::next(joint);
				}
				b3DestroyBody(body);
				bodies.erase(c.entity);
				shapes.erase(c.entity);
			} else if (c.operation == Operation::IMPULSE) {
				b3Body_ApplyLinearImpulseToCenter(body, c.value, true);
			} else if (c.operation == Operation::VELOCITY) {
				b3Body_SetLinearVelocity(body, c.value);
			} else if (c.operation == Operation::BODY_STATE) {
				b3Body_SetTransform(body, { c.value.x, c.value.y, c.value.z }, c.rotation);
				b3Body_SetLinearVelocity(body, c.linear_velocity);
				b3Body_SetAngularVelocity(body, c.angular_velocity);
			} else {
				apply_body_command(c, body);
			}
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::SET_WORLD) {
			apply_world(world, c.props);
			gravity = b3World_GetGravity(world);
		} else if (c.operation == Operation::EXPLODE) {
			b3ExplosionDef def = b3DefaultExplosionDef();
			def.maskBits = c.mask;
			def.position = c.value;
			def.radius = c.radius;
			def.falloff = c.falloff;
			def.impulsePerArea = c.impulse_per_area;
			b3World_Explode(world, &def);
		}
	}
	for (const Command &c : pending) {
		if (joint_creation(c.operation)) {
			create_joint(c);
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::SET_JOINT) {
			const auto joint = joints.find(c.entity);
			if (joint != joints.end() && b3Joint_IsValid(joint->second.id)) {
				apply_joint(joint->second.id, c.props);
			}
		}
	}
	clear_pending_commands();
	return Result::OK;
}

b3ShapeId DeterministicWorld::add_shape(uint64_t entity, uint32_t index, const b3BodyId &body, b3ShapeDef def, const Geometry &shape_geometry) {
	char name[48];
	shape_name(name, entity, index);
	def.name = name;
	def.userData = tag(uint64_t(index) + 1);
	const b3ShapeId shape = geometry.create_shape(body, def, shape_geometry);
	if (b3Shape_IsValid(shape)) {
		shapes[entity][index] = shape;
	}
	return shape;
}

void DeterministicWorld::create_body(const Command &c) {
	char name[32];
	std::snprintf(name, sizeof(name), "egp:%llu", static_cast<unsigned long long>(c.entity));
	b3BodyDef def = b3DefaultBodyDef();
	apply_body_def(def, c.props);
	def.name = name;
	def.userData = tag(c.entity);
	const b3BodyId body = b3CreateBody(world, &def);
	bodies[c.entity] = body;
	shapes[c.entity];
	for (uint32_t index = 0; index < c.shapes.size(); ++index) {
		b3ShapeDef shape = b3DefaultShapeDef();
		apply_shape_def(shape, c.shapes[index].props);
		add_shape(c.entity, index, body, shape, c.shapes[index].geometry);
	}
	apply_body_extras(body, c.props, 1.0f / float(tick_rate));
}

void DeterministicWorld::apply_body_command(const Command &c, b3BodyId body) {
	switch (c.operation) {
		case Operation::SET_BODY:
			apply_body(body, c.props, 1.0f / float(tick_rate));
			break;
		case Operation::ADD_SHAPE: {
			b3ShapeDef shape = b3DefaultShapeDef();
			apply_shape_def(shape, c.shapes[0].props);
			add_shape(c.entity, c.shape_index, body, shape, c.shapes[0].geometry);
			break;
		}
		case Operation::SET_SHAPE: {
			const b3ShapeId shape = shapes[c.entity][c.shape_index];
			if (b3Shape_IsValid(shape)) {
				apply_shape(shape, c.props, c.material_index);
			}
			break;
		}
		case Operation::DESTROY_SHAPE: {
			auto &indices = shapes[c.entity];
			const auto found = indices.find(c.shape_index);
			if (found != indices.end()) {
				if (b3Shape_IsValid(found->second)) {
					b3DestroyShape(found->second, true);
				}
				indices.erase(found);
			}
			break;
		}
		case Operation::APPLY:
			switch (c.apply) {
				case ApplyKind::FORCE:
					b3Body_ApplyForceToCenter(body, c.value, true);
					break;
				case ApplyKind::FORCE_AT_POINT:
					b3Body_ApplyForce(body, c.value, c.point, true);
					break;
				case ApplyKind::TORQUE:
					b3Body_ApplyTorque(body, c.value, true);
					break;
				case ApplyKind::LINEAR_IMPULSE:
					b3Body_ApplyLinearImpulseToCenter(body, c.value, true);
					break;
				case ApplyKind::IMPULSE_AT_POINT:
					b3Body_ApplyLinearImpulse(body, c.value, c.point, true);
					break;
				case ApplyKind::ANGULAR_IMPULSE:
					b3Body_ApplyAngularImpulse(body, c.value, true);
					break;
			}
			break;
		default:
			break;
	}
}

void DeterministicWorld::create_joint(const Command &c) {
	const b3BodyId a = bodies.find(c.body_a)->second;
	const b3BodyId b = bodies.find(c.body_b)->second;
	b3JointId id = b3_nullJointId;
	uint32_t kind = uint32_t(c.joint_kind);
	if (c.operation == Operation::CREATE_TYPED_JOINT) {
		id = egp::box3d::create_joint(world, c.joint_type, a, b, c.props);
		kind = uint32_t(c.joint_type);
	} else if (c.joint_kind == JointKind::DISTANCE) {
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
	if (b3Joint_IsValid(id)) {
		b3Joint_SetUserData(id, tag(c.entity));
	}
	joints[c.entity] = { id, c.body_a, c.body_b, kind };
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
	apply_fluid();
	b3World_Step(world, 1.0f / static_cast<float>(tick_rate), static_cast<int>(substeps));
	tick = expected;
	capture_events();
	return Result::OK;
}

ShapeRef DeterministicWorld::shape_ref(b3ShapeId shape) const {
	ShapeRef ref;
	if (!b3Shape_IsValid(shape)) {
		return ref;
	}
	ref.entity = untag(b3Body_GetUserData(b3Shape_GetBody(shape)));
	const uint64_t index = untag(b3Shape_GetUserData(shape));
	ref.shape = index > 0 ? uint32_t(index - 1) : 0;
	return ref;
}

void DeterministicWorld::capture_events() {
	events.clear();
	const b3ContactEvents contacts = b3World_GetContactEvents(world);
	for (int i = 0; i < contacts.beginCount; ++i) {
		events.contact_begin.push_back({ shape_ref(contacts.beginEvents[i].shapeIdA), shape_ref(contacts.beginEvents[i].shapeIdB) });
	}
	for (int i = 0; i < contacts.endCount; ++i) {
		events.contact_end.push_back({ shape_ref(contacts.endEvents[i].shapeIdA), shape_ref(contacts.endEvents[i].shapeIdB) });
	}
	for (int i = 0; i < contacts.hitCount; ++i) {
		const b3ContactHitEvent &hit = contacts.hitEvents[i];
		events.contact_hit.push_back({ shape_ref(hit.shapeIdA), shape_ref(hit.shapeIdB), hit.point, hit.normal, hit.approachSpeed });
	}
	const b3SensorEvents sensors = b3World_GetSensorEvents(world);
	for (int i = 0; i < sensors.beginCount; ++i) {
		events.sensor_begin.push_back({ shape_ref(sensors.beginEvents[i].sensorShapeId), shape_ref(sensors.beginEvents[i].visitorShapeId) });
	}
	for (int i = 0; i < sensors.endCount; ++i) {
		events.sensor_end.push_back({ shape_ref(sensors.endEvents[i].sensorShapeId), shape_ref(sensors.endEvents[i].visitorShapeId) });
	}
	const b3BodyEvents moves = b3World_GetBodyEvents(world);
	events.moved.reserve(size_t(moves.moveCount));
	for (int i = 0; i < moves.moveCount; ++i) {
		events.moved.push_back({ untag(moves.moveEvents[i].userData), moves.moveEvents[i].transform, moves.moveEvents[i].fellAsleep });
	}
	const b3JointEvents joint_events = b3World_GetJointEvents(world);
	for (int i = 0; i < joint_events.count; ++i) {
		events.joints.push_back(untag(joint_events.jointEvents[i].userData));
	}
}

void DeterministicWorld::tag_bodies() {
	for (const auto &entry : bodies) {
		b3Body_SetUserData(entry.second, tag(entry.first));
	}
	for (const auto &body : shapes) {
		for (const auto &shape : body.second) {
			b3Shape_SetUserData(shape.second, tag(uint64_t(shape.first) + 1));
		}
	}
	for (const auto &joint : joints) {
		if (b3Joint_IsValid(joint.second.id)) {
			b3Joint_SetUserData(joint.second.id, tag(joint.first));
		}
	}
}

bool DeterministicWorld::rebuild_shapes(const std::map<uint64_t, b3BodyId> &restored, std::map<uint64_t, std::map<uint32_t, b3ShapeId>> &out) const {
	out.clear();
	std::vector<b3ShapeId> attached;
	for (const auto &entry : restored) {
		auto &indices = out[entry.first];
		attached.resize(size_t(b3Body_GetShapeCount(entry.second)));
		const int count = b3Body_GetShapes(entry.second, attached.data(), int(attached.size()));
		uint32_t unnamed = 0;
		for (int i = 0; i < count; ++i) {
			uint32_t index = 0;
			if (parse_shape_name(b3Shape_GetName(attached[size_t(i)]), entry.first, index)) {
				if (!indices.emplace(index, attached[size_t(i)]).second) {
					return false;
				}
			} else {
				++unnamed;
			}
		}
		if (unnamed > 0) {
			// Snapshots from before stable shape names hold one unnamed shape per body.
			if (unnamed != 1 || count != 1) {
				return false;
			}
			indices.emplace(0u, attached[0]);
		}
	}
	return true;
}

namespace {
uint64_t shape_entity(b3ShapeId shape) {
	return untag(b3Body_GetUserData(b3Shape_GetBody(shape)));
}
struct MoverContext {
	uint64_t ignore = 0;
	b3CollisionPlane planes[16] = {};
	int count = 0;
};
bool mover_planes(b3ShapeId shape, const b3PlaneResult *planes, int count, void *context) {
	auto *mover = static_cast<MoverContext *>(context);
	if (shape_entity(shape) == mover->ignore) {
		return true;
	}
	for (int i = 0; i < count && mover->count < 16; ++i) {
		mover->planes[mover->count++] = { planes[i].plane, FLT_MAX, 0.0f, true };
	}
	return true;
}
bool mover_filter(b3ShapeId shape, void *context) {
	return shape_entity(shape) != *static_cast<const uint64_t *>(context);
}
b3QueryFilter query_filter(uint64_t mask) {
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.maskBits = mask;
	return filter;
}
struct OverlapContext {
	const DeterministicWorld *world = nullptr;
	std::vector<b3ShapeId> found;
};
bool collect_overlap(b3ShapeId shape, void *context) {
	static_cast<OverlapContext *>(context)->found.push_back(shape);
	return true;
}
struct CastContext {
	uint64_t ignore = 0;
	bool hit = false;
	float fraction = 1.0f;
	uint64_t entity = 0;
	uint32_t shape = 0;
	b3Vec3 point = {};
	b3Vec3 normal = {};
};
uint32_t shape_index(b3ShapeId shape) {
	const uint64_t index = untag(b3Shape_GetUserData(shape));
	return index > 0 ? uint32_t(index - 1) : 0;
}
float collect_cast(b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void *context) {
	auto *cast = static_cast<CastContext *>(context);
	const uint64_t entity = shape_entity(shape);
	if (entity == cast->ignore) {
		return -1.0f;
	}
	const uint32_t index = shape_index(shape);
	// Closest hit; equal fractions resolve by identity, not traversal order.
	if (!cast->hit || fraction < cast->fraction || (fraction == cast->fraction && (entity < cast->entity || (entity == cast->entity && index < cast->shape)))) {
		cast->hit = true;
		cast->fraction = fraction;
		cast->entity = entity;
		cast->shape = index;
		cast->point = point;
		cast->normal = normal;
	}
	return fraction;
}
} // namespace

void DeterministicWorld::cast_rays(const b3Vec3 *origins, const b3Vec3 *translations, size_t count, RayHit *hits, uint64_t mask) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	for (size_t i = 0; i < count; ++i) {
		hits[i] = {};
		if (!b3World_IsValid(world)) {
			continue;
		}
		const b3RayResult ray = b3World_CastRayClosest(world, origins[i], translations[i], query_filter(mask));
		if (!ray.hit) {
			continue;
		}
		hits[i].hit = true;
		hits[i].fraction = ray.fraction;
		hits[i].point = ray.point;
		hits[i].normal = ray.normal;
		hits[i].entity = shape_entity(ray.shapeId);
		hits[i].shape = shape_index(ray.shapeId);
	}
}

void DeterministicWorld::overlap_aabb(b3Vec3 lower, b3Vec3 upper, uint64_t mask, std::vector<ShapeRef> &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	out.clear();
	if (!b3World_IsValid(world) || !finite(lower) || !finite(upper)) {
		return;
	}
	OverlapContext context;
	context.world = this;
	b3World_OverlapAABB(world, { lower, upper }, query_filter(mask), collect_overlap, &context);
	for (const b3ShapeId shape : context.found) {
		out.push_back(shape_ref(shape));
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool DeterministicWorld::overlap_shape(const Geometry &shape, b3Vec3 position, b3Quat rotation, uint64_t mask, std::vector<ShapeRef> &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	out.clear();
	std::vector<b3Vec3> points;
	b3ShapeProxy proxy;
	if (!b3World_IsValid(world) || !finite(position) || !finite_quat(rotation) || !valid_geometry(shape) || !make_proxy(shape, rotation, points, proxy)) {
		return false;
	}
	OverlapContext context;
	context.world = this;
	b3World_OverlapShape(world, position, &proxy, query_filter(mask), collect_overlap, &context);
	for (const b3ShapeId found : context.found) {
		out.push_back(shape_ref(found));
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return true;
}

bool DeterministicWorld::cast_shape(const Geometry &shape, b3Vec3 position, b3Quat rotation, b3Vec3 translation, uint64_t mask, uint64_t ignore, RayHit &hit) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	hit = {};
	std::vector<b3Vec3> points;
	b3ShapeProxy proxy;
	if (!b3World_IsValid(world) || !finite(position) || !finite(translation) || !finite_quat(rotation) || !valid_geometry(shape) || !make_proxy(shape, rotation, points, proxy)) {
		return false;
	}
	CastContext context;
	context.ignore = ignore;
	b3World_CastShape(world, position, &proxy, translation, query_filter(mask), collect_cast, &context);
	if (context.hit) {
		hit.hit = true;
		hit.fraction = context.fraction;
		hit.point = context.point;
		hit.normal = context.normal;
		hit.entity = context.entity;
		hit.shape = context.shape;
	}
	return true;
}

b3Vec3 DeterministicWorld::move_capsule(b3Vec3 position, float half_height, float radius, b3Vec3 translation, uint64_t ignore, b3Vec3 *clipped) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (clipped) {
		*clipped = translation;
	}
	if (!b3World_IsValid(world)) {
		return position;
	}
	const b3Capsule mover = { { 0.0f, -half_height, 0.0f }, { 0.0f, half_height, 0.0f }, radius };
	const b3QueryFilter filter = b3DefaultQueryFilter();
	const b3Vec3 target = { position.x + translation.x, position.y + translation.y, position.z + translation.z };
	MoverContext context;
	context.ignore = ignore;
	// Box3D's mover loop: gather contact planes, solve for the reachable delta,
	// then sweep the capsule along it; stop once it no longer moves.
	for (int iteration = 0; iteration < 5; ++iteration) {
		context.count = 0;
		b3World_CollideMover(world, position, &mover, filter, mover_planes, &context);
		const b3Vec3 wanted = { target.x - position.x, target.y - position.y, target.z - position.z };
		const b3PlaneSolverResult solved = b3SolvePlanes(wanted, context.planes, context.count);
		const float fraction = b3World_CastMover(world, position, &mover, solved.delta, filter, mover_filter, &context.ignore);
		const b3Vec3 delta = { solved.delta.x * fraction, solved.delta.y * fraction, solved.delta.z * fraction };
		position = { position.x + delta.x, position.y + delta.y, position.z + delta.z };
		if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z < 1.0e-8f) {
			break;
		}
	}
	if (clipped) {
		*clipped = b3ClipVector(translation, context.planes, context.count);
	}
	return position;
}

Result DeterministicWorld::configure_fluid(b3Vec3 lower, b3Vec3 upper, float density, float linear_drag, float angular_drag) {
	if (!(lower.x < upper.x && lower.y < upper.y && lower.z < upper.z) || !(density >= 0.0f) || !(linear_drag >= 0.0f) || !(angular_drag >= 0.0f)) {
		return Result::INVALID_ARGUMENT;
	}
	fluid = { true, lower, upper, density, linear_drag, angular_drag };
	return Result::OK;
}

Result DeterministicWorld::set_buoyant(uint64_t entity, bool enabled) {
	if (enabled) {
		buoyant.insert(entity);
	} else {
		buoyant.erase(entity);
	}
	return Result::OK;
}

void DeterministicWorld::apply_fluid() {
	if (!fluid.enabled) {
		return;
	}
	const float dt = 1.0f / static_cast<float>(tick_rate);
	const float g = std::sqrt(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z);
	for (const uint64_t entity : buoyant) {
		auto found = bodies.find(entity);
		if (found == bodies.end() || !b3Body_IsValid(found->second) || b3Body_GetType(found->second) != b3_dynamicBody) {
			continue;
		}
		const b3BodyId body = found->second;
		const b3AABB box = b3Body_ComputeAABB(body);
		const b3Vec3 centre = b3Body_GetPosition(body);
		if (centre.x < fluid.lower.x || centre.x > fluid.upper.x || centre.z < fluid.lower.z || centre.z > fluid.upper.z) {
			continue;
		}
		const float height = box.upperBound.y - box.lowerBound.y;
		const float depth = std::fmin(fluid.upper.y, box.upperBound.y) - std::fmax(fluid.lower.y, box.lowerBound.y);
		if (height <= 0.0f || depth <= 0.0f) {
			continue;
		}
		const float submerged = std::fmin(depth / height, 1.0f);
		// Archimedes on the displaced volume (mass / shape density), plus drag.
		b3ShapeId shape;
		if (b3Body_GetShapes(body, &shape, 1) < 1) {
			continue;
		}
		const float mass = b3Body_GetMass(body);
		const float body_density = b3Shape_GetDensity(shape);
		const float volume = body_density > 0.0f ? mass / body_density : 0.0f;
		const b3Vec3 linear = b3Body_GetLinearVelocity(body);
		const float lift = fluid.density * g * volume * submerged * dt;
		const float drag = fluid.linear_drag * mass * submerged * dt;
		const b3Vec3 impulse = { -linear.x * drag, lift - linear.y * drag, -linear.z * drag };
		b3Body_ApplyLinearImpulseToCenter(body, impulse, true);
		if (fluid.angular_drag > 0.0f) {
			const b3Vec3 angular = b3Body_GetAngularVelocity(body);
			const float keep = std::fmax(0.0f, 1.0f - fluid.angular_drag * submerged * dt);
			b3Body_SetAngularVelocity(body, { angular.x * keep, angular.y * keep, angular.z * keep });
		}
	}
}

std::string DeterministicWorld::get_simulation_fingerprint() const {
	char text[200];
	std::snprintf(text, sizeof(text), "egp-box3d-v1:e77352cd606dc1a34209094076199549a52ea0a1:egp-joints1:f32:simd4:precise:no-fma:hz%u:ss%u:g%08x,%08x,%08x:sleep1:ccd1", tick_rate, substeps, float_bits(configured_gravity.x), float_bits(configured_gravity.y), float_bits(configured_gravity.z));
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
	if (bytes.size() < 89 || bytes.size() > MAX_SNAPSHOT_BYTES || (read_u64(bytes, 0) != SNAPSHOT_MAGIC && !joint_table) || recording_size > MAX_SNAPSHOT_BYTES || joint_count > MAX_COMMANDS || read_u64(bytes, 8) != PROFILE_ID || read_u64(bytes, 24) != tick_rate || read_u64(bytes, 32) != substeps || read_u64(bytes, 40) > 0xffffffffull || read_u64(bytes, 48) > 0xffffffffull || read_u64(bytes, 56) > 0xffffffffull || read_u64(bytes, 16) > uint64_t(INT64_MAX) || read_u64(bytes, 64) > MAX_BODIES || expected_size != bytes.size() || read_u64(bytes, bytes.size() - 8) != checksum(bytes, bytes.size() - 8)) {
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
	b3Vec3 restored_gravity;
	{
		const uint32_t bits[3] = { uint32_t(read_u64(bytes, 40)), uint32_t(read_u64(bytes, 48)), uint32_t(read_u64(bytes, 56)) };
		std::memcpy(&restored_gravity.x, &bits[0], 4);
		std::memcpy(&restored_gravity.y, &bits[1], 4);
		std::memcpy(&restored_gravity.z, &bits[2], 4);
	}
	std::map<uint64_t, std::map<uint32_t, b3ShapeId>> restored_shapes;
	if (!valid || restored.size() != read_u64(bytes, 64) || !finite(restored_gravity) || !rebuild_shapes(restored, restored_shapes)) {
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
		if (a == restored.end() || b == restored.end() || restored_joints.count(id) || record.kind >= JOINT_TYPE_COUNT) {
			valid = false;
			break;
		}
		std::vector<b3JointId> attached(size_t(b3Body_GetJointCount(a->second)));
		b3Body_GetJoints(a->second, attached.data(), int(attached.size()));
		bool found = false;
		for (const b3JointId joint : attached) {
			const b3BodyId other = B3_ID_EQUALS(b3Joint_GetBodyA(joint), a->second) ? b3Joint_GetBodyB(joint) : b3Joint_GetBodyA(joint);
			const auto key = std::make_pair(uint64_t(joint.index1), uint64_t(joint.generation));
			if (B3_ID_EQUALS(other, b->second) && b3Joint_GetType(joint) == box3d_joint_type(JointType(record.kind)) && !claimed.count(key)) {
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
	shapes = std::move(restored_shapes);
	joints = std::move(restored_joints);
	tag_bodies();
	gravity = restored_gravity;
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

bool DeterministicWorld::read_world(Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b3World_IsValid(world)) {
		return false;
	}
	egp::box3d::read_world(world, out);
	return true;
}

bool DeterministicWorld::read_body(uint64_t entity, Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto found = bodies.find(entity);
	if (found == bodies.end() || !b3Body_IsValid(found->second)) {
		return false;
	}
	egp::box3d::read_body(found->second, out);
	return true;
}

bool DeterministicWorld::read_shape(uint64_t entity, uint32_t index, Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto body = shapes.find(entity);
	if (body == shapes.end()) {
		return false;
	}
	const auto found = body->second.find(index);
	if (found == body->second.end() || !b3Shape_IsValid(found->second)) {
		return false;
	}
	egp::box3d::read_shape(found->second, out);
	return true;
}

bool DeterministicWorld::read_shape_materials(uint64_t entity, uint32_t index, std::vector<Values> &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	out.clear();
	const auto body = shapes.find(entity);
	if (body == shapes.end()) {
		return false;
	}
	const auto found = body->second.find(index);
	if (found == body->second.end() || !b3Shape_IsValid(found->second)) {
		return false;
	}
	const int count = b3Shape_GetMeshMaterialCount(found->second);
	out.resize(size_t(count));
	for (int i = 0; i < count; ++i) {
		read_material(b3Shape_GetMeshSurfaceMaterial(found->second, i), out[size_t(i)]);
	}
	return true;
}

bool DeterministicWorld::read_joint(uint64_t id, Values &out, JointType &type, uint64_t &body_a, uint64_t &body_b) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto found = joints.find(id);
	if (found == joints.end() || !b3Joint_IsValid(found->second.id)) {
		return false;
	}
	egp::box3d::read_joint(found->second.id, out);
	type = JointType(found->second.kind);
	body_a = found->second.body_a;
	body_b = found->second.body_b;
	return true;
}

std::vector<uint32_t> DeterministicWorld::get_shape_indices(uint64_t entity) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	std::vector<uint32_t> out;
	const auto body = shapes.find(entity);
	if (body != shapes.end()) {
		for (const auto &entry : body->second) {
			out.push_back(entry.first);
		}
	}
	return out;
}

std::vector<uint64_t> DeterministicWorld::get_entities() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	std::vector<uint64_t> out;
	out.reserve(bodies.size());
	for (const auto &entry : bodies) {
		out.push_back(entry.first);
	}
	return out;
}

std::vector<uint64_t> DeterministicWorld::get_joint_ids() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	std::vector<uint64_t> out;
	out.reserve(joints.size());
	for (const auto &entry : joints) {
		out.push_back(entry.first);
	}
	return out;
}
} // namespace egp::box3d
