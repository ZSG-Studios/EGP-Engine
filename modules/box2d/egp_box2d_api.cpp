// SPDX-License-Identifier: MIT
#include "egp_box2d_api.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace egp::box2d {
namespace {

// ---- Field tables -------------------------------------------------------------

#define EGP2_WORLD_FIELDS(X)                                \
	X(GRAVITY, "gravity", VEC2)                             \
	X(RESTITUTION_THRESHOLD, "restitution_threshold", FLOAT) \
	X(HIT_EVENT_THRESHOLD, "hit_event_threshold", FLOAT)    \
	X(CONTACT_HERTZ, "contact_hertz", FLOAT)                \
	X(CONTACT_DAMPING_RATIO, "contact_damping_ratio", FLOAT) \
	X(CONTACT_SPEED, "contact_speed", FLOAT)                \
	X(MAXIMUM_LINEAR_SPEED, "maximum_linear_speed", FLOAT)  \
	X(SLEEP, "sleep", BOOL)                                 \
	X(CONTINUOUS, "continuous", BOOL)                       \
	X(WARM_STARTING, "warm_starting", BOOL)                 \
	X(SPECULATIVE, "speculative", BOOL)                     \
	X(CONTACT_RECYCLE_DISTANCE, "contact_recycle_distance", FLOAT)

#define EGP2_BODY_FIELDS(X)                         \
	X(TYPE, "type", INT)                            \
	X(POSITION, "position", VEC2)                   \
	X(ROTATION, "rotation", FLOAT)                  \
	X(LINEAR_VELOCITY, "linear_velocity", VEC2)     \
	X(ANGULAR_VELOCITY, "angular_velocity", FLOAT)  \
	X(LINEAR_DAMPING, "linear_damping", FLOAT)      \
	X(ANGULAR_DAMPING, "angular_damping", FLOAT)    \
	X(GRAVITY_SCALE, "gravity_scale", FLOAT)        \
	X(SLEEP_THRESHOLD, "sleep_threshold", FLOAT)    \
	X(LOCK_LINEAR, "lock_linear", VEC2)             \
	X(LOCK_ANGULAR, "lock_angular", BOOL)           \
	X(SLEEP, "sleep", BOOL)                         \
	X(AWAKE, "awake", BOOL)                         \
	X(BULLET, "bullet", BOOL)                       \
	X(ENABLED, "enabled", BOOL)                     \
	X(FAST_ROTATION, "fast_rotation", BOOL)         \
	X(CONTACT_RECYCLING, "contact_recycling", BOOL) \
	X(CONTACT_EVENTS, "contact_events", BOOL)       \
	X(HIT_EVENTS, "hit_events", BOOL)               \
	X(MASS, "mass", FLOAT)                          \
	X(CENTER_OF_MASS, "center_of_mass", VEC2)       \
	X(INERTIA, "inertia", FLOAT)                    \
	X(TARGET_POSITION, "target_position", VEC2)     \
	X(TARGET_ROTATION, "target_rotation", FLOAT)    \
	X(TARGET_TIME, "target_time", FLOAT)

#define EGP2_SHAPE_FIELDS(X)                                    \
	X(FRICTION, "friction", FLOAT)                              \
	X(RESTITUTION, "restitution", FLOAT)                        \
	X(ROLLING_RESISTANCE, "rolling_resistance", FLOAT)          \
	X(TANGENT_SPEED, "tangent_speed", FLOAT)                    \
	X(USER_MATERIAL, "user_material", U64)                      \
	X(DENSITY, "density", FLOAT)                                \
	X(CATEGORY, "category", U64)                                \
	X(MASK, "mask", U64)                                        \
	X(GROUP, "group", INT)                                      \
	X(SENSOR, "sensor", BOOL)                                   \
	X(SENSOR_EVENTS, "sensor_events", BOOL)                     \
	X(CONTACT_EVENTS, "contact_events", BOOL)                   \
	X(HIT_EVENTS, "hit_events", BOOL)                           \
	X(PRESOLVE_EVENTS, "presolve_events", BOOL)                 \
	X(UPDATE_BODY_MASS, "update_body_mass", BOOL)               \
	X(INVOKE_CONTACT_CREATION, "invoke_contact_creation", BOOL)

#define EGP2_JOINT_FIELDS(X)                                       \
	X(ANCHOR_A, "anchor_a", VEC2)                                  \
	X(ROTATION_A, "rotation_a", FLOAT)                             \
	X(ANCHOR_B, "anchor_b", VEC2)                                  \
	X(ROTATION_B, "rotation_b", FLOAT)                             \
	X(COLLIDE_CONNECTED, "collide_connected", BOOL)                \
	X(FORCE_THRESHOLD, "force_threshold", FLOAT)                   \
	X(TORQUE_THRESHOLD, "torque_threshold", FLOAT)                 \
	X(CONSTRAINT_HERTZ, "constraint_hertz", FLOAT)                 \
	X(CONSTRAINT_DAMPING_RATIO, "constraint_damping_ratio", FLOAT) \
	X(LENGTH, "length", FLOAT)                                     \
	X(ENABLE_SPRING, "enable_spring", BOOL)                        \
	X(LOWER_SPRING_FORCE, "lower_spring_force", FLOAT)             \
	X(UPPER_SPRING_FORCE, "upper_spring_force", FLOAT)             \
	X(HERTZ, "hertz", FLOAT)                                       \
	X(DAMPING_RATIO, "damping_ratio", FLOAT)                       \
	X(ENABLE_LIMIT, "enable_limit", BOOL)                          \
	X(MIN_LENGTH, "min_length", FLOAT)                             \
	X(MAX_LENGTH, "max_length", FLOAT)                             \
	X(ENABLE_MOTOR, "enable_motor", BOOL)                          \
	X(MAX_MOTOR_FORCE, "max_motor_force", FLOAT)                   \
	X(MOTOR_SPEED, "motor_speed", FLOAT)                           \
	X(LINEAR_VELOCITY, "linear_velocity", VEC2)                    \
	X(MAX_VELOCITY_FORCE, "max_velocity_force", FLOAT)             \
	X(ANGULAR_VELOCITY, "angular_velocity", FLOAT)                 \
	X(MAX_VELOCITY_TORQUE, "max_velocity_torque", FLOAT)           \
	X(LINEAR_HERTZ, "linear_hertz", FLOAT)                         \
	X(LINEAR_DAMPING_RATIO, "linear_damping_ratio", FLOAT)         \
	X(MAX_SPRING_FORCE, "max_spring_force", FLOAT)                 \
	X(ANGULAR_HERTZ, "angular_hertz", FLOAT)                       \
	X(ANGULAR_DAMPING_RATIO, "angular_damping_ratio", FLOAT)       \
	X(MAX_SPRING_TORQUE, "max_spring_torque", FLOAT)               \
	X(TARGET_TRANSLATION, "target_translation", FLOAT)             \
	X(LOWER_TRANSLATION, "lower_translation", FLOAT)               \
	X(UPPER_TRANSLATION, "upper_translation", FLOAT)               \
	X(TARGET_ANGLE, "target_angle", FLOAT)                         \
	X(LOWER_ANGLE, "lower_angle", FLOAT)                           \
	X(UPPER_ANGLE, "upper_angle", FLOAT)                           \
	X(MAX_MOTOR_TORQUE, "max_motor_torque", FLOAT)

#define EGP2_ENUM(id, name, kind) id,
#define EGP2_INFO(id, name, kind) { name, uint16_t(id), FieldKind::kind },
namespace world_field {
enum : uint16_t { EGP2_WORLD_FIELDS(EGP2_ENUM) COUNT };
}
namespace body_field {
enum : uint16_t { EGP2_BODY_FIELDS(EGP2_ENUM) COUNT };
}
namespace shape_field {
enum : uint16_t { EGP2_SHAPE_FIELDS(EGP2_ENUM) COUNT };
}
namespace joint_field {
enum : uint16_t { EGP2_JOINT_FIELDS(EGP2_ENUM) COUNT };
}

const std::vector<FieldInfo> &table(FieldSet set) {
	static const std::vector<FieldInfo> world = [] { using namespace world_field; return std::vector<FieldInfo>{ EGP2_WORLD_FIELDS(EGP2_INFO) }; }();
	static const std::vector<FieldInfo> body = [] { using namespace body_field; return std::vector<FieldInfo>{ EGP2_BODY_FIELDS(EGP2_INFO) }; }();
	static const std::vector<FieldInfo> shape = [] { using namespace shape_field; return std::vector<FieldInfo>{ EGP2_SHAPE_FIELDS(EGP2_INFO) }; }();
	static const std::vector<FieldInfo> joint = [] { using namespace joint_field; return std::vector<FieldInfo>{ EGP2_JOINT_FIELDS(EGP2_INFO) }; }();
	switch (set) {
		case FieldSet::WORLD:
			return world;
		case FieldSet::BODY:
			return body;
		case FieldSet::SHAPE:
			return shape;
		default:
			return joint;
	}
}

// ---- Value access -------------------------------------------------------------

class View {
	const Props &props;

public:
	explicit View(const Props &p_props) :
			props(p_props) {}
	const Prop *find(uint16_t id) const {
		// Last write wins when a field repeats.
		for (auto it = props.rbegin(); it != props.rend(); ++it) {
			if (it->id == id) {
				return &*it;
			}
		}
		return nullptr;
	}
	bool has(uint16_t id) const { return find(id) != nullptr; }
	float f(uint16_t id, float fallback) const {
		const Prop *p = find(id);
		return p ? float(p->v[0]) : fallback;
	}
	bool b(uint16_t id, bool fallback) const {
		const Prop *p = find(id);
		return p ? p->v[0] != 0.0 : fallback;
	}
	int i(uint16_t id, int fallback) const {
		const Prop *p = find(id);
		return p ? int(p->v[0]) : fallback;
	}
	uint64_t u(uint16_t id, uint64_t fallback) const {
		const Prop *p = find(id);
		return p ? p->bits : fallback;
	}
	b2Vec2 v2(uint16_t id, b2Vec2 fallback) const {
		const Prop *p = find(id);
		return p ? b2Vec2{ float(p->v[0]), float(p->v[1]) } : fallback;
	}
	b2Rot rot(uint16_t id, b2Rot fallback) const {
		const Prop *p = find(id);
		return p ? b2MakeRot(float(p->v[0])) : fallback;
	}
};

b2MotionLocks locks(const View &v, b2MotionLocks current) {
	if (const Prop *p = v.find(body_field::LOCK_LINEAR)) {
		current.linearX = p->v[0] != 0.0;
		current.linearY = p->v[1] != 0.0;
	}
	current.angularZ = v.b(body_field::LOCK_ANGULAR, current.angularZ);
	return current;
}

b2SurfaceMaterial material(const View &v, b2SurfaceMaterial m) {
	using namespace shape_field;
	m.friction = v.f(FRICTION, m.friction);
	m.restitution = v.f(RESTITUTION, m.restitution);
	m.rollingResistance = v.f(ROLLING_RESISTANCE, m.rollingResistance);
	m.tangentSpeed = v.f(TANGENT_SPEED, m.tangentSpeed);
	m.userMaterialId = v.u(USER_MATERIAL, m.userMaterialId);
	return m;
}

b2Filter filter(const View &v, b2Filter f) {
	using namespace shape_field;
	f.categoryBits = v.u(CATEGORY, f.categoryBits);
	f.maskBits = v.u(MASK, f.maskBits);
	f.groupIndex = v.i(GROUP, f.groupIndex);
	return f;
}

void apply_base(b2JointDef &base, const View &v) {
	using namespace joint_field;
	base.localFrameA = { v.v2(ANCHOR_A, base.localFrameA.p), v.rot(ROTATION_A, base.localFrameA.q) };
	base.localFrameB = { v.v2(ANCHOR_B, base.localFrameB.p), v.rot(ROTATION_B, base.localFrameB.q) };
	base.collideConnected = v.b(COLLIDE_CONNECTED, base.collideConnected);
	base.forceThreshold = v.f(FORCE_THRESHOLD, base.forceThreshold);
	base.torqueThreshold = v.f(TORQUE_THRESHOLD, base.torqueThreshold);
	base.constraintHertz = v.f(CONSTRAINT_HERTZ, base.constraintHertz);
	base.constraintDampingRatio = v.f(CONSTRAINT_DAMPING_RATIO, base.constraintDampingRatio);
}

bool finite_prop(const FieldInfo &info, const Prop &p) {
	const int count = info.kind == FieldKind::VEC2 ? 2 : 1;
	for (int k = 0; k < count; ++k) {
		if (!std::isfinite(p.v[k]) || std::fabs(p.v[k]) > 3.0e38) {
			return false;
		}
	}
	return true;
}

bool finite2(b2Vec2 v) {
	return std::isfinite(v.x) && std::isfinite(v.y);
}

b2Vec2 place(const Geometry &g, b2Vec2 local) {
	return b2Add(g.center, b2RotateVector(b2MakeRot(g.angle), local));
}

void put(Values &out, const char *name, float value) {
	Value v;
	v.name = name;
	v.v[0] = value;
	out.push_back(v);
}
void put_bool(Values &out, const char *name, bool value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::BOOL;
	v.v[0] = value ? 1.0 : 0.0;
	out.push_back(v);
}
void put_int(Values &out, const char *name, int64_t value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::INT;
	v.v[0] = double(value);
	out.push_back(v);
}
void put_u64(Values &out, const char *name, uint64_t value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::U64;
	v.bits = value;
	v.v[0] = double(value);
	out.push_back(v);
}
void put_vec(Values &out, const char *name, b2Vec2 value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::VEC2;
	v.v[0] = value.x;
	v.v[1] = value.y;
	out.push_back(v);
}

const char *const JOINT_NAMES[JOINT_TYPE_COUNT] = { "distance", "filter", "motor", "prismatic", "revolute", "weld", "wheel" };
const char *const SHAPE_NAMES[] = { "circle", "capsule", "box", "polygon", "segment", "chain" };
constexpr uint32_t SHAPE_TYPE_COUNT = sizeof(SHAPE_NAMES) / sizeof(SHAPE_NAMES[0]);

} // namespace

