// SPDX-License-Identifier: MIT
#include "egp_box3d_api.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace egp::box3d {
namespace {

// ---- Field tables -------------------------------------------------------------

#define EGP_WORLD_FIELDS(X)                                     \
	X(GRAVITY, "gravity", VEC3)                                 \
	X(RESTITUTION_THRESHOLD, "restitution_threshold", FLOAT)   \
	X(RESTITUTION_ITERATIONS, "restitution_iterations", INT)   \
	X(RESTITUTION_PROPAGATION, "restitution_propagation", BOOL) \
	X(HIT_EVENT_THRESHOLD, "hit_event_threshold", FLOAT)       \
	X(CONTACT_HERTZ, "contact_hertz", FLOAT)                   \
	X(CONTACT_DAMPING_RATIO, "contact_damping_ratio", FLOAT)   \
	X(CONTACT_SPEED, "contact_speed", FLOAT)                   \
	X(MAXIMUM_LINEAR_SPEED, "maximum_linear_speed", FLOAT)     \
	X(SLEEP, "sleep", BOOL)                                    \
	X(CONTINUOUS, "continuous", BOOL)                          \
	X(WARM_STARTING, "warm_starting", BOOL)                    \
	X(SPECULATIVE, "speculative", BOOL)                        \
	X(CONTACT_RECYCLE_DISTANCE, "contact_recycle_distance", FLOAT)

#define EGP_BODY_FIELDS(X)                            \
	X(TYPE, "type", INT)                              \
	X(POSITION, "position", VEC3)                     \
	X(ROTATION, "rotation", QUAT)                     \
	X(LINEAR_VELOCITY, "linear_velocity", VEC3)       \
	X(ANGULAR_VELOCITY, "angular_velocity", VEC3)     \
	X(LINEAR_DAMPING, "linear_damping", FLOAT)        \
	X(ANGULAR_DAMPING, "angular_damping", FLOAT)      \
	X(GRAVITY_SCALE, "gravity_scale", FLOAT)          \
	X(SLEEP_THRESHOLD, "sleep_threshold", FLOAT)      \
	X(SAFETY_FACTOR, "safety_factor", FLOAT)          \
	X(LOCK_LINEAR, "lock_linear", VEC3)               \
	X(LOCK_ANGULAR, "lock_angular", VEC3)             \
	X(SLEEP, "sleep", BOOL)                           \
	X(AWAKE, "awake", BOOL)                           \
	X(BULLET, "bullet", BOOL)                         \
	X(ENABLED, "enabled", BOOL)                       \
	X(FAST_ROTATION, "fast_rotation", BOOL)           \
	X(CONTACT_RECYCLING, "contact_recycling", BOOL)   \
	X(HIT_EVENTS, "hit_events", BOOL)                 \
	X(MASS, "mass", FLOAT)                            \
	X(CENTER_OF_MASS, "center_of_mass", VEC3)         \
	X(INERTIA, "inertia", VEC3)                       \
	X(TARGET_POSITION, "target_position", VEC3)       \
	X(TARGET_ROTATION, "target_rotation", QUAT)       \
	X(TARGET_TIME, "target_time", FLOAT)

#define EGP_SHAPE_FIELDS(X)                                     \
	X(FRICTION, "friction", FLOAT)                              \
	X(RESTITUTION, "restitution", FLOAT)                        \
	X(ROLLING_RESISTANCE, "rolling_resistance", FLOAT)          \
	X(TANGENT_VELOCITY, "tangent_velocity", VEC3)               \
	X(USER_MATERIAL, "user_material", U64)                      \
	X(DENSITY, "density", FLOAT)                                \
	X(EXPLOSION_SCALE, "explosion_scale", FLOAT)                \
	X(CATEGORY, "category", U64)                                \
	X(MASK, "mask", U64)                                        \
	X(GROUP, "group", INT)                                      \
	X(SENSOR, "sensor", BOOL)                                   \
	X(SENSOR_EVENTS, "sensor_events", BOOL)                     \
	X(CONTACT_EVENTS, "contact_events", BOOL)                   \
	X(HIT_EVENTS, "hit_events", BOOL)                           \
	X(PRESOLVE_EVENTS, "presolve_events", BOOL)                 \
	X(SPECULATIVE, "speculative", BOOL)                         \
	X(UPDATE_BODY_MASS, "update_body_mass", BOOL)               \
	X(INVOKE_CONTACT_CREATION, "invoke_contact_creation", BOOL)

#define EGP_JOINT_FIELDS(X)                                         \
	X(ANCHOR_A, "anchor_a", VEC3)                                   \
	X(ROTATION_A, "rotation_a", QUAT)                               \
	X(ANCHOR_B, "anchor_b", VEC3)                                   \
	X(ROTATION_B, "rotation_b", QUAT)                               \
	X(COLLIDE_CONNECTED, "collide_connected", BOOL)                 \
	X(FORCE_THRESHOLD, "force_threshold", FLOAT)                    \
	X(TORQUE_THRESHOLD, "torque_threshold", FLOAT)                  \
	X(CONSTRAINT_HERTZ, "constraint_hertz", FLOAT)                  \
	X(CONSTRAINT_DAMPING_RATIO, "constraint_damping_ratio", FLOAT)  \
	X(LENGTH, "length", FLOAT)                                      \
	X(ENABLE_SPRING, "enable_spring", BOOL)                         \
	X(LOWER_SPRING_FORCE, "lower_spring_force", FLOAT)              \
	X(UPPER_SPRING_FORCE, "upper_spring_force", FLOAT)              \
	X(HERTZ, "hertz", FLOAT)                                        \
	X(DAMPING_RATIO, "damping_ratio", FLOAT)                        \
	X(ENABLE_LIMIT, "enable_limit", BOOL)                           \
	X(MIN_LENGTH, "min_length", FLOAT)                              \
	X(MAX_LENGTH, "max_length", FLOAT)                              \
	X(ENABLE_MOTOR, "enable_motor", BOOL)                           \
	X(MAX_MOTOR_FORCE, "max_motor_force", FLOAT)                    \
	X(MOTOR_SPEED, "motor_speed", FLOAT)                            \
	X(LINEAR_VELOCITY, "linear_velocity", VEC3)                     \
	X(MAX_VELOCITY_FORCE, "max_velocity_force", FLOAT)              \
	X(ANGULAR_VELOCITY, "angular_velocity", VEC3)                   \
	X(MAX_VELOCITY_TORQUE, "max_velocity_torque", FLOAT)            \
	X(LINEAR_HERTZ, "linear_hertz", FLOAT)                          \
	X(LINEAR_DAMPING_RATIO, "linear_damping_ratio", FLOAT)          \
	X(MAX_SPRING_FORCE, "max_spring_force", FLOAT)                  \
	X(ANGULAR_HERTZ, "angular_hertz", FLOAT)                        \
	X(ANGULAR_DAMPING_RATIO, "angular_damping_ratio", FLOAT)        \
	X(MAX_SPRING_TORQUE, "max_spring_torque", FLOAT)                \
	X(TARGET_TRANSLATION, "target_translation", FLOAT)              \
	X(LOWER_TRANSLATION, "lower_translation", FLOAT)                \
	X(UPPER_TRANSLATION, "upper_translation", FLOAT)                \
	X(TARGET_ANGLE, "target_angle", FLOAT)                          \
	X(LOWER_ANGLE, "lower_angle", FLOAT)                            \
	X(UPPER_ANGLE, "upper_angle", FLOAT)                            \
	X(MAX_MOTOR_TORQUE, "max_motor_torque", FLOAT)                  \
	X(TARGET_ROTATION, "target_rotation", QUAT)                     \
	X(ENABLE_CONE_LIMIT, "enable_cone_limit", BOOL)                 \
	X(CONE_ANGLE, "cone_angle", FLOAT)                              \
	X(ENABLE_TWIST_LIMIT, "enable_twist_limit", BOOL)               \
	X(LOWER_TWIST_ANGLE, "lower_twist_angle", FLOAT)                \
	X(UPPER_TWIST_ANGLE, "upper_twist_angle", FLOAT)                \
	X(MOTOR_VELOCITY, "motor_velocity", VEC3)                       \
	X(ENABLE_SUSPENSION_SPRING, "enable_suspension_spring", BOOL)   \
	X(SUSPENSION_HERTZ, "suspension_hertz", FLOAT)                  \
	X(SUSPENSION_DAMPING_RATIO, "suspension_damping_ratio", FLOAT)  \
	X(ENABLE_SUSPENSION_LIMIT, "enable_suspension_limit", BOOL)     \
	X(LOWER_SUSPENSION_LIMIT, "lower_suspension_limit", FLOAT)      \
	X(UPPER_SUSPENSION_LIMIT, "upper_suspension_limit", FLOAT)      \
	X(ENABLE_SPIN_MOTOR, "enable_spin_motor", BOOL)                 \
	X(MAX_SPIN_TORQUE, "max_spin_torque", FLOAT)                    \
	X(SPIN_SPEED, "spin_speed", FLOAT)                              \
	X(ENABLE_STEERING, "enable_steering", BOOL)                     \
	X(STEERING_HERTZ, "steering_hertz", FLOAT)                      \
	X(STEERING_DAMPING_RATIO, "steering_damping_ratio", FLOAT)      \
	X(TARGET_STEERING_ANGLE, "target_steering_angle", FLOAT)        \
	X(MAX_STEERING_TORQUE, "max_steering_torque", FLOAT)            \
	X(ENABLE_STEERING_LIMIT, "enable_steering_limit", BOOL)         \
	X(LOWER_STEERING_LIMIT, "lower_steering_limit", FLOAT)          \
	X(UPPER_STEERING_LIMIT, "upper_steering_limit", FLOAT)          \
	X(MAX_TORQUE, "max_torque", FLOAT)                              	EGP_GENERIC_FIELDS(X)

// EGP Generic6DOF: per-axis (x, y, z) values in PhysicsServer3D parameter order; the
// parameter index is the field id minus GENERIC_FIRST.
#define EGP_GENERIC_FIELDS(X)                                                   	X(LINEAR_LOWER_LIMIT, "linear_lower_limit", VEC3)                           	X(LINEAR_UPPER_LIMIT, "linear_upper_limit", VEC3)                           	X(LINEAR_LIMIT_SOFTNESS, "linear_limit_softness", VEC3)                     	X(LINEAR_RESTITUTION, "linear_restitution", VEC3)                           	X(LINEAR_DAMPING, "linear_damping", VEC3)                                   	X(LINEAR_MOTOR_TARGET_VELOCITY, "linear_motor_target_velocity", VEC3)       	X(LINEAR_MOTOR_FORCE_LIMIT, "linear_motor_force_limit", VEC3)               	X(LINEAR_SPRING_STIFFNESS, "linear_spring_stiffness", VEC3)                 	X(LINEAR_SPRING_DAMPING, "linear_spring_damping", VEC3)                     	X(LINEAR_SPRING_EQUILIBRIUM, "linear_spring_equilibrium", VEC3)             	X(ANGULAR_LOWER_LIMIT, "angular_lower_limit", VEC3)                         	X(ANGULAR_UPPER_LIMIT, "angular_upper_limit", VEC3)                         	X(ANGULAR_LIMIT_SOFTNESS, "angular_limit_softness", VEC3)                   	X(ANGULAR_DAMPING, "angular_damping", VEC3)                                 	X(ANGULAR_RESTITUTION, "angular_restitution", VEC3)                         	X(ANGULAR_FORCE_LIMIT, "angular_force_limit", VEC3)                         	X(ANGULAR_ERP, "angular_erp", VEC3)                                         	X(ANGULAR_MOTOR_TARGET_VELOCITY, "angular_motor_target_velocity", VEC3)     	X(ANGULAR_MOTOR_FORCE_LIMIT, "angular_motor_force_limit", VEC3)             	X(ANGULAR_SPRING_STIFFNESS, "angular_spring_stiffness", VEC3)               	X(ANGULAR_SPRING_DAMPING, "angular_spring_damping", VEC3)                   	X(ANGULAR_SPRING_EQUILIBRIUM, "angular_spring_equilibrium", VEC3)           	X(LINEAR_SPRING_MAX_FORCE, "linear_spring_max_force", VEC3)                 	X(ANGULAR_SPRING_MAX_TORQUE, "angular_spring_max_torque", VEC3)             	X(ENABLE_LINEAR_LIMIT, "enable_linear_limit", VEC3)                         	X(ENABLE_ANGULAR_LIMIT, "enable_angular_limit", VEC3)                       	X(ENABLE_ANGULAR_SPRING, "enable_angular_spring", VEC3)                     	X(ENABLE_LINEAR_SPRING, "enable_linear_spring", VEC3)                       	X(ENABLE_ANGULAR_MOTOR, "enable_angular_motor", VEC3)                       	X(ENABLE_LINEAR_MOTOR, "enable_linear_motor", VEC3)

#define EGP_ENUM(id, name, kind) id,
#define EGP_INFO(id, name, kind) { name, uint16_t(id), FieldKind::kind },
namespace world_field {
enum : uint16_t { EGP_WORLD_FIELDS(EGP_ENUM) COUNT };
}
namespace body_field {
enum : uint16_t { EGP_BODY_FIELDS(EGP_ENUM) COUNT };
}
namespace shape_field {
enum : uint16_t { EGP_SHAPE_FIELDS(EGP_ENUM) COUNT };
}
namespace joint_field {
enum : uint16_t { EGP_JOINT_FIELDS(EGP_ENUM) COUNT };
}

const std::vector<FieldInfo> &table(FieldSet set) {
	static const std::vector<FieldInfo> world = [] { using namespace world_field; return std::vector<FieldInfo>{ EGP_WORLD_FIELDS(EGP_INFO) }; }();
	static const std::vector<FieldInfo> body = [] { using namespace body_field; return std::vector<FieldInfo>{ EGP_BODY_FIELDS(EGP_INFO) }; }();
	static const std::vector<FieldInfo> shape = [] { using namespace shape_field; return std::vector<FieldInfo>{ EGP_SHAPE_FIELDS(EGP_INFO) }; }();
	static const std::vector<FieldInfo> joint = [] { using namespace joint_field; return std::vector<FieldInfo>{ EGP_JOINT_FIELDS(EGP_INFO) }; }();
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

constexpr uint16_t GENERIC_FIRST = joint_field::LINEAR_LOWER_LIMIT;
constexpr uint16_t GENERIC_FLAGS = joint_field::ENABLE_LINEAR_LIMIT;

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
	b3Vec3 v3(uint16_t id, b3Vec3 fallback) const {
		const Prop *p = find(id);
		return p ? b3Vec3{ float(p->v[0]), float(p->v[1]), float(p->v[2]) } : fallback;
	}
	b3Quat q(uint16_t id, b3Quat fallback) const {
		const Prop *p = find(id);
		return p ? b3Quat{ { float(p->v[0]), float(p->v[1]), float(p->v[2]) }, float(p->v[3]) } : fallback;
	}
};

b3MotionLocks locks(const View &v, b3MotionLocks current) {
	if (const Prop *p = v.find(body_field::LOCK_LINEAR)) {
		current.linearX = p->v[0] != 0.0;
		current.linearY = p->v[1] != 0.0;
		current.linearZ = p->v[2] != 0.0;
	}
	if (const Prop *p = v.find(body_field::LOCK_ANGULAR)) {
		current.angularX = p->v[0] != 0.0;
		current.angularY = p->v[1] != 0.0;
		current.angularZ = p->v[2] != 0.0;
	}
	return current;
}

b3SurfaceMaterial material(const View &v, b3SurfaceMaterial m) {
	using namespace shape_field;
	m.friction = v.f(FRICTION, m.friction);
	m.restitution = v.f(RESTITUTION, m.restitution);
	m.rollingResistance = v.f(ROLLING_RESISTANCE, m.rollingResistance);
	m.tangentVelocity = v.v3(TANGENT_VELOCITY, m.tangentVelocity);
	m.userMaterialId = v.u(USER_MATERIAL, m.userMaterialId);
	return m;
}

b3Filter filter(const View &v, b3Filter f) {
	using namespace shape_field;
	f.categoryBits = v.u(CATEGORY, f.categoryBits);
	f.maskBits = v.u(MASK, f.maskBits);
	f.groupIndex = v.i(GROUP, f.groupIndex);
	return f;
}

void apply_base(b3JointDef &base, const View &v) {
	using namespace joint_field;
	base.localFrameA = { v.v3(ANCHOR_A, base.localFrameA.p), v.q(ROTATION_A, base.localFrameA.q) };
	base.localFrameB = { v.v3(ANCHOR_B, base.localFrameB.p), v.q(ROTATION_B, base.localFrameB.q) };
	base.collideConnected = v.b(COLLIDE_CONNECTED, base.collideConnected);
	base.forceThreshold = v.f(FORCE_THRESHOLD, base.forceThreshold);
	base.torqueThreshold = v.f(TORQUE_THRESHOLD, base.torqueThreshold);
	base.constraintHertz = v.f(CONSTRAINT_HERTZ, base.constraintHertz);
	base.constraintDampingRatio = v.f(CONSTRAINT_DAMPING_RATIO, base.constraintDampingRatio);
}

void apply_generic(b3JointId joint, const View &v) {
	for (uint16_t id = GENERIC_FIRST; id < GENERIC_FLAGS; ++id) {
		if (const Prop *p = v.find(id)) {
			for (int axis = 0; axis < 3; ++axis) {
				b3GenericJoint_SetParam(joint, axis, int(id - GENERIC_FIRST), float(p->v[axis]));
			}
		}
	}
	for (uint16_t id = GENERIC_FLAGS; id < joint_field::COUNT; ++id) {
		if (const Prop *p = v.find(id)) {
			for (int axis = 0; axis < 3; ++axis) {
				b3GenericJoint_SetFlag(joint, axis, int(id - GENERIC_FLAGS), p->v[axis] != 0.0);
			}
		}
	}
	if (const Prop *p = v.find(joint_field::TARGET_ROTATION)) {
		b3GenericJoint_SetTargetRotation(joint, { { float(p->v[0]), float(p->v[1]), float(p->v[2]) }, float(p->v[3]) });
	}
}

int component_count(FieldKind kind) {
	return kind == FieldKind::VEC3 ? 3 : kind == FieldKind::QUAT ? 4 :
																   1;
}

bool finite_prop(const FieldInfo &info, const Prop &p) {
	for (int k = 0; k < component_count(info.kind); ++k) {
		if (!std::isfinite(p.v[k]) || std::fabs(p.v[k]) > 3.0e38) {
			return false;
		}
	}
	if (info.kind == FieldKind::QUAT) {
		const double n = p.v[0] * p.v[0] + p.v[1] * p.v[1] + p.v[2] * p.v[2] + p.v[3] * p.v[3];
		return std::fabs(n - 1.0) < 1.0e-4;
	}
	return true;
}

bool unit(b3Quat q) {
	const float n = q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z + q.s * q.s;
	return std::isfinite(n) && std::fabs(n - 1.0f) < 1.0e-4f;
}

bool finite3(b3Vec3 v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

b3Vec3 place(const Geometry &g, b3Vec3 local) {
	return b3Add(g.center, b3RotateVector(g.rotation, local));
}

// Readback helpers.
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
void put_vec(Values &out, const char *name, b3Vec3 value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::VEC3;
	v.v[0] = value.x;
	v.v[1] = value.y;
	v.v[2] = value.z;
	out.push_back(v);
}
void put_quat(Values &out, const char *name, b3Quat value) {
	Value v;
	v.name = name;
	v.kind = FieldKind::QUAT;
	v.v[0] = value.v.x;
	v.v[1] = value.v.y;
	v.v[2] = value.v.z;
	v.v[3] = value.s;
	out.push_back(v);
}

const char *const JOINT_NAMES[JOINT_TYPE_COUNT] = { "distance", "spherical", "prismatic", "motor", "revolute", "weld", "wheel", "filter", "parallel", "generic" };
const char *const SHAPE_NAMES[] = { "sphere", "capsule", "box", "hull", "mesh", "height_field", "compound" };
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

namespace {
enum class Rule : uint8_t { ANY,
	NONNEGATIVE,
	POSITIVE,
	ANGLE,
	CONE };

// Ranges Box3D asserts on. Validated before any mutation so a bad command is rejected
// instead of reaching the solver.
Rule rule(FieldSet set, uint16_t id) {
	switch (set) {
		case FieldSet::WORLD:
			switch (id) {
				case world_field::RESTITUTION_THRESHOLD:
				case world_field::HIT_EVENT_THRESHOLD:
				case world_field::CONTACT_HERTZ:
				case world_field::CONTACT_DAMPING_RATIO:
				case world_field::CONTACT_SPEED:
				case world_field::CONTACT_RECYCLE_DISTANCE:
					return Rule::NONNEGATIVE;
				case world_field::MAXIMUM_LINEAR_SPEED:
					return Rule::POSITIVE;
				default:
					return Rule::ANY;
			}
		case FieldSet::BODY:
			switch (id) {
				case body_field::LINEAR_DAMPING:
				case body_field::ANGULAR_DAMPING:
				case body_field::SLEEP_THRESHOLD:
				case body_field::SAFETY_FACTOR:
				case body_field::MASS:
				case body_field::INERTIA:
					return Rule::NONNEGATIVE;
				case body_field::TARGET_TIME:
					return Rule::POSITIVE;
				default:
					return Rule::ANY;
			}
		case FieldSet::SHAPE:
			switch (id) {
				case shape_field::FRICTION:
				case shape_field::RESTITUTION:
				case shape_field::ROLLING_RESISTANCE:
				case shape_field::DENSITY:
				case shape_field::EXPLOSION_SCALE:
					return Rule::NONNEGATIVE;
				default:
					return Rule::ANY;
			}
		case FieldSet::JOINT:
			switch (id) {
				case joint_field::FORCE_THRESHOLD:
				case joint_field::TORQUE_THRESHOLD:
				case joint_field::CONSTRAINT_HERTZ:
				case joint_field::CONSTRAINT_DAMPING_RATIO:
				case joint_field::HERTZ:
				case joint_field::DAMPING_RATIO:
				case joint_field::MAX_MOTOR_FORCE:
				case joint_field::MAX_MOTOR_TORQUE:
				case joint_field::MAX_VELOCITY_FORCE:
				case joint_field::MAX_VELOCITY_TORQUE:
				case joint_field::LINEAR_HERTZ:
				case joint_field::LINEAR_DAMPING_RATIO:
				case joint_field::ANGULAR_HERTZ:
				case joint_field::ANGULAR_DAMPING_RATIO:
				case joint_field::MAX_SPRING_FORCE:
				case joint_field::MAX_SPRING_TORQUE:
				case joint_field::SUSPENSION_HERTZ:
				case joint_field::SUSPENSION_DAMPING_RATIO:
				case joint_field::MAX_SPIN_TORQUE:
				case joint_field::STEERING_HERTZ:
				case joint_field::STEERING_DAMPING_RATIO:
				case joint_field::MAX_STEERING_TORQUE:
				case joint_field::MAX_TORQUE:
					return Rule::NONNEGATIVE;
				case joint_field::LENGTH:
					return Rule::POSITIVE;
				case joint_field::TARGET_ANGLE:
					return Rule::ANGLE;
				case joint_field::CONE_ANGLE:
					return Rule::CONE;
				default:
					return Rule::ANY;
			}
	}
	return Rule::ANY;
}

bool obeys(Rule r, const FieldInfo &info, const Prop &p) {
	const int count = component_count(info.kind);
	for (int k = 0; k < count; ++k) {
		const double v = p.v[k];
		switch (r) {
			case Rule::NONNEGATIVE:
				if (v < 0.0) {
					return false;
				}
				break;
			case Rule::POSITIVE:
				if (!(v > 0.0)) {
					return false;
				}
				break;
			case Rule::ANGLE:
				if (v < -3.14159265358979 || v > 3.14159265358979) {
					return false;
				}
				break;
			case Rule::CONE:
				if (v < 0.0 || v > 3.14159265358979) {
					return false;
				}
				break;
			case Rule::ANY:
				break;
		}
	}
	return true;
}

struct LimitPair {
	uint16_t lower, upper;
	const char *lower_name, *upper_name;
};
const LimitPair LIMIT_PAIRS[] = {
	{ joint_field::LOWER_SPRING_FORCE, joint_field::UPPER_SPRING_FORCE, "lower_spring_force", "upper_spring_force" },
	{ joint_field::MIN_LENGTH, joint_field::MAX_LENGTH, "min_length", "max_length" },
	{ joint_field::LOWER_TRANSLATION, joint_field::UPPER_TRANSLATION, "lower_translation", "upper_translation" },
	{ joint_field::LOWER_ANGLE, joint_field::UPPER_ANGLE, "lower_angle", "upper_angle" },
	{ joint_field::LOWER_TWIST_ANGLE, joint_field::UPPER_TWIST_ANGLE, "lower_twist_angle", "upper_twist_angle" },
	{ joint_field::LOWER_SUSPENSION_LIMIT, joint_field::UPPER_SUSPENSION_LIMIT, "lower_suspension_limit", "upper_suspension_limit" },
	{ joint_field::LOWER_STEERING_LIMIT, joint_field::UPPER_STEERING_LIMIT, "lower_steering_limit", "upper_steering_limit" },
};

double value_of(const Values &values, const char *name, double fallback) {
	for (const Value &v : values) {
		if (std::strcmp(v.name, name) == 0) {
			return v.v[0];
		}
	}
	return fallback;
}

// The type's default limits, named like the readback.
void default_limits(JointType type, Values &out) {
	out.clear();
	auto put_pair = [&](const char *lower, float lv, const char *upper, float uv) {
		Value a;
		a.name = lower;
		a.v[0] = lv;
		out.push_back(a);
		Value b;
		b.name = upper;
		b.v[0] = uv;
		out.push_back(b);
	};
	switch (type) {
		case JointType::DISTANCE: {
			const b3DistanceJointDef def = b3DefaultDistanceJointDef();
			put_pair("lower_spring_force", def.lowerSpringForce, "upper_spring_force", def.upperSpringForce);
			put_pair("min_length", def.minLength, "max_length", def.maxLength);
			break;
		}
		case JointType::PRISMATIC: {
			const b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
			put_pair("lower_translation", def.lowerTranslation, "upper_translation", def.upperTranslation);
			break;
		}
		case JointType::REVOLUTE: {
			const b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
			put_pair("lower_angle", def.lowerAngle, "upper_angle", def.upperAngle);
			break;
		}
		case JointType::SPHERICAL: {
			const b3SphericalJointDef def = b3DefaultSphericalJointDef();
			put_pair("lower_twist_angle", def.lowerTwistAngle, "upper_twist_angle", def.upperTwistAngle);
			break;
		}
		case JointType::WHEEL: {
			const b3WheelJointDef def = b3DefaultWheelJointDef();
			put_pair("lower_suspension_limit", def.lowerSuspensionLimit, "upper_suspension_limit", def.upperSuspensionLimit);
			put_pair("lower_steering_limit", def.lowerSteeringLimit, "upper_steering_limit", def.upperSteeringLimit);
			break;
		}
		default:
			break;
	}
}
} // namespace

bool validate_props(FieldSet set, const Props &props) {
	const auto &fields = table(set);
	if (props.size() > 4 * fields.size()) {
		return false;
	}
	for (const Prop &p : props) {
		if (p.id >= fields.size() || !finite_prop(fields[p.id], p) || !obeys(rule(set, p.id), fields[p.id], p)) {
			return false;
		}
		if (set == FieldSet::BODY && p.id == body_field::TYPE && (p.v[0] < 0 || p.v[0] > 2)) {
			return false;
		}
		if (set == FieldSet::WORLD && p.id == world_field::RESTITUTION_ITERATIONS && (p.v[0] < 0 || p.v[0] > 64)) {
			return false;
		}
	}
	// Generic joint parameters address three axes; their values are free.
	return true;
}

bool joint_limits_ordered(JointType type, const b3JointId *existing, const Props &props) {
	Values current;
	if (existing) {
		read_joint(*existing, current);
	} else {
		default_limits(type, current);
	}
	const View v(props);
	for (const LimitPair &pair : LIMIT_PAIRS) {
		if (!v.has(pair.lower) && !v.has(pair.upper)) {
			continue;
		}
		const double lower = v.has(pair.lower) ? v.find(pair.lower)->v[0] : value_of(current, pair.lower_name, -3.0e38);
		const double upper = v.has(pair.upper) ? v.find(pair.upper)->v[0] : value_of(current, pair.upper_name, 3.0e38);
		if (lower > upper) {
			return false;
		}
	}
	return true;
}

bool props_body_type(const Props &props, int &type) {
	const View v(props);
	if (!v.has(body_field::TYPE)) {
		return false;
	}
	type = v.i(body_field::TYPE, 0);
	return true;
}

bool props_sensor(const Props &props) {
	return View(props).b(shape_field::SENSOR, false);
}

bool props_material(const Props &props) {
	const View v(props);
	return v.has(shape_field::FRICTION) || v.has(shape_field::RESTITUTION) || v.has(shape_field::ROLLING_RESISTANCE) || v.has(shape_field::TANGENT_VELOCITY) || v.has(shape_field::USER_MATERIAL);
}

b3SurfaceMaterial material_from(const Props &props, b3SurfaceMaterial base) {
	return material(View(props), base);
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

b3JointType box3d_joint_type(JointType type) {
	switch (type) {
		case JointType::DISTANCE:
			return b3_distanceJoint;
		case JointType::SPHERICAL:
			return b3_sphericalJoint;
		case JointType::PRISMATIC:
			return b3_prismaticJoint;
		case JointType::MOTOR:
			return b3_motorJoint;
		case JointType::REVOLUTE:
			return b3_revoluteJoint;
		case JointType::WELD:
			return b3_weldJoint;
		case JointType::WHEEL:
			return b3_wheelJoint;
		case JointType::FILTER:
			return b3_filterJoint;
		case JointType::PARALLEL:
			return b3_parallelJoint;
		case JointType::GENERIC:
			return b3_genericJoint;
	}
	return b3_filterJoint;
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

bool valid_material(const b3SurfaceMaterial &m) {
	return std::isfinite(m.friction) && m.friction >= 0.0f && std::isfinite(m.restitution) && m.restitution >= 0.0f &&
			std::isfinite(m.rollingResistance) && m.rollingResistance >= 0.0f && finite3(m.tangentVelocity);
}

bool static_only(ShapeType type) {
	return type == ShapeType::HEIGHT_FIELD || type == ShapeType::COMPOUND;
}

namespace {
bool valid_material_table(const Geometry &g, size_t elements) {
	if (g.extra_materials.size() > 254) {
		return false;
	}
	for (const b3SurfaceMaterial &m : g.extra_materials) {
		if (!valid_material(m)) {
			return false;
		}
	}
	if (g.material_indices.empty()) {
		return true;
	}
	if (g.material_indices.size() != elements) {
		return false;
	}
	for (uint8_t index : g.material_indices) {
		if (index > g.extra_materials.size()) {
			return false;
		}
	}
	return true;
}

// Shape material table: the shape's own material, then the extra materials.
std::vector<b3SurfaceMaterial> material_table(const b3ShapeDef &def, const Geometry &g) {
	std::vector<b3SurfaceMaterial> table;
	table.reserve(g.extra_materials.size() + 1);
	table.push_back(def.baseMaterial);
	table.insert(table.end(), g.extra_materials.begin(), g.extra_materials.end());
	return table;
}

b3MeshData *make_mesh(const Geometry &g, bool bake_placement) {
	std::vector<b3Vec3> vertices(g.points.size());
	for (size_t i = 0; i < g.points.size(); ++i) {
		vertices[i] = bake_placement ? place(g, g.points[i]) : g.points[i];
	}
	std::vector<uint8_t> indices(g.material_indices);
	b3MeshDef mesh_def = {};
	mesh_def.vertices = vertices.data();
	mesh_def.stride = sizeof(b3Vec3);
	mesh_def.indices = const_cast<int32_t *>(g.indices.data());
	mesh_def.materialIndices = indices.empty() ? nullptr : indices.data();
	mesh_def.vertexCount = int(vertices.size());
	mesh_def.triangleCount = int(g.indices.size() / 3);
	return b3CreateMesh(&mesh_def, nullptr, 0);
}
} // namespace

bool valid_geometry(const Geometry &g) {
	if (!finite3(g.center) || !finite3(g.center1) || !finite3(g.center2) || !finite3(g.half_extents) || !finite3(g.scale) || !std::isfinite(g.radius) || !unit(g.rotation) || !valid_material(g.material)) {
		return false;
	}
	for (const b3Vec3 &point : g.points) {
		if (!finite3(point)) {
			return false;
		}
	}
	switch (g.type) {
		case ShapeType::SPHERE:
			return g.radius > 0.0f;
		case ShapeType::CAPSULE:
			return g.radius > 0.0f;
		case ShapeType::BOX:
			return g.half_extents.x > 0.0f && g.half_extents.y > 0.0f && g.half_extents.z > 0.0f;
		case ShapeType::HULL:
			return g.points.size() >= 4 && g.points.size() <= size_t(B3_MAX_HULL_VERTICES) && g.scale.x > 0.0f && g.scale.y > 0.0f && g.scale.z > 0.0f;
		case ShapeType::MESH:
			if (g.points.size() < 3 || g.points.size() > (1u << 20) || g.indices.size() < 3 || g.indices.size() % 3 != 0 || g.indices.size() > (3u << 20) || g.scale.x == 0.0f || g.scale.y == 0.0f || g.scale.z == 0.0f) {
				return false;
			}
			for (int32_t index : g.indices) {
				if (index < 0 || size_t(index) >= g.points.size()) {
					return false;
				}
			}
			return valid_material_table(g, g.indices.size() / 3);
		case ShapeType::HEIGHT_FIELD:
			if (g.count_x < 2 || g.count_z < 2 || g.count_x > 4096 || g.count_z > 4096 || g.heights.size() != size_t(g.count_x) * size_t(g.count_z) || !(g.scale.x > 0.0f && g.scale.y > 0.0f && g.scale.z > 0.0f)) {
				return false;
			}
			if (!g.holes.empty() && g.holes.size() != size_t(g.count_x - 1) * size_t(g.count_z - 1)) {
				return false;
			}
			for (float height : g.heights) {
				if (!std::isfinite(height)) {
					return false;
				}
			}
			return valid_material_table(g, size_t(g.count_x - 1) * size_t(g.count_z - 1));
		case ShapeType::COMPOUND:
			if (g.children.empty() || g.children.size() > MAX_COMPOUND_CHILDREN) {
				return false;
			}
			for (const Geometry &child : g.children) {
				if (child.type == ShapeType::HEIGHT_FIELD || child.type == ShapeType::COMPOUND || !valid_geometry(child)) {
					return false;
				}
			}
			return true;
	}
	return false;
}

void GeometryStore::clear() {
	for (b3MeshData *mesh : meshes) {
		b3DestroyMesh(mesh);
	}
	for (b3HeightFieldData *field : height_fields) {
		b3DestroyHeightField(field);
	}
	for (b3CompoundData *compound : compounds) {
		b3DestroyCompound(compound);
	}
	meshes.clear();
	height_fields.clear();
	compounds.clear();
}

b3ShapeId GeometryStore::create_shape(b3BodyId body, b3ShapeDef def, const Geometry &g) {
	switch (g.type) {
		case ShapeType::SPHERE: {
			const b3Sphere sphere = { g.center, g.radius };
			return b3CreateSphereShape(body, &def, &sphere);
		}
		case ShapeType::CAPSULE: {
			const b3Capsule capsule = { place(g, g.center1), place(g, g.center2), g.radius };
			return b3CreateCapsuleShape(body, &def, &capsule);
		}
		case ShapeType::BOX: {
			b3BoxHull box = b3MakeTransformedBoxHull(g.half_extents.x, g.half_extents.y, g.half_extents.z, { g.center, g.rotation });
			return b3CreateHullShape(body, &def, &box.base);
		}
		case ShapeType::HULL: {
			b3HullData *hull = b3CreateHull(g.points.data(), int(g.points.size()), B3_MAX_HULL_VERTICES);
			if (!hull) {
				return b3_nullShapeId;
			}
			// Box3D copies hulls into the world's hull database.
			const b3ShapeId shape = b3CreateTransformedHullShape(body, &def, hull, { g.center, g.rotation }, g.scale);
			b3DestroyHull(hull);
			return shape;
		}
		case ShapeType::MESH: {
			// Meshes take no shape transform: bake the offset and rotation into the vertices.
			b3MeshData *mesh = make_mesh(g, true);
			if (!mesh) {
				return b3_nullShapeId;
			}
			meshes.push_back(mesh);
			const std::vector<b3SurfaceMaterial> table = material_table(def, g);
			def.materials = const_cast<b3SurfaceMaterial *>(table.data());
			def.materialCount = int(table.size());
			return b3CreateMeshShape(body, &def, mesh, g.scale);
		}
		case ShapeType::HEIGHT_FIELD: {
			std::vector<float> heights(g.heights);
			std::vector<uint8_t> materials(g.material_indices);
			if (!g.holes.empty()) {
				materials.resize(g.holes.size(), 0);
				for (size_t i = 0; i < g.holes.size(); ++i) {
					if (g.holes[i]) {
						materials[i] = B3_HEIGHT_FIELD_HOLE;
					}
				}
			}
			const auto range = std::minmax_element(heights.begin(), heights.end());
			b3HeightFieldDef field_def = {};
			field_def.heights = heights.data();
			field_def.materialIndices = materials.empty() ? nullptr : materials.data();
			field_def.scale = g.scale;
			field_def.countX = g.count_x;
			field_def.countZ = g.count_z;
			field_def.globalMinimumHeight = *range.first;
			field_def.globalMaximumHeight = *range.second;
			b3HeightFieldData *field = b3CreateHeightField(&field_def);
			if (!field) {
				return b3_nullShapeId;
			}
			height_fields.push_back(field);
			const std::vector<b3SurfaceMaterial> table = material_table(def, g);
			def.materials = const_cast<b3SurfaceMaterial *>(table.data());
			def.materialCount = int(table.size());
			return b3CreateHeightFieldShape(body, &def, field);
		}
		case ShapeType::COMPOUND: {
			// Children are cloned into the baked compound; the temporary hulls, boxes and
			// meshes are released once it is built.
			std::vector<b3CompoundSphereDef> spheres;
			std::vector<b3CompoundCapsuleDef> capsules;
			std::vector<b3CompoundHullDef> hulls;
			std::vector<b3CompoundMeshDef> mesh_defs;
			std::vector<b3HullData *> owned_hulls;
			std::vector<b3BoxHull> boxes;
			std::vector<b3MeshData *> owned_meshes;
			std::vector<std::vector<b3SurfaceMaterial>> tables;
			size_t box_count = 0;
			for (const Geometry &child : g.children) {
				box_count += child.type == ShapeType::BOX ? 1 : 0;
			}
			boxes.reserve(box_count);
			tables.reserve(g.children.size());
			bool built = true;
			for (const Geometry &child : g.children) {
				switch (child.type) {
					case ShapeType::SPHERE:
						spheres.push_back({ { child.center, child.radius }, child.material });
						break;
					case ShapeType::CAPSULE:
						capsules.push_back({ { place(child, child.center1), place(child, child.center2), child.radius }, child.material });
						break;
					case ShapeType::BOX:
						boxes.push_back(b3MakeBoxHull(child.half_extents.x, child.half_extents.y, child.half_extents.z));
						hulls.push_back({ &boxes.back().base, { child.center, child.rotation }, child.material });
						break;
					case ShapeType::HULL: {
						std::vector<b3Vec3> scaled(child.points.size());
						for (size_t i = 0; i < child.points.size(); ++i) {
							scaled[i] = { child.points[i].x * child.scale.x, child.points[i].y * child.scale.y, child.points[i].z * child.scale.z };
						}
						b3HullData *hull = b3CreateHull(scaled.data(), int(scaled.size()), B3_MAX_HULL_VERTICES);
						if (!hull) {
							built = false;
							break;
						}
						owned_hulls.push_back(hull);
						hulls.push_back({ hull, { child.center, child.rotation }, child.material });
						break;
					}
					case ShapeType::MESH: {
						b3MeshData *mesh = make_mesh(child, false);
						if (!mesh) {
							built = false;
							break;
						}
						owned_meshes.push_back(mesh);
						tables.push_back({ child.material });
						tables.back().insert(tables.back().end(), child.extra_materials.begin(), child.extra_materials.end());
						mesh_defs.push_back({ mesh, { child.center, child.rotation }, child.scale, tables.back().data(), int(tables.back().size()) });
						break;
					}
					default:
						built = false;
						break;
				}
			}
			b3CompoundData *compound = nullptr;
			if (built) {
				b3CompoundDef compound_def = {};
				compound_def.spheres = spheres.data();
				compound_def.sphereCount = int(spheres.size());
				compound_def.capsules = capsules.data();
				compound_def.capsuleCount = int(capsules.size());
				compound_def.hulls = hulls.data();
				compound_def.hullCount = int(hulls.size());
				compound_def.meshes = mesh_defs.data();
				compound_def.meshCount = int(mesh_defs.size());
				compound = b3CreateCompound(&compound_def);
			}
			for (b3HullData *hull : owned_hulls) {
				b3DestroyHull(hull);
			}
			for (b3MeshData *mesh : owned_meshes) {
				b3DestroyMesh(mesh);
			}
			if (!compound) {
				return b3_nullShapeId;
			}
			compounds.push_back(compound);
			return b3CreateBakedCompoundShape(body, &def, compound);
		}
	}
	return b3_nullShapeId;
}

bool make_proxy(const Geometry &g, b3Quat rotation, std::vector<b3Vec3> &points, b3ShapeProxy &proxy) {
	points.clear();
	float radius = 0.0f;
	switch (g.type) {
		case ShapeType::SPHERE:
			points.push_back(g.center);
			radius = g.radius;
			break;
		case ShapeType::CAPSULE:
			points.push_back(place(g, g.center1));
			points.push_back(place(g, g.center2));
			radius = g.radius;
			break;
		case ShapeType::BOX:
			for (int corner = 0; corner < 8; ++corner) {
				const b3Vec3 local = { corner & 1 ? g.half_extents.x : -g.half_extents.x, corner & 2 ? g.half_extents.y : -g.half_extents.y, corner & 4 ? g.half_extents.z : -g.half_extents.z };
				points.push_back(place(g, local));
			}
			break;
		case ShapeType::HULL:
			if (g.points.size() > size_t(B3_MAX_SHAPE_CAST_POINTS)) {
				return false;
			}
			for (const b3Vec3 &point : g.points) {
				points.push_back(place(g, { point.x * g.scale.x, point.y * g.scale.y, point.z * g.scale.z }));
			}
			break;
		default:
			return false;
	}
	for (b3Vec3 &point : points) {
		point = b3RotateVector(rotation, point);
	}
	proxy.points = points.data();
	proxy.count = int(points.size());
	proxy.radius = radius;
	return true;
}

void apply_body_def(b3BodyDef &def, const Props &props) {
	using namespace body_field;
	const View v(props);
	def.type = b3BodyType(v.i(TYPE, int(def.type)));
	def.position = v.v3(POSITION, def.position);
	def.rotation = v.q(ROTATION, def.rotation);
	def.linearVelocity = v.v3(LINEAR_VELOCITY, def.linearVelocity);
	def.angularVelocity = v.v3(ANGULAR_VELOCITY, def.angularVelocity);
	def.linearDamping = v.f(LINEAR_DAMPING, def.linearDamping);
	def.angularDamping = v.f(ANGULAR_DAMPING, def.angularDamping);
	def.gravityScale = v.f(GRAVITY_SCALE, def.gravityScale);
	def.sleepThreshold = v.f(SLEEP_THRESHOLD, def.sleepThreshold);
	def.safetyFactor = v.f(SAFETY_FACTOR, def.safetyFactor);
	def.motionLocks = locks(v, def.motionLocks);
	def.enableSleep = v.b(SLEEP, def.enableSleep);
	def.isAwake = v.b(AWAKE, def.isAwake);
	def.isBullet = v.b(BULLET, def.isBullet);
	def.isEnabled = v.b(ENABLED, def.isEnabled);
	def.allowFastRotation = v.b(FAST_ROTATION, def.allowFastRotation);
	def.enableContactRecycling = v.b(CONTACT_RECYCLING, def.enableContactRecycling);
}

void apply_shape_def(b3ShapeDef &def, const Props &props) {
	using namespace shape_field;
	const View v(props);
	def.baseMaterial = material(v, def.baseMaterial);
	def.density = v.f(DENSITY, def.density);
	def.explosionScale = v.f(EXPLOSION_SCALE, def.explosionScale);
	def.filter = filter(v, def.filter);
	def.isSensor = v.b(SENSOR, def.isSensor);
	def.enableSensorEvents = v.b(SENSOR_EVENTS, def.enableSensorEvents);
	def.enableContactEvents = v.b(CONTACT_EVENTS, def.enableContactEvents);
	def.enableHitEvents = v.b(HIT_EVENTS, def.enableHitEvents);
	def.enablePreSolveEvents = v.b(PRESOLVE_EVENTS, def.enablePreSolveEvents);
	def.enableSpeculativeContact = v.b(SPECULATIVE, def.enableSpeculativeContact);
	def.updateBodyMass = v.b(UPDATE_BODY_MASS, def.updateBodyMass);
	def.invokeContactCreation = v.b(INVOKE_CONTACT_CREATION, def.invokeContactCreation);
}

b3JointId create_joint(b3WorldId world, JointType type, b3BodyId a, b3BodyId b, const Props &props) {
	using namespace joint_field;
	const View v(props);
	auto base = [&](b3JointDef &def) {
		def.bodyIdA = a;
		def.bodyIdB = b;
		apply_base(def, v);
	};
	switch (type) {
		case JointType::DISTANCE: {
			b3DistanceJointDef def = b3DefaultDistanceJointDef();
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
			return b3CreateDistanceJoint(world, &def);
		}
		case JointType::SPHERICAL: {
			b3SphericalJointDef def = b3DefaultSphericalJointDef();
			base(def.base);
			def.enableSpring = v.b(ENABLE_SPRING, def.enableSpring);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.targetRotation = v.q(TARGET_ROTATION, def.targetRotation);
			def.enableConeLimit = v.b(ENABLE_CONE_LIMIT, def.enableConeLimit);
			def.coneAngle = v.f(CONE_ANGLE, def.coneAngle);
			def.enableTwistLimit = v.b(ENABLE_TWIST_LIMIT, def.enableTwistLimit);
			def.lowerTwistAngle = v.f(LOWER_TWIST_ANGLE, def.lowerTwistAngle);
			def.upperTwistAngle = v.f(UPPER_TWIST_ANGLE, def.upperTwistAngle);
			def.enableMotor = v.b(ENABLE_MOTOR, def.enableMotor);
			def.maxMotorTorque = v.f(MAX_MOTOR_TORQUE, def.maxMotorTorque);
			def.motorVelocity = v.v3(MOTOR_VELOCITY, def.motorVelocity);
			return b3CreateSphericalJoint(world, &def);
		}
		case JointType::PRISMATIC: {
			b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
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
			return b3CreatePrismaticJoint(world, &def);
		}
		case JointType::MOTOR: {
			b3MotorJointDef def = b3DefaultMotorJointDef();
			base(def.base);
			def.linearVelocity = v.v3(LINEAR_VELOCITY, def.linearVelocity);
			def.maxVelocityForce = v.f(MAX_VELOCITY_FORCE, def.maxVelocityForce);
			def.angularVelocity = v.v3(ANGULAR_VELOCITY, def.angularVelocity);
			def.maxVelocityTorque = v.f(MAX_VELOCITY_TORQUE, def.maxVelocityTorque);
			def.linearHertz = v.f(LINEAR_HERTZ, def.linearHertz);
			def.linearDampingRatio = v.f(LINEAR_DAMPING_RATIO, def.linearDampingRatio);
			def.maxSpringForce = v.f(MAX_SPRING_FORCE, def.maxSpringForce);
			def.angularHertz = v.f(ANGULAR_HERTZ, def.angularHertz);
			def.angularDampingRatio = v.f(ANGULAR_DAMPING_RATIO, def.angularDampingRatio);
			def.maxSpringTorque = v.f(MAX_SPRING_TORQUE, def.maxSpringTorque);
			return b3CreateMotorJoint(world, &def);
		}
		case JointType::REVOLUTE: {
			b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
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
			return b3CreateRevoluteJoint(world, &def);
		}
		case JointType::WELD:
		case JointType::GENERIC: {
			b3WeldJointDef def = b3DefaultWeldJointDef();
			base(def.base);
			def.linearHertz = v.f(LINEAR_HERTZ, def.linearHertz);
			def.angularHertz = v.f(ANGULAR_HERTZ, def.angularHertz);
			def.linearDampingRatio = v.f(LINEAR_DAMPING_RATIO, def.linearDampingRatio);
			def.angularDampingRatio = v.f(ANGULAR_DAMPING_RATIO, def.angularDampingRatio);
			if (type == JointType::WELD) {
				return b3CreateWeldJoint(world, &def);
			}
			const b3JointId joint = b3CreateGenericJoint(world, &def);
			apply_generic(joint, v);
			return joint;
		}
		case JointType::WHEEL: {
			b3WheelJointDef def = b3DefaultWheelJointDef();
			base(def.base);
			def.enableSuspensionSpring = v.b(ENABLE_SUSPENSION_SPRING, def.enableSuspensionSpring);
			def.suspensionHertz = v.f(SUSPENSION_HERTZ, def.suspensionHertz);
			def.suspensionDampingRatio = v.f(SUSPENSION_DAMPING_RATIO, def.suspensionDampingRatio);
			def.enableSuspensionLimit = v.b(ENABLE_SUSPENSION_LIMIT, def.enableSuspensionLimit);
			def.lowerSuspensionLimit = v.f(LOWER_SUSPENSION_LIMIT, def.lowerSuspensionLimit);
			def.upperSuspensionLimit = v.f(UPPER_SUSPENSION_LIMIT, def.upperSuspensionLimit);
			def.enableSpinMotor = v.b(ENABLE_SPIN_MOTOR, def.enableSpinMotor);
			def.maxSpinTorque = v.f(MAX_SPIN_TORQUE, def.maxSpinTorque);
			def.spinSpeed = v.f(SPIN_SPEED, def.spinSpeed);
			def.enableSteering = v.b(ENABLE_STEERING, def.enableSteering);
			def.steeringHertz = v.f(STEERING_HERTZ, def.steeringHertz);
			def.steeringDampingRatio = v.f(STEERING_DAMPING_RATIO, def.steeringDampingRatio);
			def.targetSteeringAngle = v.f(TARGET_STEERING_ANGLE, def.targetSteeringAngle);
			def.maxSteeringTorque = v.f(MAX_STEERING_TORQUE, def.maxSteeringTorque);
			def.enableSteeringLimit = v.b(ENABLE_STEERING_LIMIT, def.enableSteeringLimit);
			def.lowerSteeringLimit = v.f(LOWER_STEERING_LIMIT, def.lowerSteeringLimit);
			def.upperSteeringLimit = v.f(UPPER_STEERING_LIMIT, def.upperSteeringLimit);
			return b3CreateWheelJoint(world, &def);
		}
		case JointType::FILTER: {
			b3FilterJointDef def = b3DefaultFilterJointDef();
			base(def.base);
			return b3CreateFilterJoint(world, &def);
		}
		case JointType::PARALLEL: {
			b3ParallelJointDef def = b3DefaultParallelJointDef();
			base(def.base);
			def.hertz = v.f(HERTZ, def.hertz);
			def.dampingRatio = v.f(DAMPING_RATIO, def.dampingRatio);
			def.maxTorque = v.f(MAX_TORQUE, def.maxTorque);
			return b3CreateParallelJoint(world, &def);
		}
	}
	return b3_nullJointId;
}

void apply_world(b3WorldId world, const Props &props) {
	using namespace world_field;
	const View v(props);
	if (v.has(GRAVITY)) {
		b3World_SetGravity(world, v.v3(GRAVITY, {}));
	}
	if (v.has(RESTITUTION_THRESHOLD)) {
		b3World_SetRestitutionThreshold(world, v.f(RESTITUTION_THRESHOLD, 0));
	}
	if (v.has(RESTITUTION_ITERATIONS)) {
		b3World_SetRestitutionIterations(world, v.i(RESTITUTION_ITERATIONS, 1));
	}
	if (v.has(RESTITUTION_PROPAGATION)) {
		b3World_EnableRestitutionPropagation(world, v.b(RESTITUTION_PROPAGATION, false));
	}
	if (v.has(HIT_EVENT_THRESHOLD)) {
		b3World_SetHitEventThreshold(world, v.f(HIT_EVENT_THRESHOLD, 0));
	}
	if (v.has(CONTACT_HERTZ) || v.has(CONTACT_DAMPING_RATIO) || v.has(CONTACT_SPEED)) {
		float hertz = 0, damping = 0, speed = 0;
		b3World_GetContactTuning(world, &hertz, &damping, &speed);
		b3World_SetContactTuning(world, v.f(CONTACT_HERTZ, hertz), v.f(CONTACT_DAMPING_RATIO, damping), v.f(CONTACT_SPEED, speed));
	}
	if (v.has(MAXIMUM_LINEAR_SPEED)) {
		b3World_SetMaximumLinearSpeed(world, v.f(MAXIMUM_LINEAR_SPEED, 0));
	}
	if (v.has(SLEEP)) {
		b3World_EnableSleeping(world, v.b(SLEEP, true));
	}
	if (v.has(CONTINUOUS)) {
		b3World_EnableContinuous(world, v.b(CONTINUOUS, true));
	}
	if (v.has(WARM_STARTING)) {
		b3World_EnableWarmStarting(world, v.b(WARM_STARTING, true));
	}
	if (v.has(SPECULATIVE)) {
		b3World_EnableSpeculative(world, v.b(SPECULATIVE, true));
	}
	if (v.has(CONTACT_RECYCLE_DISTANCE)) {
		b3World_SetContactRecycleDistance(world, v.f(CONTACT_RECYCLE_DISTANCE, 0));
	}
}

void apply_body(b3BodyId body, const Props &props, float step) {
	using namespace body_field;
	const View v(props);
	if (v.has(TYPE)) {
		b3Body_SetType(body, b3BodyType(v.i(TYPE, 2)));
	}
	if (v.has(POSITION) || v.has(ROTATION)) {
		b3Body_SetTransform(body, v.v3(POSITION, b3Body_GetPosition(body)), v.q(ROTATION, b3Body_GetRotation(body)));
	}
	if (v.has(LINEAR_VELOCITY)) {
		b3Body_SetLinearVelocity(body, v.v3(LINEAR_VELOCITY, {}));
	}
	if (v.has(ANGULAR_VELOCITY)) {
		b3Body_SetAngularVelocity(body, v.v3(ANGULAR_VELOCITY, {}));
	}
	if (v.has(LINEAR_DAMPING)) {
		b3Body_SetLinearDamping(body, v.f(LINEAR_DAMPING, 0));
	}
	if (v.has(ANGULAR_DAMPING)) {
		b3Body_SetAngularDamping(body, v.f(ANGULAR_DAMPING, 0));
	}
	if (v.has(GRAVITY_SCALE)) {
		b3Body_SetGravityScale(body, v.f(GRAVITY_SCALE, 1));
	}
	if (v.has(SLEEP_THRESHOLD)) {
		b3Body_SetSleepThreshold(body, v.f(SLEEP_THRESHOLD, 0));
	}
	if (v.has(SAFETY_FACTOR)) {
		b3Body_SetSafetyFactor(body, v.f(SAFETY_FACTOR, 0));
	}
	if (v.has(LOCK_LINEAR) || v.has(LOCK_ANGULAR)) {
		b3Body_SetMotionLocks(body, locks(v, b3Body_GetMotionLocks(body)));
	}
	if (v.has(SLEEP)) {
		b3Body_EnableSleep(body, v.b(SLEEP, true));
	}
	if (v.has(BULLET)) {
		b3Body_SetBullet(body, v.b(BULLET, false));
	}
	if (v.has(ENABLED)) {
		if (v.b(ENABLED, true)) {
			b3Body_Enable(body);
		} else {
			b3Body_Disable(body);
		}
	}
	if (v.has(FAST_ROTATION)) {
		b3Body_AllowFastRotation(body, v.b(FAST_ROTATION, false));
	}
	if (v.has(CONTACT_RECYCLING)) {
		b3Body_EnableContactRecycling(body, v.b(CONTACT_RECYCLING, true));
	}
	if (v.has(HIT_EVENTS)) {
		b3Body_EnableHitEvents(body, v.b(HIT_EVENTS, false));
	}
	if (v.has(MASS) || v.has(CENTER_OF_MASS) || v.has(INERTIA)) {
		b3MassData mass = b3Body_GetMassData(body);
		mass.mass = v.f(MASS, mass.mass);
		mass.center = v.v3(CENTER_OF_MASS, mass.center);
		if (const Prop *p = v.find(INERTIA)) {
			mass.inertia = {};
			mass.inertia.cx.x = float(p->v[0]);
			mass.inertia.cy.y = float(p->v[1]);
			mass.inertia.cz.z = float(p->v[2]);
		}
		b3Body_SetMassData(body, mass);
	}
	if (v.has(TARGET_POSITION) || v.has(TARGET_ROTATION)) {
		const b3WorldTransform target = { v.v3(TARGET_POSITION, b3Body_GetPosition(body)), v.q(TARGET_ROTATION, b3Body_GetRotation(body)) };
		b3Body_SetTargetTransform(body, target, v.f(TARGET_TIME, step), true);
	}
	if (v.has(AWAKE)) {
		b3Body_SetAwake(body, v.b(AWAKE, true));
	}
}

void apply_body_extras(b3BodyId body, const Props &props, float step) {
	using namespace body_field;
	Props extras;
	for (const Prop &p : props) {
		if (p.id == HIT_EVENTS || p.id == MASS || p.id == CENTER_OF_MASS || p.id == INERTIA || p.id == TARGET_POSITION || p.id == TARGET_ROTATION || p.id == TARGET_TIME) {
			extras.push_back(p);
		}
	}
	if (!extras.empty()) {
		apply_body(body, extras, step);
	}
}

void apply_shape(b3ShapeId shape, const Props &props, int32_t material_index) {
	using namespace shape_field;
	const View v(props);
	if (material_index > 0) {
		// One entry of a mesh or height field's per-triangle material table.
		b3Shape_SetMeshMaterial(shape, material(v, b3Shape_GetMeshSurfaceMaterial(shape, material_index)), material_index);
	} else if (v.has(FRICTION) || v.has(RESTITUTION) || v.has(ROLLING_RESISTANCE) || v.has(TANGENT_VELOCITY) || v.has(USER_MATERIAL)) {
		b3Shape_SetSurfaceMaterial(shape, material(v, b3Shape_GetSurfaceMaterial(shape)));
	}
	if (v.has(DENSITY)) {
		b3Shape_SetDensity(shape, v.f(DENSITY, 1), v.b(UPDATE_BODY_MASS, true));
	}
	if (v.has(CATEGORY) || v.has(MASK) || v.has(GROUP)) {
		// Recompute existing contacts so the new filter applies immediately.
		b3Shape_SetFilter(shape, filter(v, b3Shape_GetFilter(shape)), true);
	}
	if (v.has(SENSOR_EVENTS)) {
		b3Shape_EnableSensorEvents(shape, v.b(SENSOR_EVENTS, false));
	}
	if (v.has(CONTACT_EVENTS)) {
		b3Shape_EnableContactEvents(shape, v.b(CONTACT_EVENTS, false));
	}
	if (v.has(HIT_EVENTS)) {
		b3Shape_EnableHitEvents(shape, v.b(HIT_EVENTS, false));
	}
	if (v.has(PRESOLVE_EVENTS)) {
		b3Shape_EnablePreSolveEvents(shape, v.b(PRESOLVE_EVENTS, false));
	}
}

void apply_joint(b3JointId joint, const Props &props) {
	using namespace joint_field;
	const View v(props);
	if (v.has(ANCHOR_A) || v.has(ROTATION_A)) {
		const b3Transform frame = b3Joint_GetLocalFrameA(joint);
		b3Joint_SetLocalFrameA(joint, { v.v3(ANCHOR_A, frame.p), v.q(ROTATION_A, frame.q) });
	}
	if (v.has(ANCHOR_B) || v.has(ROTATION_B)) {
		const b3Transform frame = b3Joint_GetLocalFrameB(joint);
		b3Joint_SetLocalFrameB(joint, { v.v3(ANCHOR_B, frame.p), v.q(ROTATION_B, frame.q) });
	}
	if (v.has(COLLIDE_CONNECTED)) {
		b3Joint_SetCollideConnected(joint, v.b(COLLIDE_CONNECTED, false));
	}
	if (v.has(FORCE_THRESHOLD)) {
		b3Joint_SetForceThreshold(joint, v.f(FORCE_THRESHOLD, 0));
	}
	if (v.has(TORQUE_THRESHOLD)) {
		b3Joint_SetTorqueThreshold(joint, v.f(TORQUE_THRESHOLD, 0));
	}
	if (v.has(CONSTRAINT_HERTZ) || v.has(CONSTRAINT_DAMPING_RATIO)) {
		float hertz = 0, damping = 0;
		b3Joint_GetConstraintTuning(joint, &hertz, &damping);
		b3Joint_SetConstraintTuning(joint, v.f(CONSTRAINT_HERTZ, hertz), v.f(CONSTRAINT_DAMPING_RATIO, damping));
	}
	switch (b3Joint_GetType(joint)) {
		case b3_distanceJoint:
			if (v.has(LENGTH)) b3DistanceJoint_SetLength(joint, v.f(LENGTH, 1));
			if (v.has(ENABLE_SPRING)) b3DistanceJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(LOWER_SPRING_FORCE) || v.has(UPPER_SPRING_FORCE)) {
				float lower = 0, upper = 0;
				b3DistanceJoint_GetSpringForceRange(joint, &lower, &upper);
				b3DistanceJoint_SetSpringForceRange(joint, v.f(LOWER_SPRING_FORCE, lower), v.f(UPPER_SPRING_FORCE, upper));
			}
			if (v.has(HERTZ)) b3DistanceJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b3DistanceJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(ENABLE_LIMIT)) b3DistanceJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(MIN_LENGTH) || v.has(MAX_LENGTH)) b3DistanceJoint_SetLengthRange(joint, v.f(MIN_LENGTH, b3DistanceJoint_GetMinLength(joint)), v.f(MAX_LENGTH, b3DistanceJoint_GetMaxLength(joint)));
			if (v.has(ENABLE_MOTOR)) b3DistanceJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_FORCE)) b3DistanceJoint_SetMaxMotorForce(joint, v.f(MAX_MOTOR_FORCE, 0));
			if (v.has(MOTOR_SPEED)) b3DistanceJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b3_motorJoint:
			if (v.has(LINEAR_VELOCITY)) b3MotorJoint_SetLinearVelocity(joint, v.v3(LINEAR_VELOCITY, {}));
			if (v.has(MAX_VELOCITY_FORCE)) b3MotorJoint_SetMaxVelocityForce(joint, v.f(MAX_VELOCITY_FORCE, 0));
			if (v.has(ANGULAR_VELOCITY)) b3MotorJoint_SetAngularVelocity(joint, v.v3(ANGULAR_VELOCITY, {}));
			if (v.has(MAX_VELOCITY_TORQUE)) b3MotorJoint_SetMaxVelocityTorque(joint, v.f(MAX_VELOCITY_TORQUE, 0));
			if (v.has(LINEAR_HERTZ)) b3MotorJoint_SetLinearHertz(joint, v.f(LINEAR_HERTZ, 0));
			if (v.has(LINEAR_DAMPING_RATIO)) b3MotorJoint_SetLinearDampingRatio(joint, v.f(LINEAR_DAMPING_RATIO, 0));
			if (v.has(MAX_SPRING_FORCE)) b3MotorJoint_SetMaxSpringForce(joint, v.f(MAX_SPRING_FORCE, 0));
			if (v.has(ANGULAR_HERTZ)) b3MotorJoint_SetAngularHertz(joint, v.f(ANGULAR_HERTZ, 0));
			if (v.has(ANGULAR_DAMPING_RATIO)) b3MotorJoint_SetAngularDampingRatio(joint, v.f(ANGULAR_DAMPING_RATIO, 0));
			if (v.has(MAX_SPRING_TORQUE)) b3MotorJoint_SetMaxSpringTorque(joint, v.f(MAX_SPRING_TORQUE, 0));
			break;
		case b3_prismaticJoint:
			if (v.has(ENABLE_SPRING)) b3PrismaticJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b3PrismaticJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b3PrismaticJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(TARGET_TRANSLATION)) b3PrismaticJoint_SetTargetTranslation(joint, v.f(TARGET_TRANSLATION, 0));
			if (v.has(ENABLE_LIMIT)) b3PrismaticJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(LOWER_TRANSLATION) || v.has(UPPER_TRANSLATION)) b3PrismaticJoint_SetLimits(joint, v.f(LOWER_TRANSLATION, b3PrismaticJoint_GetLowerLimit(joint)), v.f(UPPER_TRANSLATION, b3PrismaticJoint_GetUpperLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b3PrismaticJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_FORCE)) b3PrismaticJoint_SetMaxMotorForce(joint, v.f(MAX_MOTOR_FORCE, 0));
			if (v.has(MOTOR_SPEED)) b3PrismaticJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b3_revoluteJoint:
			if (v.has(ENABLE_SPRING)) b3RevoluteJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b3RevoluteJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b3RevoluteJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(TARGET_ANGLE)) b3RevoluteJoint_SetTargetAngle(joint, v.f(TARGET_ANGLE, 0));
			if (v.has(ENABLE_LIMIT)) b3RevoluteJoint_EnableLimit(joint, v.b(ENABLE_LIMIT, false));
			if (v.has(LOWER_ANGLE) || v.has(UPPER_ANGLE)) b3RevoluteJoint_SetLimits(joint, v.f(LOWER_ANGLE, b3RevoluteJoint_GetLowerLimit(joint)), v.f(UPPER_ANGLE, b3RevoluteJoint_GetUpperLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b3RevoluteJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_TORQUE)) b3RevoluteJoint_SetMaxMotorTorque(joint, v.f(MAX_MOTOR_TORQUE, 0));
			if (v.has(MOTOR_SPEED)) b3RevoluteJoint_SetMotorSpeed(joint, v.f(MOTOR_SPEED, 0));
			break;
		case b3_sphericalJoint:
			if (v.has(ENABLE_SPRING)) b3SphericalJoint_EnableSpring(joint, v.b(ENABLE_SPRING, false));
			if (v.has(HERTZ)) b3SphericalJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b3SphericalJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(TARGET_ROTATION)) b3SphericalJoint_SetTargetRotation(joint, v.q(TARGET_ROTATION, { {}, 1 }));
			if (v.has(ENABLE_CONE_LIMIT)) b3SphericalJoint_EnableConeLimit(joint, v.b(ENABLE_CONE_LIMIT, false));
			if (v.has(CONE_ANGLE)) b3SphericalJoint_SetConeLimit(joint, v.f(CONE_ANGLE, 0));
			if (v.has(ENABLE_TWIST_LIMIT)) b3SphericalJoint_EnableTwistLimit(joint, v.b(ENABLE_TWIST_LIMIT, false));
			if (v.has(LOWER_TWIST_ANGLE) || v.has(UPPER_TWIST_ANGLE)) b3SphericalJoint_SetTwistLimits(joint, v.f(LOWER_TWIST_ANGLE, b3SphericalJoint_GetLowerTwistLimit(joint)), v.f(UPPER_TWIST_ANGLE, b3SphericalJoint_GetUpperTwistLimit(joint)));
			if (v.has(ENABLE_MOTOR)) b3SphericalJoint_EnableMotor(joint, v.b(ENABLE_MOTOR, false));
			if (v.has(MAX_MOTOR_TORQUE)) b3SphericalJoint_SetMaxMotorTorque(joint, v.f(MAX_MOTOR_TORQUE, 0));
			if (v.has(MOTOR_VELOCITY)) b3SphericalJoint_SetMotorVelocity(joint, v.v3(MOTOR_VELOCITY, {}));
			break;
		case b3_weldJoint:
			if (v.has(LINEAR_HERTZ)) b3WeldJoint_SetLinearHertz(joint, v.f(LINEAR_HERTZ, 0));
			if (v.has(LINEAR_DAMPING_RATIO)) b3WeldJoint_SetLinearDampingRatio(joint, v.f(LINEAR_DAMPING_RATIO, 0));
			if (v.has(ANGULAR_HERTZ)) b3WeldJoint_SetAngularHertz(joint, v.f(ANGULAR_HERTZ, 0));
			if (v.has(ANGULAR_DAMPING_RATIO)) b3WeldJoint_SetAngularDampingRatio(joint, v.f(ANGULAR_DAMPING_RATIO, 0));
			break;
		case b3_wheelJoint:
			if (v.has(ENABLE_SUSPENSION_SPRING)) b3WheelJoint_EnableSuspension(joint, v.b(ENABLE_SUSPENSION_SPRING, false));
			if (v.has(SUSPENSION_HERTZ)) b3WheelJoint_SetSuspensionHertz(joint, v.f(SUSPENSION_HERTZ, 0));
			if (v.has(SUSPENSION_DAMPING_RATIO)) b3WheelJoint_SetSuspensionDampingRatio(joint, v.f(SUSPENSION_DAMPING_RATIO, 0));
			if (v.has(ENABLE_SUSPENSION_LIMIT)) b3WheelJoint_EnableSuspensionLimit(joint, v.b(ENABLE_SUSPENSION_LIMIT, false));
			if (v.has(LOWER_SUSPENSION_LIMIT) || v.has(UPPER_SUSPENSION_LIMIT)) b3WheelJoint_SetSuspensionLimits(joint, v.f(LOWER_SUSPENSION_LIMIT, b3WheelJoint_GetLowerSuspensionLimit(joint)), v.f(UPPER_SUSPENSION_LIMIT, b3WheelJoint_GetUpperSuspensionLimit(joint)));
			if (v.has(ENABLE_SPIN_MOTOR)) b3WheelJoint_EnableSpinMotor(joint, v.b(ENABLE_SPIN_MOTOR, false));
			if (v.has(MAX_SPIN_TORQUE)) b3WheelJoint_SetMaxSpinTorque(joint, v.f(MAX_SPIN_TORQUE, 0));
			if (v.has(SPIN_SPEED)) b3WheelJoint_SetSpinMotorSpeed(joint, v.f(SPIN_SPEED, 0));
			if (v.has(ENABLE_STEERING)) b3WheelJoint_EnableSteering(joint, v.b(ENABLE_STEERING, false));
			if (v.has(STEERING_HERTZ)) b3WheelJoint_SetSteeringHertz(joint, v.f(STEERING_HERTZ, 0));
			if (v.has(STEERING_DAMPING_RATIO)) b3WheelJoint_SetSteeringDampingRatio(joint, v.f(STEERING_DAMPING_RATIO, 0));
			if (v.has(TARGET_STEERING_ANGLE)) b3WheelJoint_SetTargetSteeringAngle(joint, v.f(TARGET_STEERING_ANGLE, 0));
			if (v.has(MAX_STEERING_TORQUE)) b3WheelJoint_SetMaxSteeringTorque(joint, v.f(MAX_STEERING_TORQUE, 0));
			if (v.has(ENABLE_STEERING_LIMIT)) b3WheelJoint_EnableSteeringLimit(joint, v.b(ENABLE_STEERING_LIMIT, false));
			if (v.has(LOWER_STEERING_LIMIT) || v.has(UPPER_STEERING_LIMIT)) b3WheelJoint_SetSteeringLimits(joint, v.f(LOWER_STEERING_LIMIT, b3WheelJoint_GetLowerSteeringLimit(joint)), v.f(UPPER_STEERING_LIMIT, b3WheelJoint_GetUpperSteeringLimit(joint)));
			break;
		case b3_parallelJoint:
			if (v.has(HERTZ)) b3ParallelJoint_SetSpringHertz(joint, v.f(HERTZ, 0));
			if (v.has(DAMPING_RATIO)) b3ParallelJoint_SetSpringDampingRatio(joint, v.f(DAMPING_RATIO, 0));
			if (v.has(MAX_TORQUE)) b3ParallelJoint_SetMaxTorque(joint, v.f(MAX_TORQUE, 0));
			break;
		case b3_genericJoint:
			apply_generic(joint, v);
			break;
		default:
			break;
	}
}

