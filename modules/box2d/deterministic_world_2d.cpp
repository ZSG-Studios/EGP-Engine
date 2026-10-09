// SPDX-License-Identifier: MIT
#include "deterministic_world_2d.h"

#include "simulation_guard.h"

#include <algorithm>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace egp::box2d {
namespace {
constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;
constexpr uint64_t SNAPSHOT_MAGIC = 0x3150414E53324745ull; // EG2SNAP1, little endian.
constexpr uint64_t PROFILE_ID = 0x56edae79f2949d86ull;
constexpr size_t HEADER_BYTES = 8 * 11;

bool finite(b2Vec2 v) {
	return std::isfinite(v.x) && std::isfinite(v.y);
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
uint32_t float_bits(float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	return bits;
}
float bits_float(uint64_t v) {
	const uint32_t bits = uint32_t(v);
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}
void hash_u64(uint64_t &h, uint64_t v) {
	for (unsigned i = 0; i < 8; ++i) {
		h = (h ^ uint8_t(v >> (i * 8))) * FNV_PRIME;
	}
}
void hash_float(uint64_t &h, float v) {
	hash_u64(h, float_bits(v));
}
uint64_t checksum(const std::vector<uint8_t> &bytes, size_t end) {
	uint64_t h = FNV_OFFSET;
	for (size_t i = 0; i < end; ++i) {
		h = (h ^ bytes[i]) * FNV_PRIME;
	}
	return h;
}
void *tag(uint64_t value) {
	return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
}
uint64_t untag(void *value) {
	return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}
// Ids survive an in-place restore: store index and generation, rebuild with the
// current world slot.
template <typename Id>
uint64_t pack_id(Id id) {
	return (uint64_t(uint32_t(id.index1)) << 16) | id.generation;
}
template <typename Id>
Id unpack_id(uint64_t packed, b2WorldId world) {
	Id id;
	id.index1 = int32_t(uint32_t(packed >> 16));
	id.world0 = uint16_t(world.index1 - 1);
	id.generation = uint16_t(packed & 0xffff);
	return id;
}
bool joint_operation(Operation op) {
	return op == Operation::CREATE_JOINT || op == Operation::SET_JOINT || op == Operation::DESTROY_JOINT;
}
bool world_operation(Operation op) {
	return op == Operation::SET_WORLD || op == Operation::EXPLODE;
}
bool body_operation(Operation op) {
	return !joint_operation(op) && !world_operation(op);
}
b2QueryFilter query_filter(uint64_t mask) {
	b2QueryFilter filter = b2DefaultQueryFilter();
	filter.maskBits = mask;
	return filter;
}
uint64_t shape_entity(b2ShapeId shape) {
	return untag(b2Body_GetUserData(b2Shape_GetBody(shape)));
}
uint32_t shape_index(b2ShapeId shape) {
	const uint64_t index = untag(b2Shape_GetUserData(shape));
	return index > 0 ? uint32_t(index - 1) : 0;
}
struct OverlapContext {
	std::vector<b2ShapeId> found;
};
bool collect_overlap(b2ShapeId shape, void *context) {
	static_cast<OverlapContext *>(context)->found.push_back(shape);
	return true;
}
struct CastContext {
	uint64_t ignore = 0;
	bool hit = false;
	float fraction = 1.0f;
	uint64_t entity = 0;
	uint32_t shape = 0;
	b2Vec2 point = { 0.0f, 0.0f };
	b2Vec2 normal = { 0.0f, 0.0f };
};
float collect_cast(b2ShapeId shape, b2Pos point, b2Vec2 normal, float fraction, void *context) {
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
struct MoverContext {
	b2CollisionPlane planes[16] = {};
	int count = 0;
};
bool collect_planes(b2ShapeId, const b2PlaneResult *plane, void *context) {
	auto *mover = static_cast<MoverContext *>(context);
	if (plane->hit && mover->count < 16) {
		mover->planes[mover->count++] = { plane->plane, FLT_MAX, 0.0f, true };
	}
	return true;
}
} // namespace

DeterministicWorld2D::~DeterministicWorld2D() {
	release_world();
}

void DeterministicWorld2D::release_world() {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (b2World_IsValid(world)) {
		b2DestroyWorld(world);
	}
	world = b2_nullWorldId;
	bodies.clear();
	shapes.clear();
	joints.clear();
	events.clear();
}

Result DeterministicWorld2D::configure(uint32_t rate, uint32_t steps, uint32_t worker_count, b2Vec2 g, b2EnqueueTaskCallback *enqueue, b2FinishTaskCallback *finish) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (b2World_IsValid(world) || rate < 1 || rate > 240 || steps < 1 || steps > 16 || worker_count < 1 || worker_count > 64 || !finite(g) || std::fegetround() != FE_TONEAREST) {
		return Result::INVALID_ARGUMENT;
	}
	tick_rate = rate;
	substeps = steps;
	workers = worker_count;
	configured_gravity = g;
	length_units = b2GetLengthUnitsPerMeter();
	b2WorldDef def = b2DefaultWorldDef();
	def.gravity = g;
	def.workerCount = int(workers);
	if (enqueue && finish && workers > 1) {
		def.enqueueTask = enqueue;
		def.finishTask = finish;
	}
	def.enableSleep = true;
	def.enableContinuous = true;
	world = b2CreateWorld(&def);
	return b2World_IsValid(world) ? Result::OK : Result::LIMIT_REACHED;
}

Result DeterministicWorld2D::queue(const Command &c) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (pending.size() >= MAX_COMMANDS) {
		return Result::LIMIT_REACHED;
	}
	if (c.operation < Operation::CREATE_BODY || c.operation > Operation::EXPLODE || c.entity == 0) {
		return Result::INVALID_ARGUMENT;
	}
	// World commands order under WORLD_KEY, above every body and joint identifier.
	if (world_operation(c.operation) ? c.entity != WORLD_KEY : c.entity > uint64_t(INT64_MAX)) {
		return Result::INVALID_ARGUMENT;
	}
	if (!finite(c.value) || !finite(c.point) || !std::isfinite(c.scalar) || !std::isfinite(c.drag) || !std::isfinite(c.lift)) {
		return Result::INVALID_ARGUMENT;
	}
	switch (c.operation) {
		case Operation::CREATE_BODY:
			if (!validate_props(FieldSet::BODY, c.props) || c.shapes.size() > MAX_SHAPES_PER_BODY) {
				return Result::INVALID_ARGUMENT;
			}
			for (const ShapeSpec &shape : c.shapes) {
				if (!valid_geometry(shape.geometry) || !validate_props(FieldSet::SHAPE, shape.props)) {
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
			if (c.shapes.size() != 1 || c.shape_index >= MAX_SHAPES_PER_BODY || !valid_geometry(c.shapes[0].geometry) || !validate_props(FieldSet::SHAPE, c.shapes[0].props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_SHAPE:
			if (!validate_props(FieldSet::SHAPE, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::CREATE_JOINT:
			if (uint32_t(c.joint_type) >= JOINT_TYPE_COUNT || c.body_a == 0 || c.body_b == 0 || c.body_a == c.body_b || c.body_a > uint64_t(INT64_MAX) || c.body_b > uint64_t(INT64_MAX) || !validate_props(FieldSet::JOINT, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_JOINT:
			if (!validate_props(FieldSet::JOINT, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::APPLY:
			if (c.apply > ApplyKind::WIND || (c.apply == ApplyKind::WIND && c.shape_index >= MAX_SHAPES_PER_BODY)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::SET_WORLD:
			if (!validate_props(FieldSet::WORLD, c.props)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		case Operation::EXPLODE:
			if (!(c.radius > 0.0f) || !(c.falloff >= 0.0f) || !std::isfinite(c.radius) || !std::isfinite(c.falloff) || !std::isfinite(c.impulse_per_length)) {
				return Result::INVALID_ARGUMENT;
			}
			break;
		default:
			break;
	}
	if (!pending_keys.emplace(c.entity, c.sequence).second) {
		return Result::DUPLICATE_COMMAND;
	}
	pending.push_back(c);
	return Result::OK;
}

Result DeterministicWorld2D::apply_queued_commands() {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (std::fegetround() != FE_TONEAREST) {
		return Result::INVALID_ARGUMENT;
	}
	if (pending.empty()) {
		return Result::OK;
	}
	std::sort(pending.begin(), pending.end(), [](const Command &a, const Command &b) { return a.entity != b.entity ? a.entity < b.entity : a.sequence < b.sequence; });
	// Validate every command before mutating.
	std::set<uint64_t> created, destroyed, new_joints, removed_joints;
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_BODY) {
			created.insert(c.entity);
		} else if (c.operation == Operation::DESTROY_BODY) {
			destroyed.insert(c.entity);
		}
	}
	auto body_live = [&](uint64_t entity) {
		return !destroyed.count(entity) && (bodies.count(entity) || created.count(entity));
	};
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT && (joints.count(c.entity) || !new_joints.insert(c.entity).second || !body_live(c.body_a) || !body_live(c.body_b))) {
			return Result::INVALID_BATCH;
		}
		if (c.operation == Operation::DESTROY_JOINT && (!joints.count(c.entity) || !removed_joints.insert(c.entity).second)) {
			return Result::INVALID_BATCH;
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::SET_JOINT && !new_joints.count(c.entity) && (!joints.count(c.entity) || removed_joints.count(c.entity))) {
			return Result::INVALID_BATCH;
		}
	}
	size_t live_count = bodies.size();
	uint64_t current_entity = 0;
	bool entity_live = false;
	std::set<uint32_t> entity_shapes;
	for (const Command &c : pending) {
		if (!body_operation(c.operation)) {
			continue;
		}
		if (current_entity != c.entity) {
			current_entity = c.entity;
			entity_live = bodies.count(c.entity) != 0;
			entity_shapes.clear();
			const auto indices = shapes.find(c.entity);
			if (entity_live && indices != shapes.end()) {
				for (const auto &entry : indices->second) {
					entity_shapes.insert(entry.first);
				}
			}
		}
		if (c.operation == Operation::CREATE_BODY) {
			if (entity_live) {
				return Result::INVALID_BATCH;
			}
			entity_live = true;
			if (++live_count > MAX_BODIES) {
				return Result::LIMIT_REACHED;
			}
			entity_shapes.clear();
			for (uint32_t index = 0; index < c.shapes.size(); ++index) {
				entity_shapes.insert(index);
			}
			continue;
		}
		if (!entity_live) {
			return Result::INVALID_BATCH;
		}
		switch (c.operation) {
			case Operation::DESTROY_BODY:
				entity_live = false;
				entity_shapes.clear();
				--live_count;
				break;
			case Operation::ADD_SHAPE:
				if (entity_shapes.size() >= MAX_SHAPES_PER_BODY || !entity_shapes.insert(c.shape_index).second) {
					return Result::INVALID_BATCH;
				}
				break;
			case Operation::SET_SHAPE:
				if (!entity_shapes.count(c.shape_index)) {
					return Result::INVALID_BATCH;
				}
				break;
			case Operation::DESTROY_SHAPE:
				if (!entity_shapes.erase(c.shape_index)) {
					return Result::INVALID_BATCH;
				}
				break;
			case Operation::APPLY:
				if (c.apply == ApplyKind::WIND && !entity_shapes.count(c.shape_index)) {
					return Result::INVALID_BATCH;
				}
				break;
			default:
				break;
		}
	}
	const float step = 1.0f / float(tick_rate);
	for (const Command &c : pending) {
		if (c.operation == Operation::DESTROY_JOINT) {
			auto joint = joints.find(c.entity);
			if (joint != joints.end()) {
				if (b2Joint_IsValid(joint->second.id)) {
					b2DestroyJoint(joint->second.id, true);
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
			char name[32];
			std::snprintf(name, sizeof(name), "egp:%llu", static_cast<unsigned long long>(c.entity));
			b2BodyDef def = b2DefaultBodyDef();
			apply_body_def(def, c.props);
			def.name = name;
			def.userData = tag(c.entity);
			const b2BodyId body = b2CreateBody(world, &def);
			bodies[c.entity] = body;
			shapes[c.entity];
			for (uint32_t index = 0; index < c.shapes.size(); ++index) {
				add_shape(c.entity, index, body, c.shapes[index]);
			}
			apply_body_extras(body, c.props, step);
			continue;
		}
		const b2BodyId body = bodies.find(c.entity)->second;
		if (c.operation == Operation::DESTROY_BODY) {
			// Box2D destroys attached joints with the body; forget them deterministically.
			for (auto joint = joints.begin(); joint != joints.end();) {
				joint = joint->second.body_a == c.entity || joint->second.body_b == c.entity ? joints.erase(joint) : std::next(joint);
			}
			b2DestroyBody(body);
			bodies.erase(c.entity);
			shapes.erase(c.entity);
		} else {
			apply_body_command(c, body);
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::SET_WORLD) {
			apply_world(world, c.props);
		} else if (c.operation == Operation::EXPLODE) {
			b2ExplosionDef def = b2DefaultExplosionDef();
			def.maskBits = c.mask;
			def.position = c.value;
			def.radius = c.radius;
			def.falloff = c.falloff;
			def.impulsePerLength = c.impulse_per_length;
			b2World_Explode(world, &def);
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::CREATE_JOINT) {
			const b2JointId id = create_joint(world, c.joint_type, bodies.find(c.body_a)->second, bodies.find(c.body_b)->second, c.props);
			if (b2Joint_IsValid(id)) {
				b2Joint_SetUserData(id, tag(c.entity));
			}
			joints[c.entity] = { id, c.body_a, c.body_b, uint32_t(c.joint_type) };
		}
	}
	for (const Command &c : pending) {
		if (c.operation == Operation::SET_JOINT) {
			const auto joint = joints.find(c.entity);
			if (joint != joints.end() && b2Joint_IsValid(joint->second.id)) {
				apply_joint(joint->second.id, c.props);
			}
		}
	}
	clear_pending_commands();
	return Result::OK;
}

void DeterministicWorld2D::add_shape(uint64_t entity, uint32_t index, b2BodyId body, const ShapeSpec &spec) {
	ShapeSlot slot;
	if (spec.geometry.type == ShapeType::CHAIN) {
		b2SurfaceMaterial shared;
		b2ChainDef def = chain_def(spec.props, shared);
		def.points = spec.geometry.points.data();
		def.count = int(spec.geometry.points.size());
		def.isLoop = spec.geometry.loop;
		slot.chain = b2CreateChain(body, &def);
		if (!b2Chain_IsValid(slot.chain)) {
			return;
		}
		apply_chain(slot.chain, spec.props);
	} else {
		b2ShapeDef def = b2DefaultShapeDef();
		apply_shape_def(def, spec.props);
		def.userData = tag(uint64_t(index) + 1);
		slot.shape = create_shape(body, def, spec.geometry, nullptr);
		if (!b2Shape_IsValid(slot.shape)) {
			return;
		}
	}
	shapes[entity][index] = slot;
	if (b2Chain_IsValid(slot.chain)) {
		std::vector<b2ShapeId> segments(static_cast<size_t>(b2Chain_GetSegmentCount(slot.chain)));
		const int count = b2Chain_GetSegments(slot.chain, segments.data(), int(segments.size()));
		for (int i = 0; i < count; ++i) {
			b2Shape_SetUserData(segments[size_t(i)], tag(uint64_t(index) + 1));
		}
	}
}

void DeterministicWorld2D::destroy_shape(ShapeSlot &slot) {
	if (b2Chain_IsValid(slot.chain)) {
		b2DestroyChain(slot.chain);
	} else if (b2Shape_IsValid(slot.shape)) {
		b2DestroyShape(slot.shape, true);
	}
}

void DeterministicWorld2D::apply_body_command(const Command &c, b2BodyId body) {
	switch (c.operation) {
		case Operation::SET_BODY:
			apply_body(body, c.props, 1.0f / float(tick_rate));
			break;
		case Operation::ADD_SHAPE:
			add_shape(c.entity, c.shape_index, body, c.shapes[0]);
			break;
		case Operation::SET_SHAPE: {
			ShapeSlot &slot = shapes[c.entity][c.shape_index];
			if (b2Chain_IsValid(slot.chain)) {
				apply_chain(slot.chain, c.props);
			} else if (b2Shape_IsValid(slot.shape)) {
				apply_shape(slot.shape, c.props);
			}
			break;
		}
		case Operation::DESTROY_SHAPE: {
			auto &indices = shapes[c.entity];
			const auto found = indices.find(c.shape_index);
			if (found != indices.end()) {
				destroy_shape(found->second);
				indices.erase(found);
			}
			break;
		}
		case Operation::APPLY:
			switch (c.apply) {
				case ApplyKind::FORCE:
					b2Body_ApplyForceToCenter(body, c.value, true);
					break;
				case ApplyKind::FORCE_AT_POINT:
					b2Body_ApplyForce(body, c.value, c.point, true);
					break;
				case ApplyKind::TORQUE:
					b2Body_ApplyTorque(body, c.scalar, true);
					break;
				case ApplyKind::LINEAR_IMPULSE:
					b2Body_ApplyLinearImpulseToCenter(body, c.value, true);
					break;
				case ApplyKind::IMPULSE_AT_POINT:
					b2Body_ApplyLinearImpulse(body, c.value, c.point, true);
					break;
				case ApplyKind::ANGULAR_IMPULSE:
					b2Body_ApplyAngularImpulse(body, c.scalar, true);
					break;
				case ApplyKind::WIND: {
					const ShapeSlot &slot = shapes[c.entity][c.shape_index];
					if (b2Shape_IsValid(slot.shape)) {
						b2Shape_ApplyWind(slot.shape, c.value, c.drag, c.lift, true);
					}
					break;
				}
			}
			break;
		default:
			break;
	}
}

Result DeterministicWorld2D::step_tick(uint64_t expected) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (tick == uint64_t(INT64_MAX) || expected != tick + 1) {
		return Result::WRONG_TICK;
	}
	if (b2GetLengthUnitsPerMeter() != length_units) {
		return Result::INVALID_ARGUMENT;
	}
	const Result result = apply_queued_commands();
	if (result != Result::OK) {
		return result;
	}
	b2World_Step(world, 1.0f / float(tick_rate), int(substeps));
	tick = expected;
	capture_events();
	return Result::OK;
}

ShapeRef DeterministicWorld2D::shape_ref(b2ShapeId shape) const {
	ShapeRef ref;
	if (b2Shape_IsValid(shape)) {
		ref.entity = shape_entity(shape);
		ref.shape = shape_index(shape);
	}
	return ref;
}

void DeterministicWorld2D::capture_events() {
	events.clear();
	const b2ContactEvents contacts = b2World_GetContactEvents(world);
	for (int i = 0; i < contacts.beginCount; ++i) {
		events.contact_begin.push_back({ shape_ref(contacts.beginEvents[i].shapeIdA), shape_ref(contacts.beginEvents[i].shapeIdB) });
	}
	for (int i = 0; i < contacts.endCount; ++i) {
		events.contact_end.push_back({ shape_ref(contacts.endEvents[i].shapeIdA), shape_ref(contacts.endEvents[i].shapeIdB) });
	}
	for (int i = 0; i < contacts.hitCount; ++i) {
		const b2ContactHitEvent &hit = contacts.hitEvents[i];
		events.contact_hit.push_back({ shape_ref(hit.shapeIdA), shape_ref(hit.shapeIdB), hit.point, hit.normal, hit.approachSpeed });
	}
	const b2SensorEvents sensors = b2World_GetSensorEvents(world);
	for (int i = 0; i < sensors.beginCount; ++i) {
		events.sensor_begin.push_back({ shape_ref(sensors.beginEvents[i].sensorShapeId), shape_ref(sensors.beginEvents[i].visitorShapeId) });
	}
	for (int i = 0; i < sensors.endCount; ++i) {
		events.sensor_end.push_back({ shape_ref(sensors.endEvents[i].sensorShapeId), shape_ref(sensors.endEvents[i].visitorShapeId) });
	}
	const b2BodyEvents moves = b2World_GetBodyEvents(world);
	events.moved.reserve(size_t(moves.moveCount));
	for (int i = 0; i < moves.moveCount; ++i) {
		const b2BodyMoveEvent &move = moves.moveEvents[i];
		events.moved.push_back({ untag(move.userData), move.transform.p, b2Rot_GetAngle(move.transform.q), move.fellAsleep });
	}
	const b2JointEvents joint_events = b2World_GetJointEvents(world);
	for (int i = 0; i < joint_events.count; ++i) {
		events.joints.push_back(untag(joint_events.jointEvents[i].userData));
	}
}

void DeterministicWorld2D::tag_all() {
	for (const auto &entry : bodies) {
		b2Body_SetUserData(entry.second, tag(entry.first));
	}
	std::vector<b2ShapeId> segments;
	for (const auto &body : shapes) {
		for (const auto &entry : body.second) {
			if (b2Chain_IsValid(entry.second.chain)) {
				segments.resize(static_cast<size_t>(b2Chain_GetSegmentCount(entry.second.chain)));
				const int count = b2Chain_GetSegments(entry.second.chain, segments.data(), int(segments.size()));
				for (int i = 0; i < count; ++i) {
					b2Shape_SetUserData(segments[size_t(i)], tag(uint64_t(entry.first) + 1));
				}
			} else if (b2Shape_IsValid(entry.second.shape)) {
				b2Shape_SetUserData(entry.second.shape, tag(uint64_t(entry.first) + 1));
			}
		}
	}
	for (const auto &joint : joints) {
		if (b2Joint_IsValid(joint.second.id)) {
			b2Joint_SetUserData(joint.second.id, tag(joint.first));
		}
	}
}

std::string DeterministicWorld2D::get_simulation_fingerprint() const {
	char text[200];
	std::snprintf(text, sizeof(text), "egp-box2d-v1:56edae79f2949d86142b03450d5d60f63bcf5a6f:f32:simd4:precise:no-fma:hz%u:ss%u:u%08x:g%08x,%08x:sleep1:ccd1", tick_rate, substeps, float_bits(length_units), float_bits(configured_gravity.x), float_bits(configured_gravity.y));
	return text;
}

// Layout: header (magic, profile, tick, rate, substeps, gravity x/y, image size, body
// count, table size, length units), Box2D image, identity table, checksum.
Result DeterministicWorld2D::capture_snapshot(std::vector<uint8_t> &out) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (!pending.empty()) {
		return Result::PENDING_COMMANDS;
	}
	const int size = b2World_Snapshot(world, nullptr, 0);
	if (size < 1 || size_t(size) > MAX_SNAPSHOT_BYTES / 2) {
		return Result::LIMIT_REACHED;
	}
	std::vector<uint8_t> table;
	append_u64(table, bodies.size());
	for (const auto &entry : bodies) {
		append_u64(table, entry.first);
		append_u64(table, pack_id(entry.second));
	}
	size_t shape_count = 0;
	for (const auto &body : shapes) {
		shape_count += body.second.size();
	}
	append_u64(table, shape_count);
	for (const auto &body : shapes) {
		for (const auto &entry : body.second) {
			const bool chain = b2Chain_IsValid(entry.second.chain);
			append_u64(table, body.first);
			append_u64(table, uint64_t(entry.first) | (chain ? uint64_t(1) << 32 : 0));
			append_u64(table, chain ? pack_id(entry.second.chain) : pack_id(entry.second.shape));
		}
	}
	append_u64(table, joints.size());
	for (const auto &joint : joints) {
		append_u64(table, joint.first);
		append_u64(table, joint.second.body_a);
		append_u64(table, joint.second.body_b);
		append_u64(table, joint.second.kind);
		append_u64(table, pack_id(joint.second.id));
	}
	out.clear();
	append_u64(out, SNAPSHOT_MAGIC);
	append_u64(out, PROFILE_ID);
	append_u64(out, tick);
	append_u64(out, tick_rate);
	append_u64(out, substeps);
	const b2Vec2 gravity = b2World_GetGravity(world);
	append_u64(out, float_bits(gravity.x));
	append_u64(out, float_bits(gravity.y));
	append_u64(out, uint64_t(size));
	append_u64(out, bodies.size());
	append_u64(out, table.size());
	append_u64(out, float_bits(length_units));
	const size_t image_at = out.size();
	out.resize(image_at + size_t(size));
	if (b2World_Snapshot(world, out.data() + image_at, size) != size) {
		out.clear();
		return Result::LIMIT_REACHED;
	}
	out.insert(out.end(), table.begin(), table.end());
	append_u64(out, checksum(out, out.size()));
	return Result::OK;
}

Result DeterministicWorld2D::restore_snapshot(const std::vector<uint8_t> &bytes) {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return Result::NOT_CONFIGURED;
	}
	if (!pending.empty()) {
		return Result::PENDING_COMMANDS;
	}
	if (bytes.size() < HEADER_BYTES + 8 || bytes.size() > MAX_SNAPSHOT_BYTES || read_u64(bytes, 0) != SNAPSHOT_MAGIC || read_u64(bytes, 8) != PROFILE_ID || read_u64(bytes, 24) != tick_rate || read_u64(bytes, 32) != substeps || read_u64(bytes, 80) != float_bits(length_units) || read_u64(bytes, 16) > uint64_t(INT64_MAX)) {
		return Result::INVALID_SNAPSHOT;
	}
	const uint64_t image_size = read_u64(bytes, 56);
	const uint64_t body_count = read_u64(bytes, 64);
	const uint64_t table_size = read_u64(bytes, 72);
	if (image_size == 0 || image_size > MAX_SNAPSHOT_BYTES || table_size > MAX_SNAPSHOT_BYTES || HEADER_BYTES + image_size + table_size + 8 != bytes.size() || read_u64(bytes, bytes.size() - 8) != checksum(bytes, bytes.size() - 8)) {
		return Result::INVALID_SNAPSHOT;
	}
	const b2Vec2 gravity = { bits_float(read_u64(bytes, 40)), bits_float(read_u64(bytes, 48)) };
	if (!finite(gravity)) {
		return Result::INVALID_SNAPSHOT;
	}
	// Parse the identity table completely before touching the world.
	size_t at = size_t(HEADER_BYTES + image_size);
	const size_t end = bytes.size() - 8;
	auto next = [&](uint64_t &value) {
		if (at + 8 > end) {
			return false;
		}
		value = read_u64(bytes, at);
		at += 8;
		return true;
	};
	std::map<uint64_t, b2BodyId> restored_bodies;
	std::map<uint64_t, std::map<uint32_t, ShapeSlot>> restored_shapes;
	std::map<uint64_t, JointRecord> restored_joints;
	uint64_t count = 0;
	if (!next(count) || count != body_count || count > MAX_BODIES) {
		return Result::INVALID_SNAPSHOT;
	}
	for (uint64_t i = 0; i < count; ++i) {
		uint64_t entity = 0, packed = 0;
		if (!next(entity) || !next(packed) || entity == 0 || !restored_bodies.emplace(entity, unpack_id<b2BodyId>(packed, world)).second) {
			return Result::INVALID_SNAPSHOT;
		}
		restored_shapes[entity];
	}
	if (!next(count) || count > uint64_t(MAX_BODIES) * MAX_SHAPES_PER_BODY) {
		return Result::INVALID_SNAPSHOT;
	}
	for (uint64_t i = 0; i < count; ++i) {
		uint64_t entity = 0, index = 0, packed = 0;
		if (!next(entity) || !next(index) || !next(packed) || !restored_bodies.count(entity) || (index & 0xffffffffull) >= MAX_SHAPES_PER_BODY) {
			return Result::INVALID_SNAPSHOT;
		}
		ShapeSlot slot;
		if (index >> 32) {
			slot.chain = unpack_id<b2ChainId>(packed, world);
		} else {
			slot.shape = unpack_id<b2ShapeId>(packed, world);
		}
		if (!restored_shapes[entity].emplace(uint32_t(index & 0xffffffffull), slot).second) {
			return Result::INVALID_SNAPSHOT;
		}
	}
	if (!next(count) || count > MAX_COMMANDS) {
		return Result::INVALID_SNAPSHOT;
	}
	for (uint64_t i = 0; i < count; ++i) {
		uint64_t id = 0, packed = 0, kind = 0;
		JointRecord record;
		if (!next(id) || !next(record.body_a) || !next(record.body_b) || !next(kind) || !next(packed) || kind >= JOINT_TYPE_COUNT || !restored_bodies.count(record.body_a) || !restored_bodies.count(record.body_b)) {
			return Result::INVALID_SNAPSHOT;
		}
		record.kind = uint32_t(kind);
		record.id = unpack_id<b2JointId>(packed, world);
		if (!restored_joints.emplace(id, record).second) {
			return Result::INVALID_SNAPSHOT;
		}
	}
	if (at != end) {
		return Result::INVALID_SNAPSHOT;
	}
	// In place: the world keeps its slot, so ids held at the snapshot instant stay valid.
	if (!b2World_Restore(world, bytes.data() + HEADER_BYTES, int(image_size))) {
		return Result::INVALID_SNAPSHOT;
	}
	bool valid = true;
	for (const auto &entry : restored_bodies) {
		valid = valid && b2Body_IsValid(entry.second);
	}
	for (const auto &body : restored_shapes) {
		for (const auto &entry : body.second) {
			valid = valid && (b2Chain_IsValid(entry.second.chain) || b2Shape_IsValid(entry.second.shape));
		}
	}
	for (const auto &entry : restored_joints) {
		valid = valid && b2Joint_IsValid(entry.second.id) && int(b2Joint_GetType(entry.second.id)) == int(entry.second.kind);
	}
	bodies = std::move(restored_bodies);
	shapes = std::move(restored_shapes);
	joints = std::move(restored_joints);
	events.clear();
	if (!valid) {
		// The image and table disagree; the world no longer matches any identity map.
		bodies.clear();
		shapes.clear();
		joints.clear();
		return Result::INVALID_SNAPSHOT;
	}
	// Box2D snapshots do not carry user data.
	tag_all();
	tick = read_u64(bytes, 16);
	return Result::OK;
}

bool DeterministicWorld2D::get_body_state(uint64_t entity, b2Vec2 &position, float &angle, b2Vec2 &linear, float &angular) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto entry = bodies.find(entity);
	if (entry == bodies.end() || !b2Body_IsValid(entry->second)) {
		return false;
	}
	const b2WorldTransform t = b2Body_GetTransform(entry->second);
	position = t.p;
	angle = b2Rot_GetAngle(t.q);
	linear = b2Body_GetLinearVelocity(entry->second);
	angular = b2Body_GetAngularVelocity(entry->second);
	return true;
}

uint64_t DeterministicWorld2D::get_state_hash() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return 0;
	}
	uint64_t h = FNV_OFFSET;
	hash_u64(h, PROFILE_ID);
	hash_u64(h, tick_rate);
	hash_u64(h, substeps);
	hash_u64(h, tick);
	const b2Vec2 gravity = b2World_GetGravity(world);
	hash_float(h, gravity.x);
	hash_float(h, gravity.y);
	hash_u64(h, bodies.size());
	for (const auto &entry : bodies) {
		hash_u64(h, entry.first);
		hash_u64(h, uint64_t(b2Body_GetType(entry.second)));
		hash_u64(h, b2Body_IsAwake(entry.second));
		const b2WorldTransform t = b2Body_GetTransform(entry.second);
		hash_float(h, t.p.x);
		hash_float(h, t.p.y);
		hash_float(h, t.q.c);
		hash_float(h, t.q.s);
		const b2Vec2 linear = b2Body_GetLinearVelocity(entry.second);
		hash_float(h, linear.x);
		hash_float(h, linear.y);
		hash_float(h, b2Body_GetAngularVelocity(entry.second));
	}
	hash_u64(h, joints.size());
	for (const auto &joint : joints) {
		hash_u64(h, joint.first);
		hash_u64(h, joint.second.kind);
		if (b2Joint_IsValid(joint.second.id)) {
			const b2Vec2 force = b2Joint_GetConstraintForce(joint.second.id);
			hash_float(h, force.x);
			hash_float(h, force.y);
		}
	}
	return h;
}

bool DeterministicWorld2D::read_world(Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (!b2World_IsValid(world)) {
		return false;
	}
	egp::box2d::read_world(world, out);
	return true;
}

bool DeterministicWorld2D::read_body(uint64_t entity, Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto found = bodies.find(entity);
	if (found == bodies.end() || !b2Body_IsValid(found->second)) {
		return false;
	}
	egp::box2d::read_body(found->second, out);
	return true;
}

bool DeterministicWorld2D::read_shape(uint64_t entity, uint32_t index, Values &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto body = shapes.find(entity);
	if (body == shapes.end()) {
		return false;
	}
	const auto found = body->second.find(index);
	if (found == body->second.end()) {
		return false;
	}
	if (b2Chain_IsValid(found->second.chain)) {
		// A chain reads as its first segment plus the segment count.
		b2ShapeId first;
		if (b2Chain_GetSegments(found->second.chain, &first, 1) < 1) {
			return false;
		}
		egp::box2d::read_shape(first, out);
		Value count;
		count.name = "segment_count";
		count.kind = FieldKind::INT;
		count.v[0] = b2Chain_GetSegmentCount(found->second.chain);
		out.push_back(count);
		return true;
	}
	if (!b2Shape_IsValid(found->second.shape)) {
		return false;
	}
	egp::box2d::read_shape(found->second.shape, out);
	return true;
}

bool DeterministicWorld2D::read_joint(uint64_t id, Values &out, JointType &type, uint64_t &body_a, uint64_t &body_b) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	const auto found = joints.find(id);
	if (found == joints.end() || !b2Joint_IsValid(found->second.id)) {
		return false;
	}
	egp::box2d::read_joint(found->second.id, out);
	type = JointType(found->second.kind);
	body_a = found->second.body_a;
	body_b = found->second.body_b;
	return true;
}

std::vector<uint32_t> DeterministicWorld2D::get_shape_indices(uint64_t entity) const {
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

std::vector<uint64_t> DeterministicWorld2D::get_entities() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	std::vector<uint64_t> out;
	out.reserve(bodies.size());
	for (const auto &entry : bodies) {
		out.push_back(entry.first);
	}
	return out;
}

std::vector<uint64_t> DeterministicWorld2D::get_joint_ids() const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	std::vector<uint64_t> out;
	out.reserve(joints.size());
	for (const auto &entry : joints) {
		out.push_back(entry.first);
	}
	return out;
}

void DeterministicWorld2D::cast_rays(const b2Vec2 *origins, const b2Vec2 *translations, size_t count, RayHit *hits, uint64_t mask) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	for (size_t i = 0; i < count; ++i) {
		hits[i] = {};
		if (!b2World_IsValid(world) || !finite(origins[i]) || !finite(translations[i])) {
			continue;
		}
		const b2RayResult ray = b2World_CastRayClosest(world, origins[i], translations[i], query_filter(mask));
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

void DeterministicWorld2D::overlap_aabb(b2Vec2 lower, b2Vec2 upper, uint64_t mask, std::vector<ShapeRef> &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	out.clear();
	if (!b2World_IsValid(world) || !finite(lower) || !finite(upper)) {
		return;
	}
	OverlapContext context;
	b2World_OverlapAABB(world, { 0.0f, 0.0f }, { lower, upper }, query_filter(mask), collect_overlap, &context);
	for (const b2ShapeId shape : context.found) {
		out.push_back(shape_ref(shape));
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool DeterministicWorld2D::overlap_shape(const Geometry &shape, b2Vec2 position, float angle, uint64_t mask, std::vector<ShapeRef> &out) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	out.clear();
	b2ShapeProxy proxy;
	if (!b2World_IsValid(world) || !finite(position) || !std::isfinite(angle) || !valid_geometry(shape) || !make_proxy(shape, angle, proxy)) {
		return false;
	}
	OverlapContext context;
	b2World_OverlapShape(world, position, &proxy, query_filter(mask), collect_overlap, &context);
	for (const b2ShapeId found : context.found) {
		out.push_back(shape_ref(found));
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return true;
}

bool DeterministicWorld2D::cast_shape(const Geometry &shape, b2Vec2 position, float angle, b2Vec2 translation, uint64_t mask, uint64_t ignore, RayHit &hit) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	hit = {};
	b2ShapeProxy proxy;
	if (!b2World_IsValid(world) || !finite(position) || !finite(translation) || !std::isfinite(angle) || !valid_geometry(shape) || !make_proxy(shape, angle, proxy)) {
		return false;
	}
	CastContext context;
	context.ignore = ignore;
	b2World_CastShape(world, position, &proxy, translation, query_filter(mask), collect_cast, &context);
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

b2Vec2 DeterministicWorld2D::move_capsule(b2Vec2 position, b2Vec2 point_a, b2Vec2 point_b, float radius, b2Vec2 translation, uint64_t mask, b2Vec2 *clipped) const {
	std::lock_guard<std::recursive_mutex> guard(get_simulation_mutex());
	if (clipped) {
		*clipped = translation;
	}
	if (!b2World_IsValid(world) || !finite(position) || !finite(translation) || !(radius > 0.0f)) {
		return position;
	}
	const b2Capsule mover = { point_a, point_b, radius };
	const b2QueryFilter filter = query_filter(mask);
	const b2Vec2 target = b2Add(position, translation);
	MoverContext context;
	// Box2D's mover loop: gather contact planes, solve for the reachable delta, then
	// sweep the capsule along it; stop once it no longer moves.
	for (int iteration = 0; iteration < 5; ++iteration) {
		context.count = 0;
		b2World_CollideMover(world, position, &mover, filter, collect_planes, &context);
		const b2PlaneSolverResult solved = b2SolvePlanes(b2Sub(target, position), context.planes, context.count);
		const float fraction = b2World_CastMover(world, position, &mover, solved.translation, filter);
		const b2Vec2 delta = b2MulSV(fraction, solved.translation);
		position = b2Add(position, delta);
		if (b2LengthSquared(delta) < 1.0e-8f) {
			break;
		}
	}
	if (clipped) {
		*clipped = b2ClipVector(translation, context.planes, context.count);
	}
	return position;
}

} // namespace egp::box2d