const FieldInfo *find_field(FieldSet set, const char *name) {
	for (const FieldInfo &info : table(set)) {
		if (std::strcmp(info.name, name) == 0) {
			return &info;
		}
	}
	return nullptr;
}

const std::vector<FieldInfo> &all_fields(FieldSet set) {
	return table(set);
}

bool validate_props(FieldSet set, const Props &props) {
	const auto &fields = table(set);
	if (props.size() > 4 * fields.size()) {
		return false;
	}
	for (const Prop &p : props) {
		if (p.id >= fields.size() || !finite_prop(fields[p.id], p)) {
			return false;
		}
		if (set == FieldSet::BODY && p.id == body_field::TYPE && (p.v[0] < 0 || p.v[0] > 2)) {
			return false;
		}
		if (set == FieldSet::BODY && (p.id == body_field::MASS || p.id == body_field::INERTIA) && p.v[0] < 0) {
			return false;
		}
		if (set == FieldSet::SHAPE && p.id == shape_field::DENSITY && p.v[0] < 0) {
			return false;
		}
	}
	if (set == FieldSet::WORLD) {
		// Box2D has no contact tuning getter: set hertz, damping ratio and speed together.
		const View v(props);
		const int tuning = int(v.has(world_field::CONTACT_HERTZ)) + int(v.has(world_field::CONTACT_DAMPING_RATIO)) + int(v.has(world_field::CONTACT_SPEED));
		if (tuning != 0 && tuning != 3) {
			return false;
		}
	}
	return true;
}