void read_world(b3WorldId world, Values &out) {
	out.clear();
	put_vec(out, "gravity", b3World_GetGravity(world));
	put(out, "restitution_threshold", b3World_GetRestitutionThreshold(world));
	put_int(out, "restitution_iterations", b3World_GetRestitutionIterations(world));
	put_bool(out, "restitution_propagation", b3World_IsRestitutionPropagationEnabled(world));
	put(out, "hit_event_threshold", b3World_GetHitEventThreshold(world));
	put(out, "maximum_linear_speed", b3World_GetMaximumLinearSpeed(world));
	put_bool(out, "sleep", b3World_IsSleepingEnabled(world));
	put_bool(out, "continuous", b3World_IsContinuousEnabled(world));
	put_bool(out, "warm_starting", b3World_IsWarmStartingEnabled(world));
	put(out, "contact_recycle_distance", b3World_GetContactRecycleDistance(world));
	float hertz = 0, damping = 0, speed = 0;
	b3World_GetContactTuning(world, &hertz, &damping, &speed);
	put(out, "contact_hertz", hertz);
	put(out, "contact_damping_ratio", damping);
	put(out, "contact_speed", speed);
	put_bool(out, "speculative", b3World_IsSpeculativeEnabled(world));
	put_int(out, "awake_body_count", b3World_GetAwakeBodyCount(world));
	const b3Counters counters = b3World_GetCounters(world);
	put_int(out, "body_count", counters.bodyCount);
	put_int(out, "shape_count", counters.shapeCount);
	put_int(out, "contact_count", counters.contactCount);
	put_int(out, "joint_count", counters.jointCount);
	put_int(out, "island_count", counters.islandCount);
	const b3AABB bounds = b3World_GetBounds(world);
	put_vec(out, "bounds_lower", bounds.lowerBound);
	put_vec(out, "bounds_upper", bounds.upperBound);
}