bool joint_type_from_name(const char *name, JointType &type) {
	for (uint32_t i = 0; i < JOINT_TYPE_COUNT; ++i) {
		if (std::strcmp(JOINT_NAMES[i], name) == 0) {
			type = JointType(i);
			return true;
		}
	}
	return false;
}

const char *joint_type_name(JointType type) {
	return uint32_t(type) < JOINT_TYPE_COUNT ? JOINT_NAMES[uint32_t(type)] : "unknown";
}

bool shape_type_from_name(const char *name, ShapeType &type) {
	for (uint32_t i = 0; i < SHAPE_TYPE_COUNT; ++i) {
		if (std::strcmp(SHAPE_NAMES[i], name) == 0) {
			type = ShapeType(i);
			return true;
		}
	}
	return false;
}

const char *shape_type_name(ShapeType type) {
	return uint32_t(type) < SHAPE_TYPE_COUNT ? SHAPE_NAMES[uint32_t(type)] : "unknown";
}

bool valid_geometry(const Geometry &g) {
	if (!finite2(g.center) || !finite2(g.point_a) || !finite2(g.point_b) || !finite2(g.half_extents) || !std::isfinite(g.radius) || !std::isfinite(g.angle)) {
		return false;
	}
	for (const b2Vec2 &point : g.points) {
		if (!finite2(point)) {
			return false;
		}
	}
	switch (g.type) {
		case ShapeType::CIRCLE:
		case ShapeType::CAPSULE:
			return g.radius > 0.0f;
		case ShapeType::BOX:
			return g.half_extents.x > 0.0f && g.half_extents.y > 0.0f && g.radius >= 0.0f;
		case ShapeType::POLYGON: {
			if (g.points.size() < 3 || g.points.size() > size_t(B2_MAX_POLYGON_VERTICES) || g.radius < 0.0f) {
				return false;
			}
			const b2Hull hull = b2ComputeHull(g.points.data(), int(g.points.size()));
			return hull.count >= 3;
		}
		case ShapeType::SEGMENT:
			return b2DistanceSquared(g.point_a, g.point_b) > 1.0e-8f;
		case ShapeType::CHAIN:
			// Box2D chains take 4 points or more; an open chain's first and last points are
			// ghost vertices that only smooth the end segments.
			return g.points.size() >= 4u && g.points.size() <= (1u << 16);
	}
	return false;
}

b2ShapeId create_shape(b2BodyId body, const b2ShapeDef &def, const Geometry &g, b2ChainId *chain) {
	if (chain) {
		*chain = b2_nullChainId;
	}
	switch (g.type) {
		case ShapeType::CIRCLE: {
			const b2Circle circle = { g.center, g.radius };
			return b2CreateCircleShape(body, &def, &circle);
		}
		case ShapeType::CAPSULE: {
			const b2Capsule capsule = { place(g, g.point_a), place(g, g.point_b), g.radius };
			return b2CreateCapsuleShape(body, &def, &capsule);
		}
		case ShapeType::BOX: {
			const b2Polygon box = b2MakeOffsetRoundedBox(g.half_extents.x, g.half_extents.y, g.center, b2MakeRot(g.angle), g.radius);
			return b2CreatePolygonShape(body, &def, &box);
		}
		case ShapeType::POLYGON: {
			const b2Hull hull = b2ComputeHull(g.points.data(), int(g.points.size()));
			const b2Polygon polygon = b2MakeOffsetRoundedPolygon(&hull, g.center, b2MakeRot(g.angle), g.radius);
			return b2CreatePolygonShape(body, &def, &polygon);
		}
		case ShapeType::SEGMENT: {
			const b2Segment segment = { place(g, g.point_a), place(g, g.point_b) };
			return b2CreateSegmentShape(body, &def, &segment);
		}
		case ShapeType::CHAIN:
			// Chains are created through create_chain (they have their own definition).
			return b2_nullShapeId;
	}
	return b2_nullShapeId;
}

bool make_proxy(const Geometry &g, float angle, b2ShapeProxy &proxy) {
	b2Vec2 points[B2_MAX_POLYGON_VERTICES];
	int count = 0;
	float radius = 0.0f;
	switch (g.type) {
		case ShapeType::CIRCLE:
			points[count++] = g.center;
			radius = g.radius;
			break;
		case ShapeType::CAPSULE:
			points[count++] = place(g, g.point_a);
			points[count++] = place(g, g.point_b);
			radius = g.radius;
			break;
		case ShapeType::SEGMENT:
			points[count++] = place(g, g.point_a);
			points[count++] = place(g, g.point_b);
			break;
		case ShapeType::BOX:
			for (int corner = 0; corner < 4; ++corner) {
				const b2Vec2 local = { corner & 1 ? g.half_extents.x : -g.half_extents.x, corner & 2 ? g.half_extents.y : -g.half_extents.y };
				points[count++] = place(g, local);
			}
			radius = g.radius;
			break;
		case ShapeType::POLYGON:
			if (g.points.size() > size_t(B2_MAX_POLYGON_VERTICES)) {
				return false;
			}
			for (const b2Vec2 &point : g.points) {
				points[count++] = place(g, point);
			}
			radius = g.radius;
			break;
		default:
			return false;
	}
	proxy = b2MakeOffsetProxy(points, count, radius, { 0.0f, 0.0f }, b2MakeRot(angle));
	return true;
}

void apply_body_def(b2BodyDef &def, const Props &props) {
	using namespace body_field;
	const View v(props);
	def.type = b2BodyType(v.i(TYPE, int(def.type)));
	def.position = v.v2(POSITION, def.position);
	def.rotation = v.rot(ROTATION, def.rotation);
	def.linearVelocity = v.v2(LINEAR_VELOCITY, def.linearVelocity);
	def.angularVelocity = v.f(ANGULAR_VELOCITY, def.angularVelocity);
	def.linearDamping = v.f(LINEAR_DAMPING, def.linearDamping);
	def.angularDamping = v.f(ANGULAR_DAMPING, def.angularDamping);
	def.gravityScale = v.f(GRAVITY_SCALE, def.gravityScale);
	def.sleepThreshold = v.f(SLEEP_THRESHOLD, def.sleepThreshold);
	def.motionLocks = locks(v, def.motionLocks);
	def.enableSleep = v.b(SLEEP, def.enableSleep);
	def.isAwake = v.b(AWAKE, def.isAwake);
	def.isBullet = v.b(BULLET, def.isBullet);
	def.isEnabled = v.b(ENABLED, def.isEnabled);
	def.allowFastRotation = v.b(FAST_ROTATION, def.allowFastRotation);
	def.enableContactRecycling = v.b(CONTACT_RECYCLING, def.enableContactRecycling);
}

void apply_shape_def(b2ShapeDef &def, const Props &props) {
	using namespace shape_field;
	const View v(props);
	def.material = material(v, def.material);
	def.density = v.f(DENSITY, def.density);
	def.filter = filter(v, def.filter);
	def.isSensor = v.b(SENSOR, def.isSensor);
	def.enableSensorEvents = v.b(SENSOR_EVENTS, def.enableSensorEvents);
	def.enableContactEvents = v.b(CONTACT_EVENTS, def.enableContactEvents);
	def.enableHitEvents = v.b(HIT_EVENTS, def.enableHitEvents);
	def.enablePreSolveEvents = v.b(PRESOLVE_EVENTS, def.enablePreSolveEvents);
	def.updateBodyMass = v.b(UPDATE_BODY_MASS, def.updateBodyMass);
	def.invokeContactCreation = v.b(INVOKE_CONTACT_CREATION, def.invokeContactCreation);
}

b2ChainDef chain_def(const Props &props, b2SurfaceMaterial &shared) {
	using namespace shape_field;
	const View v(props);
	b2ChainDef def = b2DefaultChainDef();
	shared = material(v, b2DefaultSurfaceMaterial());
	def.materials = &shared;
	def.materialCount = 1;
	def.filter = filter(v, def.filter);
	def.enableSensorEvents = v.b(SENSOR_EVENTS, def.enableSensorEvents);
	return def;
}

b2JointId create_joint(b2WorldId world, JointType type, b2BodyId a, b2BodyId b, const Props &props) {
	using namespace joint_field;
	const View v(props);
	auto base = [&](b2JointDef &def) {
		def.bodyIdA = a;
		def.bodyIdB = b;
		apply_base(def, v);
	};
	switch (type) {
		case JointType::DISTANCE: {
			b2DistanceJointDef def = b2DefaultDistanceJointDef();
			base(def.base);
			def.length = v.f(LENGTH, def.length);
			def.enableSpring = v.b(ENABLE_SPRING, def.enableSpring);
			def.lowerSpringForce = v.f(LOWER_SPRING_FORCE, def.lowerSpringForce);
			def.upperSpringForce = v.f(UPPER_SPRING_FORCE, def.upperSpringForce);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.enableLimit = v.b(ENABLE_LIMIT, def.enableLimit);
			def.minLength = v.f(MIN_LENGTH, def.minLength);
			def.maxLength = v.f(MAX_LENGTH, def.maxLength);
			def.enableMotor = v.b(ENABLE_MOTOR, def.enableMotor);
			def.maxMotorForce = v.f(MAX_MOTOR_FORCE, def.maxMotorForce);
			def.motorSpeed = v.f(MOTOR_SPEED, def.motorSpeed);
			return b2CreateDistanceJoint(world, &def);
		}
		case JointType::FILTER: {
			b2FilterJointDef def = b2DefaultFilterJointDef();
			base(def.base);
			return b2CreateFilterJoint(world, &def);
		}
		case JointType::MOTOR: {
			b2MotorJointDef def = b2DefaultMotorJointDef();
			base(def.base);
			def.linearVelocity = v.v2(LINEAR_VELOCITY, def.linearVelocity);
			def.maxVelocityForce = v.f(MAX_VELOCITY_FORCE, def.maxVelocityForce);
			def.angularVelocity = v.f(ANGULAR_VELOCITY, def.angularVelocity);
			def.maxVelocityTorque = v.f(MAX_VELOCITY_TORQUE, def.maxVelocityTorque);
			def.linearHertz = v.f(LINEAR_HERTZ, def.linearHertz);
			def.linearDampingRatio = v.f(LINEAR_DAMPING_RATIO, def.linearDampingRatio);
			def.maxSpringForce = v.f(MAX_SPRING_FORCE, def.maxSpringForce);
			def.angularHertz = v.f(ANGULAR_HERTZ, def.angularHertz);
			def.angularDampingRatio = v.f(ANGULAR_DAMPING_RATIO, def.angularDampingRatio);
			def.maxSpringTorque = v.f(MAX_SPRING_TORQUE, def.maxSpringTorque);
			return b2CreateMotorJoint(world, &def);
		}
		case JointType::PRISMATIC: {
			b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
			base(def.base);
			def.enableSpring = v.b(ENABLE_SPRING, def.enableSpring);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.targetTranslation = v.f(TARGET_TRANSLATION, def.targetTranslation);
			def.enableLimit = v.b(ENABLE_LIMIT, def.enableLimit);
			def.lowerTranslation = v.f(LOWER_TRANSLATION, def.lowerTranslation);
			def.upperTranslation = v.f(UPPER_TRANSLATION, def.upperTranslation);
			def.enableMotor = v.b(ENABLE_MOTOR, def.enableMotor);
			def.maxMotorForce = v.f(MAX_MOTOR_FORCE, def.maxMotorForce);
			def.motorSpeed = v.f(MOTOR_SPEED, def.motorSpeed);
			return b2CreatePrismaticJoint(world, &def);
		}
		case JointType::REVOLUTE: {
			b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
			base(def.base);
			def.targetAngle = v.f(TARGET_ANGLE, def.targetAngle);
			def.enableSpring = v.b(ENABLE_SPRING, def.enableSpring);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.enableLimit = v.b(ENABLE_LIMIT, def.enableLimit);
			def.lowerAngle = v.f(LOWER_ANGLE, def.lowerAngle);
			def.upperAngle = v.f(UPPER_ANGLE, def.upperAngle);
			def.enableMotor = v.b(ENABLE_MOTOR, def.enableMotor);
			def.maxMotorTorque = v.f(MAX_MOTOR_TORQUE, def.maxMotorTorque);
			def.motorSpeed = v.f(MOTOR_SPEED, def.motorSpeed);
			return b2CreateRevoluteJoint(world, &def);
		}
		case JointType::WELD: {
			b2WeldJointDef def = b2DefaultWeldJointDef();
			base(def.base);
			def.linearHertz = v.f(LINEAR_HERTZ, def.linearHertz);
			def.angularHertz = v.f(ANGULAR_HERTZ, def.angularHertz);
			def.linearDampingRatio = v.f(LINEAR_DAMPING_RATIO, def.linearDampingRatio);
			def.angularDampingRatio = v.f(ANGULAR_DAMPING_RATIO, def.angularDampingRatio);
			return b2CreateWeldJoint(world, &def);
		}
		case JointType::WHEEL: {
			b2WheelJointDef def = b2DefaultWheelJointDef();
			base(def.base);
			def.enableSpring = v.b(ENABLE_SPRING, def.enableSpring);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.enableLimit = v.b(ENABLE_LIMIT, def.enableLimit);
			def.lowerTranslation = v.f(LOWER_TRANSLATION, def.lowerTranslation);
			def.upperTranslation = v.f(UPPER_TRANSLATION, def.upperTranslation);
			def.enableMotor = v.b(ENABLE_MOTOR, def.enableMotor);
			def.maxMotorTorque = v.f(MAX_MOTOR_TORQUE, def.maxMotorTorque);
			def.motorSpeed = v.f(MOTOR_SPEED, def.motorSpeed);
			return b2CreateWheelJoint(world, &def);
		}
	}
	return b2_nullJointId;
}