void read_body(b3BodyId body, Values &out) {
	out.clear();
	put_int(out, "type", int(b3Body_GetType(body)));
	put_vec(out, "position", b3Body_GetPosition(body));
	put_quat(out, "rotation", b3Body_GetRotation(body));
	put_vec(out, "linear_velocity", b3Body_GetLinearVelocity(body));
	put_vec(out, "angular_velocity", b3Body_GetAngularVelocity(body));
	put(out, "linear_damping", b3Body_GetLinearDamping(body));
	put(out, "angular_damping", b3Body_GetAngularDamping(body));
	put(out, "gravity_scale", b3Body_GetGravityScale(body));
	put(out, "sleep_threshold", b3Body_GetSleepThreshold(body));
	put(out, "safety_factor", b3Body_GetSafetyFactor(body));
	const b3MotionLocks locks = b3Body_GetMotionLocks(body);
	put_vec(out, "lock_linear", { locks.linearX ? 1.0f : 0.0f, locks.linearY ? 1.0f : 0.0f, locks.linearZ ? 1.0f : 0.0f });
	put_vec(out, "lock_angular", { locks.angularX ? 1.0f : 0.0f, locks.angularY ? 1.0f : 0.0f, locks.angularZ ? 1.0f : 0.0f });
	put_bool(out, "sleep", b3Body_IsSleepEnabled(body));
	put_bool(out, "awake", b3Body_IsAwake(body));
	put_bool(out, "bullet", b3Body_IsBullet(body));
	put_bool(out, "enabled", b3Body_IsEnabled(body));
	put_bool(out, "fast_rotation", b3Body_IsFastRotationAllowed(body));
	put_bool(out, "contact_recycling", b3Body_IsContactRecyclingEnabled(body));
	const b3MassData mass = b3Body_GetMassData(body);
	put(out, "mass", mass.mass);
	put_vec(out, "center_of_mass", mass.center);
	put_vec(out, "inertia", { mass.inertia.cx.x, mass.inertia.cy.y, mass.inertia.cz.z });
	put_vec(out, "world_center_of_mass", b3Body_GetWorldCenter(body));
	put_int(out, "shape_count", b3Body_GetShapeCount(body));
	put_int(out, "joint_count", b3Body_GetJointCount(body));
}