void apply_world(b2WorldId world, const Props &props) {
	using namespace world_field;
	const View v(props);
	if (v.has(GRAVITY)) {
		b2World_SetGravity(world, v.v2(GRAVITY, { 0.0f, 0.0f }));
	}
	if (v.has(RESTITUTION_THRESHOLD)) {
		b2World_SetRestitutionThreshold(world, v.f(RESTITUTION_THRESHOLD, 0));
	}
	if (v.has(HIT_EVENT_THRESHOLD)) {
		b2World_SetHitEventThreshold(world, v.f(HIT_EVENT_THRESHOLD, 0));
	}
	if (v.has(CONTACT_HERTZ)) {
		b2World_SetContactTuning(world, v.f(CONTACT_HERTZ, 0), v.f(CONTACT_DAMPING_RATIO, 0), v.f(CONTACT_SPEED, 0));
	}
	if (v.has(MAXIMUM_LINEAR_SPEED)) {
		b2World_SetMaximumLinearSpeed(world, v.f(MAXIMUM_LINEAR_SPEED, 0));
	}
	if (v.has(SLEEP)) {
		b2World_EnableSleeping(world, v.b(SLEEP, true));
	}
	if (v.has(CONTINUOUS)) {
		b2World_EnableContinuous(world, v.b(CONTINUOUS, true));
	}
	if (v.has(WARM_STARTING)) {
		b2World_EnableWarmStarting(world, v.b(WARM_STARTING, true));
	}
	if (v.has(SPECULATIVE)) {
		b2World_EnableSpeculative(world, v.b(SPECULATIVE, true));
	}
	if (v.has(CONTACT_RECYCLE_DISTANCE)) {
		b2World_SetContactRecycleDistance(world, v.f(CONTACT_RECYCLE_DISTANCE, 0));
	}
}

void apply_body(b2BodyId body, const Props &props, float step) {
	using namespace body_field;
	const View v(props);
	if (v.has(TYPE)) {
		b2Body_SetType(body, b2BodyType(v.i(TYPE, 2)));
	}
	if (v.has(POSITION) || v.has(ROTATION)) {
		b2Body_SetTransform(body, v.v2(POSITION, b2Body_GetPosition(body)), v.rot(ROTATION, b2Body_GetRotation(body)));
	}
	if (v.has(LINEAR_VELOCITY)) {
		b2Body_SetLinearVelocity(body, v.v2(LINEAR_VELOCITY, { 0.0f, 0.0f }));
	}
	if (v.has(ANGULAR_VELOCITY)) {
		b2Body_SetAngularVelocity(body, v.f(ANGULAR_VELOCITY, 0));
	}
	if (v.has(LINEAR_DAMPING)) {
		b2Body_SetLinearDamping(body, v.f(LINEAR_DAMPING, 0));
	}
	if (v.has(ANGULAR_DAMPING)) {
		b2Body_SetAngularDamping(body, v.f(ANGULAR_DAMPING, 0));
	}
	if (v.has(GRAVITY_SCALE)) {
		b2Body_SetGravityScale(body, v.f(GRAVITY_SCALE, 1));
	}
	if (v.has(SLEEP_THRESHOLD)) {
		b2Body_SetSleepThreshold(body, v.f(SLEEP_THRESHOLD, 0));
	}
	if (v.has(LOCK_LINEAR) || v.has(LOCK_ANGULAR)) {
		b2Body_SetMotionLocks(body, locks(v, b2Body_GetMotionLocks(body)));
	}
	if (v.has(SLEEP)) {
		b2Body_EnableSleep(body, v.b(SLEEP, true));
	}
	if (v.has(BULLET)) {
		b2Body_SetBullet(body, v.b(BULLET, false));
	}
	if (v.has(ENABLED)) {
		if (v.b(ENABLED, true)) {
			b2Body_Enable(body);
		} else {
			b2Body_Disable(body);
		}
	}
	if (v.has(CONTACT_RECYCLING)) {
		b2Body_EnableContactRecycling(body, v.b(CONTACT_RECYCLING, true));
	}
	apply_body_extras(body, props, step);
	if (v.has(AWAKE)) {
		b2Body_SetAwake(body, v.b(AWAKE, true));
	}
}

void apply_body_extras(b2BodyId body, const Props &props, float step) {
	using namespace body_field;
	const View v(props);
	if (v.has(CONTACT_EVENTS)) {
		b2Body_EnableContactEvents(body, v.b(CONTACT_EVENTS, true));
	}
	if (v.has(HIT_EVENTS)) {
		b2Body_EnableHitEvents(body, v.b(HIT_EVENTS, false));
	}
	if (v.has(MASS) || v.has(CENTER_OF_MASS) || v.has(INERTIA)) {
		b2MassData mass = b2Body_GetMassData(body);
		mass.mass = v.f(MASS, mass.mass);
		mass.center = v.v2(CENTER_OF_MASS, mass.center);
		mass.rotationalInertia = v.f(INERTIA, mass.rotationalInertia);
		b2Body_SetMassData(body, mass);
	}
	if (v.has(TARGET_POSITION) || v.has(TARGET_ROTATION)) {
		const b2WorldTransform target = { v.v2(TARGET_POSITION, b2Body_GetPosition(body)), v.rot(TARGET_ROTATION, b2Body_GetRotation(body)) };
		b2Body_SetTargetTransform(body, target, v.f(TARGET_TIME, step), true);
	}
}

void apply_shape(b2ShapeId shape, const Props &props) {
	using namespace shape_field;
	const View v(props);
	if (v.has(FRICTION) || v.has(RESTITUTION) || v.has(ROLLING_RESISTANCE) || v.has(TANGENT_SPEED) || v.has(USER_MATERIAL)) {
		const b2SurfaceMaterial m = material(v, b2Shape_GetSurfaceMaterial(shape));
		b2Shape_SetSurfaceMaterial(shape, &m);
	}
	if (v.has(DENSITY)) {
		b2Shape_SetDensity(shape, v.f(DENSITY, 1), v.b(UPDATE_BODY_MASS, true));
	}
	if (v.has(CATEGORY) || v.has(MASK) || v.has(GROUP)) {
		b2Shape_SetFilter(shape, filter(v, b2Shape_GetFilter(shape)));
	}
	if (v.has(SENSOR_EVENTS)) {
		b2Shape_EnableSensorEvents(shape, v.b(SENSOR_EVENTS, false));
	}
	if (v.has(CONTACT_EVENTS)) {
		b2Shape_EnableContactEvents(shape, v.b(CONTACT_EVENTS, false));
	}
	if (v.has(HIT_EVENTS)) {
		b2Shape_EnableHitEvents(shape, v.b(HIT_EVENTS, false));
	}
	if (v.has(PRESOLVE_EVENTS)) {
		b2Shape_EnablePreSolveEvents(shape, v.b(PRESOLVE_EVENTS, false));
	}
}