void read_material(const b3SurfaceMaterial &m, Values &out) {
	put(out, "friction", m.friction);
	put(out, "restitution", m.restitution);
	put(out, "rolling_resistance", m.rollingResistance);
	put_vec(out, "tangent_velocity", m.tangentVelocity);
	put_u64(out, "user_material", m.userMaterialId);
}

void read_shape(b3ShapeId shape, Values &out) {
	out.clear();
	const b3ShapeType type = b3Shape_GetType(shape);
	// Compounds keep per-child materials; their shape material is the first child's.
	read_material(b3Shape_GetMeshSurfaceMaterial(shape, 0), out);
	put_int(out, "material_count", b3Shape_GetMeshMaterialCount(shape));
	put_int(out, "box3d_type", int(type));
	put(out, "density", b3Shape_GetDensity(shape));
	const b3Filter f = b3Shape_GetFilter(shape);
	put_u64(out, "category", f.categoryBits);
	put_u64(out, "mask", f.maskBits);
	put_int(out, "group", f.groupIndex);
	put_bool(out, "sensor", b3Shape_IsSensor(shape));
	put_bool(out, "sensor_events", b3Shape_AreSensorEventsEnabled(shape));
	put_bool(out, "contact_events", b3Shape_AreContactEventsEnabled(shape));
	put_bool(out, "hit_events", b3Shape_AreHitEventsEnabled(shape));
	put_bool(out, "presolve_events", b3Shape_ArePreSolveEventsEnabled(shape));
	const b3AABB box = b3Shape_GetAABB(shape);
	put_vec(out, "aabb_lower", box.lowerBound);
	put_vec(out, "aabb_upper", box.upperBound);
}