void apply_chain(b2ChainId chain, const Props &props) {
	using namespace shape_field;
	const View v(props);
	if (v.has(FRICTION) || v.has(RESTITUTION) || v.has(ROLLING_RESISTANCE) || v.has(TANGENT_SPEED) || v.has(USER_MATERIAL)) {
		const int count = b2Chain_GetSurfaceMaterialCount(chain);
		for (int i = 0; i < count; ++i) {
			const b2SurfaceMaterial m = material(v, b2Chain_GetSurfaceMaterial(chain, i));
			b2Chain_SetSurfaceMaterial(chain, &m, i);
		}
	}
	// Filter and event flags live on the chain's segment shapes.
	Props segment;
	for (const Prop &p : props) {
		if (p.id == CATEGORY || p.id == MASK || p.id == GROUP || p.id == SENSOR_EVENTS || p.id == CONTACT_EVENTS || p.id == HIT_EVENTS || p.id == PRESOLVE_EVENTS) {
			segment.push_back(p);
		}
	}
	if (!segment.empty()) {
		std::vector<b2ShapeId> segments(static_cast<size_t>(b2Chain_GetSegmentCount(chain)));
		const int count = b2Chain_GetSegments(chain, segments.data(), int(segments.size()));
		for (int i = 0; i < count; ++i) {
			apply_shape(segments[size_t(i)], segment);
		}
	}
}

void apply_joint(b2JointId joint, const Props &props) {
	using namespace joint_field;
	const View v(props);
	if (v.has(ANCHOR_A) || v.has(ROTATION_A)) {
		const b2Transform frame = b2Joint_GetLocalFrameA(joint);
		b2Joint_SetLocalFrameA(joint, { v.v2(ANCHOR_A, frame.p), v.rot(ROTATION_A, frame.q) });
	}
	if (v.has(ANCHOR_B) || v.has(ROTATION_B)) {
		const b2Transform frame = b2Joint_GetLocalFrameB(joint);
		b2Joint_SetLocalFrameB(joint, { v.v2(ANCHOR_B, frame.p), v.rot(ROTATION_B, frame.q) });
	}
	if (v.has(COLLIDE_CONNECTED)) {
		b2Joint_SetCollideConnected(joint, v.b(COLLIDE_CONNECTED, false));
	}
	if (v.has(FORCE_THRESHOLD)) {
		b2Joint_SetForceThreshold(joint, v.f(FORCE_THRESHOLD, 0));
	}
	if (v.has(TORQUE_THRESHOLD)) {
		b2Joint_SetTorqueThreshold(joint, v.f(TORQUE_THRESHOLD, 0));
	}
	if (v.has(CONSTRAINT_HERTZ) || v.has(CONSTRAINT_DAMPING_RATIO)) {
		float hertz = 0, damping = 0;
		b2Joint_GetConstraintTuning(joint, &hertz, &damping);
		b2Joint_SetConstraintTuning(joint, v.f(CONSTRAINT_HERTZ, hertz), v.f(CONSTRAINT_DAMPING_RATIO, damping));
	}
	switch (b2Joint_GetType(joint)) {
		case b2_distanceJoint:
			if (v.has(LENGTH)) b2DistanceJoint_SetLength(joint, v.f(LENGTH, 1));
			if (v.has(ENABLE_SPRING)) b2DistanceJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(LOWER_SPRING_FORCE) || v.has(UPPER_SPRING_FORCE)) {
				float lower = 0, upper = 0;
				b2DistanceJoint_GetSpringForceRange(joint, &lower, &upper);
				b2DistanceJoint_SetSpringForceRange(joint, v.f(LOWER_SPRING_FORCE, lower), v.f(UPPER_SPRING_FORCE, upper));
			}
			if (v.has(HERTZ)) b2DistanceJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b2DistanceJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(ENABLE_LIMIT)) b2DistanceJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(MIN_LENGTH) || v.has(MAX_LENGTH)) b2DistanceJoint_SetLengthRange(joint, v.f(MIN_LENGTH, b2DistanceJoint_GetMinLength(joint)), v.f(MAX_LENGTH, b2DistanceJoint_GetMaxLength(joint)));
			if (v.has(ENABLE_MOTOR)) b2DistanceJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_FORCE)) b2DistanceJoint_SetMaxMotorForce(joint, v.f(MAX_MOTOR_FORCE, 0));
			if (v.has(MOTOR_SPEED)) b2DistanceJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b2_motorJoint:
			if (v.has(LINEAR_VELOCITY)) b2MotorJoint_SetLinearVelocity(joint, v.v2(LINEAR_VELOCITY, { 0.0f, 0.0f }));
			if (v.has(MAX_VELOCITY_FORCE)) b2MotorJoint_SetMaxVelocityForce(joint, v.f(MAX_VELOCITY_FORCE, 0));
			if (v.has(ANGULAR_VELOCITY)) b2MotorJoint_SetAngularVelocity(joint, v.f(ANGULAR_VELOCITY, 0));
			if (v.has(MAX_VELOCITY_TORQUE)) b2MotorJoint_SetMaxVelocityTorque(joint, v.f(MAX_VELOCITY_TORQUE, 0));
			if (v.has(LINEAR_HERTZ)) b2MotorJoint_SetLinearHertz(joint, v.f(LINEAR_HERTZ, 0));
			if (v.has(LINEAR_DAMPING_RATIO)) b2MotorJoint_SetLinearDampingRatio(joint, v.f(LINEAR_DAMPING_RATIO, 0));
			if (v.has(MAX_SPRING_FORCE)) b2MotorJoint_SetMaxSpringForce(joint, v.f(MAX_SPRING_FORCE, 0));
			if (v.has(ANGULAR_HERTZ)) b2MotorJoint_SetAngularHertz(joint, v.f(ANGULAR_HERTZ, 0));
			if (v.has(ANGULAR_DAMPING_RATIO)) b2MotorJoint_SetAngularDampingRatio(joint, v.f(ANGULAR_DAMPING_RATIO, 0));
			if (v.has(MAX_SPRING_TORQUE)) b2MotorJoint_SetMaxSpringTorque(joint, v.f(MAX_SPRING_TORQUE, 0));
			break;
		case b2_prismaticJoint:
			if (v.has(ENABLE_SPRING)) b2PrismaticJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b2PrismaticJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b2PrismaticJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(TARGET_TRANSLATION)) b2PrismaticJoint_SetTargetTranslation(joint, v.f(TARGET_TRANSLATION, 0));
			if (v.has(ENABLE_LIMIT)) b2PrismaticJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(LOWER_TRANSLATION) || v.has(UPPER_TRANSLATION)) b2PrismaticJoint_SetLimits(joint, v.f(LOWER_TRANSLATION, b2PrismaticJoint_GetLowerLimit(joint)), v.f(UPPER_TRANSLATION, b2PrismaticJoint_GetUpperLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b2PrismaticJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_FORCE)) b2PrismaticJoint_SetMaxMotorForce(joint, v.f(MAX_MOTOR_FORCE, 0));
			if (v.has(MOTOR_SPEED)) b2PrismaticJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b2_revoluteJoint:
			if (v.has(ENABLE_SPRING)) b2RevoluteJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b2RevoluteJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b2RevoluteJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(TARGET_ANGLE)) b2RevoluteJoint_SetTargetAngle(joint, v.f(TARGET_ANGLE, 0));
			if (v.has(ENABLE_LIMIT)) b2RevoluteJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(LOWER_ANGLE) || v.has(UPPER_ANGLE)) b2RevoluteJoint_SetLimits(joint, v.f(LOWER_ANGLE, b2RevoluteJoint_GetLowerLimit(joint)), v.f(UPPER_ANGLE, b2RevoluteJoint_GetUpperLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b2RevoluteJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_TORQUE)) b2RevoluteJoint_SetMaxMotorTorque(joint, v.f(MAX_MOTOR_TORQUE, 0));
			if (v.has(MOTOR_SPEED)) b2RevoluteJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b2_weldJoint:
			if (v.has(LINEAR_HERTZ)) b2WeldJoint_SetLinearHertz(joint, v.f(LINEAR_HERTZ, 0));
			if (v.has(LINEAR_DAMPING_RATIO)) b2WeldJoint_SetLinearDampingRatio(joint, v.f(LINEAR_DAMPING_RATIO, 0));
			if (v.has(ANGULAR_HERTZ)) b2WeldJoint_SetAngularHertz(joint, v.f(ANGULAR_HERTZ, 0));
			if (v.has(ANGULAR_DAMPING_RATIO)) b2WeldJoint_SetAngularDampingRatio(joint, v.f(ANGULAR_DAMPING_RATIO, 0));
			break;
		case b2_wheelJoint:
			if (v.has(ENABLE_SPRING)) b2WheelJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b2WheelJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b2WheelJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(ENABLE_LIMIT)) b2WheelJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(LOWER_TRANSLATION) || v.has(UPPER_TRANSLATION)) b2WheelJoint_SetLimits(joint, v.f(LOWER_TRANSLATION, b2WheelJoint_GetLowerLimit(joint)), v.f(UPPER_TRANSLATION, b2WheelJoint_GetUpperLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b2WheelJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_TORQUE)) b2WheelJoint_SetMaxMotorTorque(joint, v.f(MAX_MOTOR_TORQUE, 0));
			if (v.has(MOTOR_SPEED)) b2WheelJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		default:
			break;
	}
}

void read_world(b2WorldId world, Values &out) {
	out.clear();
	put_vec(out, "gravity", b2World_GetGravity(world));
	put(out, "restitution_threshold", b2World_GetRestitutionThreshold(world));
	put(out, "hit_event_threshold", b2World_GetHitEventThreshold(world));
	put(out, "maximum_linear_speed", b2World_GetMaximumLinearSpeed(world));
	put_bool(out, "sleep", b2World_IsSleepingEnabled(world));
	put_bool(out, "continuous", b2World_IsContinuousEnabled(world));
	put_bool(out, "warm_starting", b2World_IsWarmStartingEnabled(world));
	put(out, "contact_recycle_distance", b2World_GetContactRecycleDistance(world));
	put_int(out, "awake_body_count", b2World_GetAwakeBodyCount(world));
	const b2Counters counters = b2World_GetCounters(world);
	put_int(out, "body_count", counters.bodyCount);
	put_int(out, "shape_count", counters.shapeCount);
	put_int(out, "contact_count", counters.contactCount);
	put_int(out, "joint_count", counters.jointCount);
	put_int(out, "island_count", counters.islandCount);
	const b2AABB bounds = b2World_GetBounds(world);
	put_vec(out, "bounds_lower", bounds.lowerBound);
	put_vec(out, "bounds_upper", bounds.upperBound);
}

void read_body(b2BodyId body, Values &out) {
	out.clear();
	put_int(out, "type", int(b2Body_GetType(body)));
	put_vec(out, "position", b2Body_GetPosition(body));
	put(out, "rotation", b2Rot_GetAngle(b2Body_GetRotation(body)));
	put_vec(out, "linear_velocity", b2Body_GetLinearVelocity(body));
	put(out, "angular_velocity", b2Body_GetAngularVelocity(body));
	put(out, "linear_damping", b2Body_GetLinearDamping(body));
	put(out, "angular_damping", b2Body_GetAngularDamping(body));
	put(out, "gravity_scale", b2Body_GetGravityScale(body));
	put(out, "sleep_threshold", b2Body_GetSleepThreshold(body));
	const b2MotionLocks locks = b2Body_GetMotionLocks(body);
	put_vec(out, "lock_linear", { locks.linearX ? 1.0f : 0.0f, locks.linearY ? 1.0f : 0.0f });
	put_bool(out, "lock_angular", locks.angularZ);
	put_bool(out, "sleep", b2Body_IsSleepEnabled(body));
	put_bool(out, "awake", b2Body_IsAwake(body));
	put_bool(out, "bullet", b2Body_IsBullet(body));
	put_bool(out, "enabled", b2Body_IsEnabled(body));
	put_bool(out, "contact_recycling", b2Body_IsContactRecyclingEnabled(body));
	const b2MassData mass = b2Body_GetMassData(body);
	put(out, "mass", mass.mass);
	put_vec(out, "center_of_mass", mass.center);
	put(out, "inertia", mass.rotationalInertia);
	put_vec(out, "world_center_of_mass", b2Body_GetWorldCenter(body));
	put_int(out, "shape_count", b2Body_GetShapeCount(body));
	put_int(out, "joint_count", b2Body_GetJointCount(body));
}

void read_shape(b2ShapeId shape, Values &out) {
	out.clear();
	const b2SurfaceMaterial m = b2Shape_GetSurfaceMaterial(shape);
	put(out, "friction", m.friction);
	put(out, "restitution", m.restitution);
	put(out, "rolling_resistance", m.rollingResistance);
	put(out, "tangent_speed", m.tangentSpeed);
	put_u64(out, "user_material", m.userMaterialId);
	put(out, "density", b2Shape_GetDensity(shape));
	const b2Filter f = b2Shape_GetFilter(shape);
	put_u64(out, "category", f.categoryBits);
	put_u64(out, "mask", f.maskBits);
	put_int(out, "group", f.groupIndex);
	put_bool(out, "sensor", b2Shape_IsSensor(shape));
	put_bool(out, "sensor_events", b2Shape_AreSensorEventsEnabled(shape));
	put_bool(out, "contact_events", b2Shape_AreContactEventsEnabled(shape));
	put_bool(out, "hit_events", b2Shape_AreHitEventsEnabled(shape));
	put_bool(out, "presolve_events", b2Shape_ArePreSolveEventsEnabled(shape));
	const b2AABB box = b2Shape_GetAABB(shape);
	put_vec(out, "aabb_lower", box.lowerBound);
	put_vec(out, "aabb_upper", box.upperBound);
}