void read_joint(b3JointId joint, Values &out) {
	out.clear();
	const b3Transform a = b3Joint_GetLocalFrameA(joint);
	const b3Transform b = b3Joint_GetLocalFrameB(joint);
	put_vec(out, "anchor_a", a.p);
	put_quat(out, "rotation_a", a.q);
	put_vec(out, "anchor_b", b.p);
	put_quat(out, "rotation_b", b.q);
	put_bool(out, "collide_connected", b3Joint_GetCollideConnected(joint));
	put(out, "force_threshold", b3Joint_GetForceThreshold(joint));
	put(out, "torque_threshold", b3Joint_GetTorqueThreshold(joint));
	float hertz = 0, damping = 0;
	b3Joint_GetConstraintTuning(joint, &hertz, &damping);
	put(out, "constraint_hertz", hertz);
	put(out, "constraint_damping_ratio", damping);
	put_vec(out, "constraint_force", b3Joint_GetConstraintForce(joint));
	put_vec(out, "constraint_torque", b3Joint_GetConstraintTorque(joint));
	put(out, "linear_separation", b3Joint_GetLinearSeparation(joint));
	put(out, "angular_separation", b3Joint_GetAngularSeparation(joint));
	put_bool(out, "awake", b3Joint_IsAwake(joint));
	switch (b3Joint_GetType(joint)) {
		case b3_distanceJoint: {
			put(out, "length", b3DistanceJoint_GetLength(joint));
			put_bool(out, "enable_spring", b3DistanceJoint_IsSpringEnabled(joint));
			float lower = 0, upper = 0;
			b3DistanceJoint_GetSpringForceRange(joint, &lower, &upper);
			put(out, "lower_spring_force", lower);
			put(out, "upper_spring_force", upper);
			put(out, "hertz", b3DistanceJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b3DistanceJoint_GetSpringDampingRatio(joint));
			put_bool(out, "enable_limit", b3DistanceJoint_IsLimitEnabled(joint));
			put(out, "min_length", b3DistanceJoint_GetMinLength(joint));
			put(out, "max_length", b3DistanceJoint_GetMaxLength(joint));
			put_bool(out, "enable_motor", b3DistanceJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b3DistanceJoint_GetMotorSpeed(joint));
			put(out, "max_motor_force", b3DistanceJoint_GetMaxMotorForce(joint));
			put(out, "current_length", b3DistanceJoint_GetCurrentLength(joint));
			put(out, "motor_force", b3DistanceJoint_GetMotorForce(joint));
			break;
		}
		case b3_motorJoint:
			put_vec(out, "linear_velocity", b3MotorJoint_GetLinearVelocity(joint));
			put_vec(out, "angular_velocity", b3MotorJoint_GetAngularVelocity(joint));
			put(out, "max_velocity_force", b3MotorJoint_GetMaxVelocityForce(joint));
			put(out, "max_velocity_torque", b3MotorJoint_GetMaxVelocityTorque(joint));
			put(out, "linear_hertz", b3MotorJoint_GetLinearHertz(joint));
			put(out, "linear_damping_ratio", b3MotorJoint_GetLinearDampingRatio(joint));
			put(out, "angular_hertz", b3MotorJoint_GetAngularHertz(joint));
			put(out, "angular_damping_ratio", b3MotorJoint_GetAngularDampingRatio(joint));
			put(out, "max_spring_force", b3MotorJoint_GetMaxSpringForce(joint));
			put(out, "max_spring_torque", b3MotorJoint_GetMaxSpringTorque(joint));
			break;
		case b3_prismaticJoint:
			put_bool(out, "enable_spring", b3PrismaticJoint_IsSpringEnabled(joint));
			put(out, "hertz", b3PrismaticJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b3PrismaticJoint_GetSpringDampingRatio(joint));
			put(out, "target_translation", b3PrismaticJoint_GetTargetTranslation(joint));
			put_bool(out, "enable_limit", b3PrismaticJoint_IsLimitEnabled(joint));
			put(out, "lower_translation", b3PrismaticJoint_GetLowerLimit(joint));
			put(out, "upper_translation", b3PrismaticJoint_GetUpperLimit(joint));
			put_bool(out, "enable_motor", b3PrismaticJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b3PrismaticJoint_GetMotorSpeed(joint));
			put(out, "max_motor_force", b3PrismaticJoint_GetMaxMotorForce(joint));
			put(out, "motor_force", b3PrismaticJoint_GetMotorForce(joint));
			put(out, "translation", b3PrismaticJoint_GetTranslation(joint));
			put(out, "speed", b3PrismaticJoint_GetSpeed(joint));
			break;
		case b3_revoluteJoint:
			put_bool(out, "enable_spring", b3RevoluteJoint_IsSpringEnabled(joint));
			put(out, "hertz", b3RevoluteJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b3RevoluteJoint_GetSpringDampingRatio(joint));
			put(out, "target_angle", b3RevoluteJoint_GetTargetAngle(joint));
			put_bool(out, "enable_limit", b3RevoluteJoint_IsLimitEnabled(joint));
			put(out, "lower_angle", b3RevoluteJoint_GetLowerLimit(joint));
			put(out, "upper_angle", b3RevoluteJoint_GetUpperLimit(joint));
			put_bool(out, "enable_motor", b3RevoluteJoint_IsMotorEnabled(joint));
			put(out, "motor_speed", b3RevoluteJoint_GetMotorSpeed(joint));
			put(out, "max_motor_torque", b3RevoluteJoint_GetMaxMotorTorque(joint));
			put(out, "motor_torque", b3RevoluteJoint_GetMotorTorque(joint));
			put(out, "angle", b3RevoluteJoint_GetAngle(joint));
			break;
		case b3_sphericalJoint:
			put_bool(out, "enable_spring", b3SphericalJoint_IsSpringEnabled(joint));
			put(out, "hertz", b3SphericalJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b3SphericalJoint_GetSpringDampingRatio(joint));
			put_quat(out, "target_rotation", b3SphericalJoint_GetTargetRotation(joint));
			put_bool(out, "enable_cone_limit", b3SphericalJoint_IsConeLimitEnabled(joint));
			put(out, "cone_angle", b3SphericalJoint_GetConeLimit(joint));
			put_bool(out, "enable_twist_limit", b3SphericalJoint_IsTwistLimitEnabled(joint));
			put(out, "lower_twist_angle", b3SphericalJoint_GetLowerTwistLimit(joint));
			put(out, "upper_twist_angle", b3SphericalJoint_GetUpperTwistLimit(joint));
			put_bool(out, "enable_motor", b3SphericalJoint_IsMotorEnabled(joint));
			put(out, "max_motor_torque", b3SphericalJoint_GetMaxMotorTorque(joint));
			put_vec(out, "motor_velocity", b3SphericalJoint_GetMotorVelocity(joint));
			put_vec(out, "motor_torque", b3SphericalJoint_GetMotorTorque(joint));
			put(out, "current_cone_angle", b3SphericalJoint_GetConeAngle(joint));
			put(out, "twist_angle", b3SphericalJoint_GetTwistAngle(joint));
			break;
		case b3_weldJoint:
			put(out, "linear_hertz", b3WeldJoint_GetLinearHertz(joint));
			put(out, "linear_damping_ratio", b3WeldJoint_GetLinearDampingRatio(joint));
			put(out, "angular_hertz", b3WeldJoint_GetAngularHertz(joint));
			put(out, "angular_damping_ratio", b3WeldJoint_GetAngularDampingRatio(joint));
			break;
		case b3_wheelJoint:
			put_bool(out, "enable_suspension_spring", b3WheelJoint_IsSuspensionEnabled(joint));
			put(out, "suspension_hertz", b3WheelJoint_GetSuspensionHertz(joint));
			put(out, "suspension_damping_ratio", b3WheelJoint_GetSuspensionDampingRatio(joint));
			put_bool(out, "enable_suspension_limit", b3WheelJoint_IsSuspensionLimitEnabled(joint));
			put(out, "lower_suspension_limit", b3WheelJoint_GetLowerSuspensionLimit(joint));
			put(out, "upper_suspension_limit", b3WheelJoint_GetUpperSuspensionLimit(joint));
			put_bool(out, "enable_spin_motor", b3WheelJoint_IsSpinMotorEnabled(joint));
			put(out, "spin_speed", b3WheelJoint_GetSpinMotorSpeed(joint));
			put(out, "max_spin_torque", b3WheelJoint_GetMaxSpinTorque(joint));
			put_bool(out, "enable_steering", b3WheelJoint_IsSteeringEnabled(joint));
			put(out, "steering_hertz", b3WheelJoint_GetSteeringHertz(joint));
			put(out, "steering_damping_ratio", b3WheelJoint_GetSteeringDampingRatio(joint));
			put(out, "max_steering_torque", b3WheelJoint_GetMaxSteeringTorque(joint));
			put_bool(out, "enable_steering_limit", b3WheelJoint_IsSteeringLimitEnabled(joint));
			put(out, "lower_steering_limit", b3WheelJoint_GetLowerSteeringLimit(joint));
			put(out, "upper_steering_limit", b3WheelJoint_GetUpperSteeringLimit(joint));
			put(out, "target_steering_angle", b3WheelJoint_GetTargetSteeringAngle(joint));
			put(out, "current_spin_speed", b3WheelJoint_GetSpinSpeed(joint));
			put(out, "spin_torque", b3WheelJoint_GetSpinTorque(joint));
			put(out, "steering_angle", b3WheelJoint_GetSteeringAngle(joint));
			put(out, "steering_torque", b3WheelJoint_GetSteeringTorque(joint));
			break;
		case b3_parallelJoint:
			put(out, "hertz", b3ParallelJoint_GetSpringHertz(joint));
			put(out, "damping_ratio", b3ParallelJoint_GetSpringDampingRatio(joint));
			put(out, "max_torque", b3ParallelJoint_GetMaxTorque(joint));
			break;
		default:
			break;
	}
}

#undef EGP_ENUM
#undef EGP_INFO

} // namespace egp::box3d