void read_joint(b2JointId joint, Values &out) {
	out.clear();
	const b2Transform a = b2Joint_GetLocalFrameA(joint);
	const b2Transform b = b2Joint_GetLocalFrameB(joint);
	put_vec(out, "anchor_a", a.p);
	put(out, "rotation_a", b2Rot_GetAngle(a.q));
	put_vec(out, "anchor_b", b.p);
	put(out, "rotation_b", b2Rot_GetAngle(b.q));
	put_bool(out, "collide_connected", b2Joint_GetCollideConnected(joint));
	put(out, "force_threshold", b2Joint_GetForceThreshold(joint));
	put(out, "torque_threshold", b2Joint_GetTorqueThreshold(joint));
	float hertz = 0, damping = 0;
	b2Joint_GetConstraintTuning(joint, &hertz, &damping);
	put(out, "constraint_hertz", hertz);
	put(out, "constraint_damping_ratio", damping);
	put_vec(out, "constraint_force", b2Joint_GetConstraintForce(joint));
	put(out, "constraint_torque", b2Joint_GetConstraintTorque(joint));
	put(out, "linear_separation", b2Joint_GetLinearSeparation(joint));
	put(out, "angular_separation", b2Joint_GetAngularSeparation(joint));
	switch (b2Joint_GetType(joint)) {
		case b2_distanceJoint: {
			put(out, "length", b2DistanceJoint_GetLength(joint));
			put_bool(out, "enable_spring", b2DistanceJoint_IsSpringEnabled(joint));
			float lower = 0, upper = 0;
			b2DistanceJoint_GetSpringForceRange(joint, &lower, &upper);
			put(out, "lower_spring_force", lower);
			put(out, "upper_spring_force", upper);
			put(out, "hertz", b2DistanceJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b2DistanceJoint_GetSpringDampingRatio(joint));
			put_bool(out, "enable_limit", b2DistanceJoint_IsLimitEnabled(joint));
			put(out, "min_length", b2DistanceJoint_GetMinLength(joint));
			put(out, "max_length", b2DistanceJoint_GetMaxLength(joint));
			put_bool(out, "enable_motor", b2DistanceJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b2DistanceJoint_GetMotorSpeed(joint));
			put(out, "max_motor_force", b2DistanceJoint_GetMaxMotorForce(joint));
			put(out, "current_length", b2DistanceJoint_GetCurrentLength(joint));
			put(out, "motor_force", b2DistanceJoint_GetMotorForce(joint));
			break;
		}
		case b2_motorJoint:
			put_vec(out, "linear_velocity", b2MotorJoint_GetLinearVelocity(joint));
			put(out, "angular_velocity", b2MotorJoint_GetAngularVelocity(joint));
			put(out, "max_velocity_force", b2MotorJoint_GetMaxVelocityForce(joint));
			put(out, "max_velocity_torque", b2MotorJoint_GetMaxVelocityTorque(joint));
			put(out, "linear_hertz", b2MotorJoint_GetLinearHertz(joint));
			put(out, "linear_damping_ratio", b2MotorJoint_GetLinearDampingRatio(joint));
			put(out, "angular_hertz", b2MotorJoint_GetAngularHertz(joint));
			put(out, "angular_damping_ratio", b2MotorJoint_GetAngularDampingRatio(joint));
			put(out, "max_spring_force", b2MotorJoint_GetMaxSpringForce(joint));
			put(out, "max_spring_torque", b2MotorJoint_GetMaxSpringTorque(joint));
			break;
		case b2_prismaticJoint:
			put_bool(out, "enable_spring", b2PrismaticJoint_IsSpringEnabled(joint));
			put(out, "hertz", b2PrismaticJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b2PrismaticJoint_GetSpringDampingRatio(joint));
			put(out, "target_translation", b2PrismaticJoint_GetTargetTranslation(joint));
			put_bool(out, "enable_limit", b2PrismaticJoint_IsLimitEnabled(joint));
			put(out, "lower_translation", b2PrismaticJoint_GetLowerLimit(joint));
			put(out, "upper_translation", b2PrismaticJoint_GetUpperLimit(joint));
			put_bool(out, "enable_motor", b2PrismaticJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b2PrismaticJoint_GetMotorSpeed(joint));
			put(out, "max_motor_force", b2PrismaticJoint_GetMaxMotorForce(joint));
			put(out, "motor_force", b2PrismaticJoint_GetMotorForce(joint));
			put(out, "translation", b2PrismaticJoint_GetTranslation(joint));
			put(out, "speed", b2PrismaticJoint_GetSpeed(joint));
			break;
		case b2_revoluteJoint:
			put_bool(out, "enable_spring", b2RevoluteJoint_IsSpringEnabled(joint));
			put(out, "hertz", b2RevoluteJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b2RevoluteJoint_GetSpringDampingRatio(joint));
			put(out, "target_angle", b2RevoluteJoint_GetTargetAngle(joint));
			put_bool(out, "enable_limit", b2RevoluteJoint_IsLimitEnabled(joint));
			put(out, "lower_angle", b2RevoluteJoint_GetLowerLimit(joint));
			put(out, "upper_angle", b2RevoluteJoint_GetUpperLimit(joint));
			put_bool(out, "enable_motor", b2RevoluteJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b2RevoluteJoint_GetMotorSpeed(joint));
			put(out, "max_motor_torque", b2RevoluteJoint_GetMaxMotorTorque(joint));
			put(out, "motor_torque", b2RevoluteJoint_GetMotorTorque(joint));
			put(out, "angle", b2RevoluteJoint_GetAngle(joint));
			break;
		case b2_weldJoint:
			put(out, "linear_hertz", b2WeldJoint_GetLinearHertz(joint));
			put(out, "linear_damping_ratio", b2WeldJoint_GetLinearDampingRatio(joint));
			put(out, "angular_hertz", b2WeldJoint_GetAngularHertz(joint));
			put(out, "angular_damping_ratio", b2WeldJoint_GetAngularDampingRatio(joint));
			break;
		case b2_wheelJoint:
			put_bool(out, "enable_spring", b2WheelJoint_IsSpringEnabled(joint));
			put(out, "hertz", b2WheelJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b2WheelJoint_GetSpringDampingRatio(joint));
			put_bool(out, "enable_limit", b2WheelJoint_IsLimitEnabled(joint));
			put(out, "lower_translation", b2WheelJoint_GetLowerLimit(joint));
			put(out, "upper_translation", b2WheelJoint_GetUpperLimit(joint));
			put_bool(out, "enable_motor", b2WheelJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b2WheelJoint_GetMotorSpeed(joint));
			put(out, "max_motor_torque", b2WheelJoint_GetMaxMotorTorque(joint));
			put(out, "motor_torque", b2WheelJoint_GetMotorTorque(joint));
			break;
		default:
			break;
	}
}

#undef EGP2_ENUM
#undef EGP2_INFO

} // namespace egp::box2d
