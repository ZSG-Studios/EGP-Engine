// SPDX-License-Identifier: MIT
#include "box2d_portable_space.h"

#include "modules/box2d/bodies/box2d_area_2d.h"
#include "modules/box2d/bodies/box2d_body_2d.h"
#include "modules/box2d/box2d_physics_server_2d.h"
#include "modules/box2d/joints/box2d_configured_joint_2d.h"
#include "modules/box2d/joints/box2d_damped_spring_joint_2d.h"
#include "modules/box2d/joints/box2d_groove_joint_2d.h"
#include "modules/box2d/joints/box2d_joint_2d.h"
#include "modules/box2d/joints/box2d_pin_joint_2d.h"
#include "modules/box2d/simulation_guard.h"
#include "modules/box2d/spaces/box2d_space_2d.h"

#include "core/crypto/crypto_core.h"
#include "core/io/marshalls.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/pair.h"

#ifdef EGP_BOX2D_PORTABLE
#include "thirdparty/box2d/src/portable/portable_facade.h"
#endif

#include <cstring>
#include <mutex>

// Engine callbacks installed by Box2DSpace2D (spaces/box2d_space_2d.cpp).
bool box2d_godot_presolve(b2ShapeId shapeIdA, b2ShapeId shapeIdB, b2Pos point, b2Vec2 normal, void *context);
real_t godot_friction_callback(real_t frictionA, uint64_t materialA, real_t frictionB, uint64_t materialB);
real_t godot_restitution_callback(real_t restitutionA, uint64_t materialA, real_t restitutionB, uint64_t materialB);

#ifdef EGP_BOX2D_PORTABLE
namespace {

constexpr uint8_t kMagic[8] = { 'E', 'G', 'P', 'B', '2', 'S', 'P', '1' };
constexpr uint32_t kVersion = 2;
constexpr uint64_t kSymbolBox2DFriction = 1, kSymbolBox2DRestitution = 2, kSymbolGodotFriction = 3, kSymbolGodotRestitution = 4,
				   kSymbolGodotPresolve = 5, kSymbolSpace = 16, kSymbolObject = 0x10000000ull, kSymbolShape = 0x20000000ull, kSymbolJoint = 0x30000000ull;
constexpr uint32_t kNone = UINT32_MAX;
constexpr uint32_t kMaxObjects = 65536, kMaxShapes = 65536;
// Source line of the most recent capture refusal, reported for diagnosis.
int last_refusal = 0;
#define SP_PORTABLE_FAIL(e) do { last_refusal = __LINE__; return e; } while (0)

struct PortableState {
	SpB2PortableState *registries = nullptr;
	SpB2CaptureStats stats = {};
	bool requested = false;
	bool has_pending = false;
	Error pending_error = OK;
	PackedByteArray pending;
};
HashMap<Box2DSpace2D *, PortableState *> &states() {
	static HashMap<Box2DSpace2D *, PortableState *> map;
	return map;
}
PortableState *state_for(Box2DSpace2D *p_space) {
	auto &map = states();
	if (PortableState **found = map.getptr(p_space)) {
		return *found;
	}
	PortableState *state = memnew(PortableState);
	state->registries = spB2PortableCreateState();
	if (!state->registries) {
		memdelete(state);
		return nullptr;
	}
	map.insert(p_space, state);
	return state;
}

struct Writer {
	LocalVector<uint8_t> bytes;
	void u8(uint8_t v) { bytes.push_back(v); }
	void u32(uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			bytes.push_back(uint8_t(v >> (8 * i)));
		}
	}
	void u64(uint64_t v) {
		for (int i = 0; i < 8; ++i) {
			bytes.push_back(uint8_t(v >> (8 * i)));
		}
	}
	void f32(float v) {
		uint32_t b;
		std::memcpy(&b, &v, 4);
		u32(b);
	}
	void f64(double v) {
		uint64_t b;
		std::memcpy(&b, &v, 8);
		u64(b);
	}
	void boolean(bool v) { u8(v ? 1 : 0); }
	void vec2(const Vector2 &v) {
		f32(v.x);
		f32(v.y);
	}
	void bvec2(const b2Vec2 &v) {
		f32(v.x);
		f32(v.y);
	}
	void pos(const b2Pos &v) {
		if constexpr (sizeof(v.x) == 8) {
			f64(v.x);
			f64(v.y);
		} else {
			f32(float(v.x));
			f32(float(v.y));
		}
	}
	void xform(const Transform2D &t) {
		vec2(t.columns[0]);
		vec2(t.columns[1]);
		vec2(t.columns[2]);
	}
	void raw(const uint8_t *p, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			bytes.push_back(p[i]);
		}
	}
};

struct Reader {
	const uint8_t *p = nullptr;
	size_t left = 0;
	bool ok = true;
	bool need(size_t n) {
		if (!ok || n > left) {
			ok = false;
			return false;
		}
		return true;
	}
	uint8_t u8() {
		if (!need(1)) {
			return 0;
		}
		uint8_t v = *p++;
		--left;
		return v;
	}
	uint32_t u32() {
		if (!need(4)) {
			return 0;
		}
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			v |= uint32_t(p[i]) << (8 * i);
		}
		p += 4;
		left -= 4;
		return v;
	}
	uint64_t u64() {
		if (!need(8)) {
			return 0;
		}
		uint64_t v = 0;
		for (int i = 0; i < 8; ++i) {
			v |= uint64_t(p[i]) << (8 * i);
		}
		p += 8;
		left -= 8;
		return v;
	}
	float f32() {
		uint32_t b = u32();
		float v;
		std::memcpy(&v, &b, 4);
		return v;
	}
	double f64() {
		uint64_t b = u64();
		double v;
		std::memcpy(&v, &b, 8);
		return v;
	}
	bool boolean() {
		uint8_t v = u8();
		if (v > 1) {
			ok = false;
		}
		return v == 1;
	}
	Vector2 vec2() {
		float x = f32();
		float y = f32();
		return Vector2(x, y);
	}
	b2Vec2 bvec2() {
		float x = f32();
		float y = f32();
		return b2Vec2{ x, y };
	}
	b2Pos pos() {
		b2Pos v;
		if constexpr (sizeof(v.x) == 8) {
			v.x = f64();
			v.y = f64();
		} else {
			v.x = f32();
			v.y = f32();
		}
		return v;
	}
	Transform2D xform() {
		Transform2D t;
		t.columns[0] = vec2();
		t.columns[1] = vec2();
		t.columns[2] = vec2();
		return t;
	}
};

void default_box2d_callbacks(void *&r_friction, void *&r_restitution) {
	static bool probed = false;
	static void *friction = nullptr, *restitution = nullptr;
	if (!probed) {
		b2WorldDef def = b2DefaultWorldDef();
		def.workerCount = 1;
		b2WorldId probe = b2CreateWorld(&def);
		void *root[7] = {};
		if (spB2PortableRootPointers(probe, root)) {
			friction = root[0];
			restitution = root[1];
		}
		b2DestroyWorld(probe);
		probed = true;
	}
	r_friction = friction;
	r_restitution = restitution;
}

void put_symbol(LocalVector<SpB2Symbol> &r_symbols, uint64_t p_symbol, void *p_pointer) {
	for (const SpB2Symbol &s : r_symbols) {
		if (s.pointer == p_pointer) {
			return;
		}
	}
	r_symbols.push_back(SpB2Symbol{ p_symbol, p_pointer });
}

} // namespace

class Box2DPortableSpaceAccess {
public:
	static uint32_t native_slot(b2BodyId p_id) { return B2_IS_NULL(p_id) ? kNone : uint32_t(p_id.index1 - 1); }
	static uint32_t native_slot(b2ShapeId p_id) { return B2_IS_NULL(p_id) ? kNone : uint32_t(p_id.index1 - 1); }
	static uint32_t native_slot(b2JointId p_id) { return B2_IS_NULL(p_id) ? kNone : uint32_t(p_id.index1 - 1); }

	// Native joints attached to live bodies, deduplicated, in joint slot order.
	static void native_joints(b2WorldId p_world, LocalVector<b2JointId> &r_joints) {
		r_joints.clear();
		const uint32_t bodies = spB2PortableBodySlots(p_world);
		LocalVector<b2JointId> scratch;
		for (uint32_t slot = 0; slot < bodies; ++slot) {
			b2BodyId id = b2_nullBodyId;
			if (!spB2PortableBodyAt(p_world, slot, &id, nullptr)) {
				continue;
			}
			const int count = b2Body_GetJointCount(id);
			if (count <= 0) {
				continue;
			}
			scratch.resize(uint32_t(count));
			const int written = b2Body_GetJoints(id, scratch.ptr(), count);
			for (int i = 0; i < written; ++i) {
				bool seen = false;
				for (b2JointId j : r_joints) {
					if (B2_ID_EQUALS(j, scratch[i])) {
						seen = true;
						break;
					}
				}
				if (!seen) {
					r_joints.push_back(scratch[i]);
				}
			}
		}
		struct JointSlotOrder {
			bool operator()(const b2JointId &a, const b2JointId &b) const { return a.index1 < b.index1; }
		};
		r_joints.sort_custom<JointSlotOrder>();
	}

	// Names every pointer the native world holds: callbacks, the pre-solve
	// context (the space), each collision object by its native body slot, each
	// shape instance by its first native shape slot and each joint wrapper by
	// its native joint slot.
	static Error symbols(Box2DSpace2D *p_space, LocalVector<SpB2Symbol> &r_symbols) {
		r_symbols.clear();
		void *friction = nullptr, *restitution = nullptr;
		default_box2d_callbacks(friction, restitution);
		void *root[7] = {};
		if (!spB2PortableRootPointers(p_space->world_id, root)) {
			SP_PORTABLE_FAIL(ERR_INVALID_DATA);
		}
		for (void *pointer : root) {
			if (!pointer) {
				continue;
			}
			if (pointer == friction) {
				put_symbol(r_symbols, kSymbolBox2DFriction, pointer);
			} else if (pointer == restitution) {
				put_symbol(r_symbols, kSymbolBox2DRestitution, pointer);
			} else if (pointer == reinterpret_cast<void *>(&godot_friction_callback)) {
				put_symbol(r_symbols, kSymbolGodotFriction, pointer);
			} else if (pointer == reinterpret_cast<void *>(&godot_restitution_callback)) {
				put_symbol(r_symbols, kSymbolGodotRestitution, pointer);
			} else if (pointer == reinterpret_cast<void *>(&box2d_godot_presolve)) {
				put_symbol(r_symbols, kSymbolGodotPresolve, pointer);
			} else if (pointer == p_space) {
				put_symbol(r_symbols, kSymbolSpace, pointer);
			} else {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
		}
		const uint32_t bodies = spB2PortableBodySlots(p_space->world_id);
		for (uint32_t slot = 0; slot < bodies; ++slot) {
			void *user = nullptr;
			if (spB2PortableBodyAt(p_space->world_id, slot, nullptr, &user) && user) {
				if (!p_space->objects.has(static_cast<Box2DCollisionObject2D *>(user))) {
					SP_PORTABLE_FAIL(ERR_INVALID_DATA);
				}
				put_symbol(r_symbols, kSymbolObject + slot, user);
			}
		}
		const uint32_t shapes = spB2PortableShapeSlots(p_space->world_id);
		for (uint32_t slot = 0; slot < shapes; ++slot) {
			void *user = nullptr;
			if (spB2PortableShapeAt(p_space->world_id, slot, nullptr, &user) && user) {
				put_symbol(r_symbols, kSymbolShape + slot, user);
			}
		}
		LocalVector<b2JointId> joints;
		native_joints(p_space->world_id, joints);
		for (b2JointId joint : joints) {
			if (void *user = b2Joint_GetUserData(joint)) {
				put_symbol(r_symbols, kSymbolJoint + native_slot(joint), user);
			}
		}
		return OK;
	}

	static void write_body_def(Writer &w, const b2BodyDef &d) {
		w.u32(uint32_t(d.type));
		w.pos(d.position);
		w.f32(d.rotation.c);
		w.f32(d.rotation.s);
		w.bvec2(d.linearVelocity);
		w.f32(d.angularVelocity);
		w.f32(d.linearDamping);
		w.f32(d.angularDamping);
		w.f32(d.gravityScale);
		w.f32(d.sleepThreshold);
		w.boolean(d.motionLocks.linearX);
		w.boolean(d.motionLocks.linearY);
		w.boolean(d.motionLocks.angularZ);
		w.boolean(d.enableSleep);
		w.boolean(d.isAwake);
		w.boolean(d.isBullet);
		w.boolean(d.isEnabled);
		w.boolean(d.allowFastRotation);
		w.boolean(d.enableContactRecycling);
		w.u32(uint32_t(d.internalValue));
	}
	static b2BodyDef read_body_def(Reader &r) {
		b2BodyDef d = b2DefaultBodyDef();
		d.type = b2BodyType(r.u32());
		d.position = r.pos();
		d.rotation.c = r.f32();
		d.rotation.s = r.f32();
		d.linearVelocity = r.bvec2();
		d.angularVelocity = r.f32();
		d.linearDamping = r.f32();
		d.angularDamping = r.f32();
		d.gravityScale = r.f32();
		d.sleepThreshold = r.f32();
		d.motionLocks.linearX = r.boolean();
		d.motionLocks.linearY = r.boolean();
		d.motionLocks.angularZ = r.boolean();
		d.enableSleep = r.boolean();
		d.isAwake = r.boolean();
		d.isBullet = r.boolean();
		d.isEnabled = r.boolean();
		d.allowFastRotation = r.boolean();
		d.enableContactRecycling = r.boolean();
		d.internalValue = int(r.u32());
		if (uint32_t(d.type) > uint32_t(b2_dynamicBody)) {
			r.ok = false;
		}
		return d;
	}
	static void write_shape_def(Writer &w, const b2ShapeDef &d) {
		w.f32(d.material.friction);
		w.f32(d.material.restitution);
		w.f32(d.material.rollingResistance);
		w.f32(d.material.tangentSpeed);
		w.u64(d.material.userMaterialId);
		w.u32(d.material.customColor);
		w.f32(d.density);
		w.u64(d.filter.categoryBits);
		w.u64(d.filter.maskBits);
		w.u32(uint32_t(d.filter.groupIndex));
		w.boolean(d.enableCustomFiltering);
		w.boolean(d.isSensor);
		w.boolean(d.enableSensorEvents);
		w.boolean(d.enableContactEvents);
		w.boolean(d.enableHitEvents);
		w.boolean(d.enablePreSolveEvents);
		w.boolean(d.invokeContactCreation);
		w.boolean(d.updateBodyMass);
		w.u32(uint32_t(d.internalValue));
	}
	static b2ShapeDef read_shape_def(Reader &r) {
		b2ShapeDef d = b2DefaultShapeDef();
		d.material.friction = r.f32();
		d.material.restitution = r.f32();
		d.material.rollingResistance = r.f32();
		d.material.tangentSpeed = r.f32();
		d.material.userMaterialId = r.u64();
		d.material.customColor = r.u32();
		d.density = r.f32();
		d.filter.categoryBits = r.u64();
		d.filter.maskBits = r.u64();
		d.filter.groupIndex = int(r.u32());
		d.enableCustomFiltering = r.boolean();
		d.isSensor = r.boolean();
		d.enableSensorEvents = r.boolean();
		d.enableContactEvents = r.boolean();
		d.enableHitEvents = r.boolean();
		d.enablePreSolveEvents = r.boolean();
		d.invokeContactCreation = r.boolean();
		d.updateBodyMass = r.boolean();
		d.internalValue = int(r.u32());
		d.userData = nullptr;
		return d;
	}

	// Joint definition base: bodies by native slot, frames and tuning. User
	// data and body ids are rebound on restore.
	static void write_joint_base(Writer &w, const b2JointDef &d) {
		w.u32(native_slot(d.bodyIdA));
		w.u32(native_slot(d.bodyIdB));
		w.bvec2(d.localFrameA.p);
		w.f32(d.localFrameA.q.c);
		w.f32(d.localFrameA.q.s);
		w.bvec2(d.localFrameB.p);
		w.f32(d.localFrameB.q.c);
		w.f32(d.localFrameB.q.s);
		w.f32(d.forceThreshold);
		w.f32(d.torqueThreshold);
		w.f32(d.constraintHertz);
		w.f32(d.constraintDampingRatio);
		w.f32(d.drawScale);
		w.boolean(d.collideConnected);
	}
	static void read_joint_base(Reader &r, b2JointDef &d, uint32_t &r_a, uint32_t &r_b) {
		r_a = r.u32();
		r_b = r.u32();
		d.localFrameA.p = r.bvec2();
		d.localFrameA.q.c = r.f32();
		d.localFrameA.q.s = r.f32();
		d.localFrameB.p = r.bvec2();
		d.localFrameB.q.c = r.f32();
		d.localFrameB.q.s = r.f32();
		d.forceThreshold = r.f32();
		d.torqueThreshold = r.f32();
		d.constraintHertz = r.f32();
		d.constraintDampingRatio = r.f32();
		d.drawScale = r.f32();
		d.collideConnected = r.boolean();
		d.userData = nullptr;
		d.bodyIdA = b2_nullBodyId;
		d.bodyIdB = b2_nullBodyId;
	}

	static void write_string(Writer &w, const String &p_text) {
		const CharString utf8 = p_text.utf8();
		w.u32(uint32_t(utf8.length()));
		w.raw(reinterpret_cast<const uint8_t *>(utf8.get_data()), size_t(utf8.length()));
	}
	static bool read_string(Reader &r, String &r_text) {
		const uint32_t length = r.u32();
		if (!r.ok || length > 4096 || !r.need(length)) {
			return false;
		}
		r_text = String::utf8(reinterpret_cast<const char *>(r.p), int(length));
		r.p += length;
		r.left -= length;
		return true;
	}

	// User data: 0 nil, 1 object (by instance id symbol), 2 encoded non-object Variant.
	static Error write_user_data(Writer &w, const Variant &p_data) {
		switch (p_data.get_type()) {
			case Variant::NIL:
				w.u8(0);
				return OK;
			case Variant::OBJECT: {
				Object *object = p_data.get_validated_object();
				if (!object) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				w.u8(1);
				w.u64(uint64_t(object->get_instance_id()));
				return OK;
			}
			case Variant::BOOL:
			case Variant::INT:
			case Variant::FLOAT:
			case Variant::STRING:
			case Variant::STRING_NAME:
			case Variant::VECTOR2:
			case Variant::VECTOR2I: {
				int length = 0;
				if (encode_variant(p_data, nullptr, length, false) != OK || length < 0 || length > 65536) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				LocalVector<uint8_t> buffer;
				buffer.resize(uint32_t(length));
				if (encode_variant(p_data, buffer.ptr(), length, false) != OK) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				w.u8(2);
				w.u32(uint32_t(length));
				w.raw(buffer.ptr(), uint32_t(length));
				return OK;
			}
			default:
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
		}
	}

	static bool map_object(const Dictionary &p_map, uint64_t p_source, ObjectID &r_out) {
		if (!p_source) {
			r_out = ObjectID();
			return true;
		}
		const Variant key = int64_t(p_source);
		if (!p_map.has(key)) {
			return false;
		}
		const Variant value = p_map[key];
		if (value.get_type() == Variant::OBJECT) {
			Object *object = value.get_validated_object();
			if (!object) {
				return false;
			}
			r_out = object->get_instance_id();
			return true;
		}
		if (value.get_type() == Variant::INT) {
			r_out = ObjectID(uint64_t(int64_t(value)));
			return true;
		}
		return false;
	}

	static Error read_user_data(Reader &r, const Dictionary &p_map, Variant &r_data) {
		const uint8_t tag = r.u8();
		if (tag == 0) {
			r_data = Variant();
			return r.ok ? OK : ERR_FILE_CORRUPT;
		}
		if (tag == 1) {
			ObjectID id;
			if (!map_object(p_map, r.u64(), id) || !r.ok) {
				return ERR_DOES_NOT_EXIST;
			}
			Object *object = ObjectDB::get_instance(id);
			if (!object) {
				return ERR_DOES_NOT_EXIST;
			}
			r_data = object;
			return OK;
		}
		if (tag == 2) {
			const uint32_t length = r.u32();
			if (!r.need(length) || length > 65536) {
				return ERR_FILE_CORRUPT;
			}
			Variant value;
			int used = 0;
			if (decode_variant(value, r.p, int(length), &used, false) != OK || used != int(length) || value.get_type() == Variant::OBJECT) {
				return ERR_FILE_CORRUPT;
			}
			r.p += length;
			r.left -= length;
			r_data = value;
			return OK;
		}
		return ERR_FILE_CORRUPT;
	}

	// Callbacks travel as symbols: the role, the owning wrapper's source id, the
	// target object's instance id and, for method callables, the method name.
	struct CallablePlan {
		bool present = false, custom = false;
		uint64_t object = 0;
		String method;
	};
	static void write_callable(Writer &w, const Callable &p_callable) {
		if (p_callable.is_null()) {
			w.u8(0);
			return;
		}
		w.u8(1);
		w.boolean(p_callable.is_custom());
		w.u64(uint64_t(p_callable.get_object_id()));
		write_string(w, p_callable.is_custom() ? String() : String(p_callable.get_method()));
	}
	static bool read_callable(Reader &r, CallablePlan &r_plan) {
		const uint8_t present = r.u8();
		if (present > 1) {
			return false;
		}
		r_plan.present = present == 1;
		if (!r_plan.present) {
			return r.ok;
		}
		r_plan.custom = r.boolean();
		r_plan.object = r.u64();
		return read_string(r, r_plan.method) && r.ok;
	}
	// Explicit callable_map entry first, then a method callable on the mapped
	// object. Required callbacks fail with ERR_DOES_NOT_EXIST when unresolvable;
	// optional ones (state sync) resolve to null.
	static Error resolve_callable(const CallablePlan &p_plan, const String &p_key, const Dictionary &p_objects, const Dictionary &p_callables, bool p_required, Callable &r_out) {
		r_out = Callable();
		if (!p_plan.present) {
			return OK;
		}
		if (p_callables.has(p_key)) {
			const Variant value = p_callables[p_key];
			if (value.get_type() != Variant::CALLABLE) {
				return ERR_DOES_NOT_EXIST;
			}
			r_out = value;
			return OK;
		}
		ObjectID target;
		if (!p_plan.custom && !p_plan.method.is_empty() && map_object(p_objects, p_plan.object, target)) {
			Object *object = ObjectDB::get_instance(target);
			if (object) {
				r_out = Callable(object, StringName(p_plan.method));
				return OK;
			}
		}
		return p_required ? ERR_DOES_NOT_EXIST : OK;
	}

	static Error check_profile(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space) {
		if (!p_space->bodies_with_overrides.is_empty() || !p_space->contact_hit_events.is_empty() || !p_space->joint_events.is_empty()) {
			SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
		}
		for (Box2DCollisionObject2D *object : p_space->objects) {
			if (!object || object->_is_freed || object->space != p_space || !b2Body_IsValid(object->body_id) || b2Body_GetUserData(object->body_id) != object) {
				SP_PORTABLE_FAIL(ERR_INVALID_DATA);
			}
			if (object->body_def.name != nullptr) {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
			if (object->is_area()) {
				if (p_server->area_owner.get_or_null(object->rid) != object || (object == p_space->default_area && !object->shapes.is_empty())) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
			} else {
				Box2DBody2D *body = object->as_body();
				if (!body || p_server->body_owner.get_or_null(object->rid) != body) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				for (const RID &other : body->exceptions) {
					Box2DBody2D *peer = p_server->body_owner.get_or_null(other);
					if (!peer || peer->space != p_space) {
						SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
					}
				}
				for (const Box2DBody2D::Contact &contact : body->contacts) {
					Box2DBody2D *peer = p_server->body_owner.get_or_null(contact.collider);
					if (!peer || peer != contact.body || peer->space != p_space || uint32_t(contact.local_shape) >= body->shapes.size() ||
							uint32_t(contact.collider_shape) >= peer->shapes.size()) {
						SP_PORTABLE_FAIL(ERR_INVALID_DATA);
					}
				}
			}
			for (const Box2DShapeInstance &shape : object->shapes) {
				if (shape.object != object || !shape.shape || p_server->shape_owner.get_or_null(shape.shape->get_rid()) != shape.shape) {
					SP_PORTABLE_FAIL(ERR_INVALID_DATA);
				}
				for (b2ShapeId id : shape.shape_ids) {
					if (!b2Shape_IsValid(id) || b2Shape_GetUserData(id) != &shape) {
						SP_PORTABLE_FAIL(ERR_INVALID_DATA);
					}
				}
			}
		}
		// Every native joint is a captured joint wrapper or a collision-exception filter joint.
		LocalVector<b2JointId> joints;
		native_joints(p_space->world_id, joints);
		for (b2JointId joint : joints) {
			void *user = b2Joint_GetUserData(joint);
			if (user) {
				Box2DJoint2D *wrapper = static_cast<Box2DJoint2D *>(user);
				if (p_server->joint_owner.get_or_null(wrapper->rid) != wrapper || wrapper->space != p_space || !B2_ID_EQUALS(wrapper->joint_id, joint)) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				continue;
			}
			bool exception = false;
			for (Box2DBody2D *body : p_space->bodies_with_exceptions) {
				for (b2JointId id : body->exception_joints) {
					exception = exception || B2_ID_EQUALS(id, joint);
				}
			}
			if (!exception) {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
		}
		return OK;
	}

	struct ObjectSlotOrder {
		bool operator()(const Box2DCollisionObject2D *a, const Box2DCollisionObject2D *b) const {
			return native_slot(a->body_id) < native_slot(b->body_id);
		}
	};

	static uint32_t index_in(const LocalVector<Box2DBody2D *> &p_list, const Box2DBody2D *p_body) {
		for (uint32_t i = 0; i < p_list.size(); ++i) {
			if (p_list[i] == p_body) {
				return i;
			}
		}
		return kNone;
	}

	// Joint wrappers owned by this space, in RID order.
	static void space_joints(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, LocalVector<Box2DJoint2D *> &r_joints) {
		r_joints.clear();
		LocalVector<RID> rids;
		for (const RID &rid : p_server->joint_owner.get_owned_list()) {
			rids.push_back(rid);
		}
		rids.sort();
		for (const RID &rid : rids) {
			Box2DJoint2D *joint = p_server->joint_owner.get_or_null(rid);
			if (joint && joint->type != PS2DE::JOINT_TYPE_MAX && ((joint->body_a && joint->body_a->space == p_space) || joint->space == p_space)) {
				r_joints.push_back(joint);
			}
		}
	}

	static Error encode_wrappers(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, Writer &w) {
		w.raw(kMagic, 8);
		w.u32(kVersion);
		w.u32(spB2PortablePositionBits());
		w.u32(uint32_t(sizeof(real_t) * 8));
		w.vec2(p_space->default_gravity);
		w.f32(p_space->contact_hertz);
		w.f32(p_space->contact_damping_ratio);
		w.f32(p_space->contact_max_push_speed);
		w.u32(uint32_t(p_space->substeps));
		w.f32(p_space->last_step);
		w.boolean(p_space->linear_damp_changed);
		w.boolean(p_space->angular_damp_changed);
		w.boolean(p_space->exceptions_dirty);
		w.u32(native_slot(p_space->world_anchor_body));
		LocalVector<Box2DCollisionObject2D *> objects;
		for (Box2DCollisionObject2D *object : p_space->objects) {
			objects.push_back(object);
		}
		objects.sort_custom<ObjectSlotOrder>();
		if (objects.size() > kMaxObjects) {
			SP_PORTABLE_FAIL(ERR_OUT_OF_MEMORY);
		}
		w.u32(objects.size());
		for (Box2DCollisionObject2D *object : objects) {
			const uint8_t kind = object == p_space->default_area ? 1 : object->is_area() ? 2 : 0;
			w.u8(kind);
			w.u32(native_slot(object->body_id));
			w.u64(object->rid.get_id());
			w.u64(uint64_t(object->instance_id));
			w.u64(uint64_t(object->canvas_instance_id));
			w.boolean(object->pickable);
			w.boolean(object->is_animatable_body);
			w.xform(object->current_transform);
			w.u32(uint32_t(object->mode));
			write_body_def(w, object->body_def);
			write_shape_def(w, object->shape_def);
			Error error = write_user_data(w, object->user_data);
			if (error != OK) {
				return error;
			}
			if (object->shapes.size() > kMaxShapes) {
				SP_PORTABLE_FAIL(ERR_OUT_OF_MEMORY);
			}
			w.u32(object->shapes.size());
			for (const Box2DShapeInstance &shape : object->shapes) {
				w.u64(shape.shape->get_rid().get_id());
				w.xform(shape.transform);
				w.boolean(shape.disabled);
				w.boolean(shape.one_way_collision);
				w.vec2(shape.one_way_direction);
				w.f32(shape.one_way_collision_margin);
				w.u32(shape.shape_ids.size());
				for (b2ShapeId id : shape.shape_ids) {
					w.u32(native_slot(id));
				}
			}
			if (kind != 0) {
				const Box2DArea2D *a = object->as_area();
				w.u32(uint32_t(a->priority));
				w.boolean(a->monitorable);
				w.f32(a->gravity_strength);
				w.vec2(a->gravity_direction);
				w.f32(a->linear_damp);
				w.f32(a->angular_damp);
				w.boolean(a->gravity_point_enabled);
				w.f32(a->gravity_point_unit_distance);
				w.u32(uint32_t(a->override_gravity_mode));
				w.u32(uint32_t(a->override_linear_damp_mode));
				w.u32(uint32_t(a->override_angular_damp_mode));
				uint32_t step_index = kNone;
				for (uint32_t i = 0; i < p_space->areas_to_step.size(); ++i) {
					if (p_space->areas_to_step[i] == a) {
						step_index = i;
					}
				}
				w.boolean(a->in_area_step_list);
				w.u32(step_index);
				write_callable(w, a->body_monitor_callback);
				write_callable(w, a->area_monitor_callback);
				// Overlap and per-object counts in their own iteration order.
				w.u32(a->overlaps.size());
				for (const KeyValue<Box2DArea2D::ShapePair, Box2DArea2D::ObjectAndOverlapCount> &entry : a->overlaps) {
					w.u64(entry.key.other_rid.get_id());
					w.u32(uint32_t(entry.key.other_index));
					w.u32(uint32_t(entry.key.self_index));
					w.u64(uint64_t(entry.value.instance_id));
					w.u8(uint8_t(entry.value.type));
					w.u32(uint32_t(entry.value.count));
				}
				w.u32(a->object_overlap_count.size());
				for (const KeyValue<RID, int> &entry : a->object_overlap_count) {
					w.u64(entry.key.get_id());
					w.u32(uint32_t(entry.value));
				}
				continue;
			}
			const Box2DBody2D *b = object->as_body();
			w.f32(b->mass_data.mass);
			w.bvec2(b->mass_data.center);
			w.f32(b->mass_data.rotationalInertia);
			const Box2DBody2D::AreaOverrideAccumulator &o = b->area_overrides;
			w.vec2(o.total_gravity);
			w.boolean(o.skip_world_gravity);
			w.boolean(o.skip_world_linear_damp);
			w.boolean(o.skip_world_angular_damp);
			w.f32(o.total_linear_damp);
			w.f32(o.total_angular_damp);
			w.boolean(o.ignore_remaining_gravity);
			w.boolean(o.ignore_remaining_linear_damp);
			w.boolean(o.ignore_remaining_angular_damp);
			w.vec2(b->constant_force);
			w.f32(b->constant_torque);
			w.vec2(b->total_gravity);
			w.f32(b->total_linear_damp);
			w.f32(b->total_angular_damp);
			w.vec2(b->initial_linear_velocity);
			w.f32(b->initial_angular_velocity);
			w.boolean(b->use_static_velocities);
			w.vec2(b->static_linear_velocity);
			w.f32(b->static_angular_velocity);
			w.boolean(b->sleeping);
			w.boolean(b->queried_contacts);
			w.boolean(b->omit_force_integration);
			w.f32(b->linear_damping);
			w.f32(b->angular_damping);
			w.f32(b->mass);
			w.f32(b->inertia);
			w.vec2(b->center_of_mass);
			w.boolean(b->override_center_of_mass);
			w.boolean(b->override_inertia);
			w.u32(uint32_t(b->linear_damp_mode));
			w.u32(uint32_t(b->angular_damp_mode));
			w.f32(b->contact_depth_threshold);
			w.boolean(b->contact_ignore_speculative);
			w.f32(b->character_collision_priority);
			w.boolean(b->in_constant_forces_list);
			w.u32(index_in(p_space->constant_force_list, b));
			// Collision exceptions and their native filter joints.
			LocalVector<uint64_t> exceptions;
			for (const RID &other : b->exceptions) {
				exceptions.push_back(other.get_id());
			}
			exceptions.sort();
			w.u32(exceptions.size());
			for (uint64_t id : exceptions) {
				w.u64(id);
			}
			w.boolean(p_space->bodies_with_exceptions.has(const_cast<Box2DBody2D *>(b)));
			w.u32(b->exception_joints.size());
			for (b2JointId id : b->exception_joints) {
				w.u32(native_slot(id));
			}
			// Reported contacts.
			w.u32(uint32_t(b->max_contact_count));
			w.u32(b->contacts.size());
			for (const Box2DBody2D::Contact &c : b->contacts) {
				w.u64(c.collider.get_id());
				w.f32(c.normal_impulse);
				w.vec2(c.local_position);
				w.vec2(c.local_normal);
				w.f32(c.depth);
				w.u32(uint32_t(c.local_shape));
				w.vec2(c.collider_position);
				w.u32(uint32_t(c.collider_shape));
				w.u64(uint64_t(c.collider_instance_id));
				w.vec2(c.collider_velocity);
				w.vec2(c.impulse);
			}
			// Callbacks and force integration.
			write_callable(w, b->body_state_callback);
			write_callable(w, b->force_integration_callback);
			error = write_user_data(w, b->force_integration_user_data);
			if (error != OK) {
				return error;
			}
			w.boolean(b->in_force_integration_list);
			w.u32(index_in(p_space->force_integration_list, b));
		}
		w.u32(p_space->constant_force_list.size());
		w.u32(p_space->force_integration_list.size());
		w.u32(p_space->areas_to_step.size());
		// Joints.
		LocalVector<Box2DJoint2D *> joints;
		space_joints(p_server, p_space, joints);
		w.u32(joints.size());
		for (Box2DJoint2D *j : joints) {
			w.u32(uint32_t(j->type));
			w.u64(j->rid.get_id());
			w.u32(j->body_a ? native_slot(j->body_a->body_id) : kNone);
			w.u32(j->body_b ? native_slot(j->body_b->body_id) : kNone);
			w.boolean(j->space == p_space);
			w.u32(native_slot(j->joint_id));
			w.f32(j->hertz);
			w.f32(j->damping_ratio);
			w.boolean(j->disabled_collisions_between_bodies);
			switch (j->type) {
				case PS2DE::JOINT_TYPE_PIN: {
					const b2RevoluteJointDef &d = static_cast<Box2DPinJoint2D *>(j)->revolute_def;
					write_joint_base(w, d.base);
					w.f32(d.targetAngle);
					w.boolean(d.enableSpring);
					w.f32(d.hertz);
					w.f32(d.dampingRatio);
					w.boolean(d.enableLimit);
					w.f32(d.lowerAngle);
					w.f32(d.upperAngle);
					w.boolean(d.enableMotor);
					w.f32(d.maxMotorTorque);
					w.f32(d.motorSpeed);
					w.u32(uint32_t(d.internalValue));
				} break;
				case PS2DE::JOINT_TYPE_GROOVE: {
					const b2WheelJointDef &d = static_cast<Box2DGrooveJoint2D *>(j)->wheel_def;
					write_joint_base(w, d.base);
					w.boolean(d.enableSpring);
					w.f32(d.hertz);
					w.f32(d.dampingRatio);
					w.boolean(d.enableLimit);
					w.f32(d.lowerTranslation);
					w.f32(d.upperTranslation);
					w.boolean(d.enableMotor);
					w.f32(d.maxMotorTorque);
					w.f32(d.motorSpeed);
					w.u32(uint32_t(d.internalValue));
				} break;
				case PS2DE::JOINT_TYPE_DAMPED_SPRING: {
					const Box2DDampedSpringJoint2D *spring = static_cast<Box2DDampedSpringJoint2D *>(j);
					const b2DistanceJointDef &d = spring->distance_def;
					write_joint_base(w, d.base);
					w.f32(d.length);
					w.boolean(d.enableSpring);
					w.f32(d.lowerSpringForce);
					w.f32(d.upperSpringForce);
					w.f32(d.hertz);
					w.f32(d.dampingRatio);
					w.boolean(d.enableLimit);
					w.f32(d.minLength);
					w.f32(d.maxLength);
					w.boolean(d.enableMotor);
					w.f32(d.maxMotorForce);
					w.f32(d.motorSpeed);
					w.u32(uint32_t(d.internalValue));
					w.f32(spring->stiffness);
				} break;
				default: {
					if (j->type < PS2DE::JOINT_TYPE_DISTANCE || j->type > PS2DE::JOINT_TYPE_WHEEL) {
						SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
					}
					const Box2DConfiguredJoint2D *c = static_cast<Box2DConfiguredJoint2D *>(j);
					w.xform(c->frame_a);
					w.xform(c->frame_b);
					int length = 0;
					if (encode_variant(c->configuration, nullptr, length, false) != OK || length < 0 || length > 65536) {
						SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
					}
					LocalVector<uint8_t> buffer;
					buffer.resize(uint32_t(length));
					encode_variant(c->configuration, buffer.ptr(), length, false);
					w.u32(uint32_t(length));
					w.raw(buffer.ptr(), uint32_t(length));
				} break;
			}
		}
		return OK;
	}

	static Error capture_bytes(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, bool p_at_boundary, PackedByteArray &r_bytes) {
		if (!Thread::is_main_thread()) {
			SP_PORTABLE_FAIL(ERR_BUSY);
		}
		if ((!p_at_boundary && p_server->flushing_queries) || p_space->locked || p_space->replay_pending_events ||
				!p_server->bodies_to_delete.is_empty() || !p_server->areas_to_delete.is_empty()) {
			SP_PORTABLE_FAIL(ERR_BUSY);
		}
		Error error = check_profile(p_server, p_space);
		if (error != OK) {
			return error;
		}
		LocalVector<SpB2Symbol> table;
		error = symbols(p_space, table);
		if (error != OK) {
			return error;
		}
		Writer w;
		error = encode_wrappers(p_server, p_space, w);
		if (error != OK) {
			return error;
		}
		PortableState *state = state_for(p_space);
		if (!state) {
			SP_PORTABLE_FAIL(ERR_OUT_OF_MEMORY);
		}
		struct Sink {
			static void append(void *context, const uint8_t *bytes, size_t size) {
				LocalVector<uint8_t> &out = *static_cast<LocalVector<uint8_t> *>(context);
				out.clear();
				out.resize(uint32_t(size));
				std::memcpy(out.ptr(), bytes, size);
			}
		};
		LocalVector<uint8_t> native;
		SpB2CaptureStats stats = {};
		const SpB2PortableStatus status = spB2PortableCapture(p_space->world_id, state->registries, table.ptr(), table.size(), &Sink::append, &native, &stats);
		if (status != SP_B2_PORTABLE_OK) {
			last_refusal = 100000 + int(status) * 1000 + int(stats.failure_line);
			return status == SP_B2_PORTABLE_BUSY ? ERR_BUSY : status == SP_B2_PORTABLE_UNKNOWN_SYMBOL ? ERR_UNAVAILABLE : status == SP_B2_PORTABLE_NO_MEMORY ? ERR_OUT_OF_MEMORY : ERR_INVALID_DATA;
		}
		state->stats = stats;
		w.u64(native.size());
		w.raw(native.ptr(), native.size());
		uint8_t hash[32];
		if (CryptoCore::sha256(w.bytes.ptr(), w.bytes.size(), hash) != OK) {
			SP_PORTABLE_FAIL(ERR_BUG);
		}
		w.raw(hash, 32);
		r_bytes.resize(int64_t(w.bytes.size()));
		std::memcpy(r_bytes.ptrw(), w.bytes.ptr(), w.bytes.size());
		return OK;
	}

	struct ShapePlan {
		Box2DShape2D *shape = nullptr;
		Transform2D transform;
		bool disabled = false, one_way = false;
		Vector2 one_way_direction;
		real_t one_way_margin = 0;
		LocalVector<uint32_t> native;
	};
	struct ContactPlan {
		uint64_t collider = 0;
		Box2DBody2D::Contact contact;
	};
	struct OverlapPlan {
		uint64_t other = 0;
		int other_index = 0, self_index = 0, count = 0;
		ObjectID instance;
		uint8_t type = 0;
	};
	struct ObjectPlan {
		uint8_t kind = 0;
		uint32_t slot = kNone;
		uint64_t source_rid = 0;
		ObjectID instance, canvas;
		bool pickable = true, animatable = false;
		Transform2D transform;
		PS2DE::BodyMode mode = PS2DE::BODY_MODE_STATIC;
		b2BodyDef body_def;
		b2ShapeDef shape_def;
		Variant user_data;
		LocalVector<ShapePlan> shapes;
		// Area.
		int priority = 0;
		bool monitorable = false, gravity_point = false, in_step_list = false;
		real_t gravity_strength = 0, linear_damp = 0, angular_damp = 0, unit_distance = 0;
		Vector2 gravity_direction;
		uint32_t gravity_mode = 0, linear_mode = 0, angular_mode = 0, step_index = kNone;
		CallablePlan body_monitor, area_monitor;
		LocalVector<OverlapPlan> overlaps;
		LocalVector<Pair<uint64_t, int>> overlap_counts;
		// Body.
		b2MassData mass_data = {};
		Box2DBody2D::AreaOverrideAccumulator overrides;
		Vector2 constant_force, total_gravity, initial_linear_velocity, static_linear_velocity, center_of_mass;
		real_t constant_torque = 0, total_linear_damp = 0, total_angular_damp = 0, initial_angular_velocity = 0, static_angular_velocity = 0;
		real_t linear_damping = 0, angular_damping = 0, mass = 0, inertia = 0, contact_depth_threshold = 0, character_priority = 0;
		bool use_static_velocities = false, sleeping = false, queried_contacts = false, omit_force_integration = false;
		bool override_center_of_mass = false, override_inertia = false, contact_ignore_speculative = true, in_force_list = false;
		uint32_t linear_damp_mode = 0, angular_damp_mode = 0, force_queue = kNone;
		LocalVector<uint64_t> exceptions;
		bool with_exceptions = false;
		LocalVector<uint32_t> exception_joints;
		int max_contacts = 0;
		LocalVector<ContactPlan> contacts;
		CallablePlan state_sync, integration;
		Variant integration_data;
		bool in_integration_list = false;
		uint32_t integration_index = kNone;
		Box2DCollisionObject2D *created = nullptr;
	};
	struct JointPlan {
		PS2DE::JointType type = PS2DE::JOINT_TYPE_MAX;
		uint64_t source_rid = 0;
		uint32_t body_a = kNone, body_b = kNone, native = kNone, def_a = kNone, def_b = kNone;
		bool in_space = false, disabled = true;
		real_t hertz = -1, damping = -1;
		b2RevoluteJointDef revolute = b2DefaultRevoluteJointDef();
		b2WheelJointDef wheel = b2DefaultWheelJointDef();
		b2DistanceJointDef distance = b2DefaultDistanceJointDef();
		real_t stiffness = 0;
		Transform2D frame_a, frame_b;
		Dictionary configuration;
		Box2DJoint2D *created = nullptr;
	};
	struct SpacePlan {
		Vector2 gravity;
		float contact[3] = {};
		int substeps = 0;
		real_t last_step = 0;
		bool damp[2] = {}, exceptions_dirty = false;
		uint32_t anchor = kNone, force_count = 0, integration_count = 0, area_count = 0;
		LocalVector<ObjectPlan> objects;
		LocalVector<JointPlan> joints;
		const uint8_t *native = nullptr;
		size_t native_size = 0;
	};

	static Error parse(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_objects, const Dictionary &p_shapes, SpacePlan &r) {
		const size_t size = size_t(p_bytes.size());
		if (size < 8 + 12 + 32) {
			return ERR_FILE_CORRUPT;
		}
		const uint8_t *data = p_bytes.ptr();
		if (std::memcmp(data, kMagic, 8) != 0) {
			return ERR_FILE_UNRECOGNIZED;
		}
		uint8_t hash[32];
		if (CryptoCore::sha256(data, size - 32, hash) != OK || std::memcmp(hash, data + size - 32, 32) != 0) {
			return ERR_FILE_CORRUPT;
		}
		Reader in{ data + 8, size - 8 - 32 };
		if (in.u32() != kVersion || in.u32() != spB2PortablePositionBits() || in.u32() != uint32_t(sizeof(real_t) * 8)) {
			return ERR_FILE_UNRECOGNIZED;
		}
		r.gravity = in.vec2();
		r.contact[0] = in.f32();
		r.contact[1] = in.f32();
		r.contact[2] = in.f32();
		r.substeps = int(in.u32());
		r.last_step = in.f32();
		r.damp[0] = in.boolean();
		r.damp[1] = in.boolean();
		r.exceptions_dirty = in.boolean();
		r.anchor = in.u32();
		const uint32_t count = in.u32();
		if (!in.ok || count > kMaxObjects) {
			return ERR_FILE_CORRUPT;
		}
		r.objects.resize(count);
		uint32_t default_areas = 0, prior = 0;
		for (uint32_t i = 0; i < count; ++i) {
			ObjectPlan &p = r.objects[i];
			p.kind = in.u8();
			if (p.kind > 2) {
				return ERR_FILE_CORRUPT;
			}
			default_areas += p.kind == 1 ? 1 : 0;
			p.slot = in.u32();
			if (p.slot == kNone || (i && p.slot <= prior)) {
				return ERR_FILE_CORRUPT;
			}
			prior = p.slot;
			p.source_rid = in.u64();
			if (!map_object(p_objects, in.u64(), p.instance) || !map_object(p_objects, in.u64(), p.canvas)) {
				return ERR_DOES_NOT_EXIST;
			}
			p.pickable = in.boolean();
			p.animatable = in.boolean();
			p.transform = in.xform();
			p.mode = PS2DE::BodyMode(in.u32());
			p.body_def = read_body_def(in);
			p.shape_def = read_shape_def(in);
			Error error = read_user_data(in, p_objects, p.user_data);
			if (error != OK) {
				return error;
			}
			if (uint32_t(p.mode) > uint32_t(PS2DE::BODY_MODE_RIGID_LINEAR)) {
				return ERR_FILE_CORRUPT;
			}
			const uint32_t shapes = in.u32();
			if (!in.ok || shapes > kMaxShapes) {
				return ERR_FILE_CORRUPT;
			}
			p.shapes.resize(shapes);
			for (uint32_t s = 0; s < shapes; ++s) {
				ShapePlan &sp = p.shapes[s];
				const uint64_t source = in.u64();
				RID resource;
				if (p_shapes.is_empty()) {
					resource = RID::from_uint64(source);
				} else {
					const Variant key = int64_t(source);
					if (!p_shapes.has(key) || p_shapes[key].get_type() != Variant::RID) {
						return ERR_DOES_NOT_EXIST;
					}
					resource = p_shapes[key];
				}
				sp.shape = p_server->shape_owner.get_or_null(resource);
				if (!sp.shape) {
					return ERR_DOES_NOT_EXIST;
				}
				sp.transform = in.xform();
				sp.disabled = in.boolean();
				sp.one_way = in.boolean();
				sp.one_way_direction = in.vec2();
				sp.one_way_margin = in.f32();
				const uint32_t native = in.u32();
				if (!in.ok || native > kMaxShapes) {
					return ERR_FILE_CORRUPT;
				}
				for (uint32_t n = 0; n < native; ++n) {
					sp.native.push_back(in.u32());
				}
			}
			if (p.kind != 0) {
				p.priority = int(in.u32());
				p.monitorable = in.boolean();
				p.gravity_strength = in.f32();
				p.gravity_direction = in.vec2();
				p.linear_damp = in.f32();
				p.angular_damp = in.f32();
				p.gravity_point = in.boolean();
				p.unit_distance = in.f32();
				p.gravity_mode = in.u32();
				p.linear_mode = in.u32();
				p.angular_mode = in.u32();
				p.in_step_list = in.boolean();
				p.step_index = in.u32();
				if (!read_callable(in, p.body_monitor) || !read_callable(in, p.area_monitor)) {
					return ERR_FILE_CORRUPT;
				}
				const uint32_t overlaps = in.u32();
				if (!in.ok || overlaps > kMaxShapes) {
					return ERR_FILE_CORRUPT;
				}
				for (uint32_t k = 0; k < overlaps; ++k) {
					OverlapPlan o;
					o.other = in.u64();
					o.other_index = int(in.u32());
					o.self_index = int(in.u32());
					if (!map_object(p_objects, in.u64(), o.instance)) {
						return ERR_DOES_NOT_EXIST;
					}
					o.type = in.u8();
					o.count = int(in.u32());
					p.overlaps.push_back(o);
				}
				const uint32_t counted = in.u32();
				if (!in.ok || counted > kMaxShapes) {
					return ERR_FILE_CORRUPT;
				}
				for (uint32_t k = 0; k < counted; ++k) {
					const uint64_t rid = in.u64();
					p.overlap_counts.push_back(Pair<uint64_t, int>(rid, int(in.u32())));
				}
				if (p.gravity_mode > 4 || p.linear_mode > 4 || p.angular_mode > 4) {
					return ERR_FILE_CORRUPT;
				}
				continue;
			}
			p.mass_data.mass = in.f32();
			p.mass_data.center = in.bvec2();
			p.mass_data.rotationalInertia = in.f32();
			p.overrides.total_gravity = in.vec2();
			p.overrides.skip_world_gravity = in.boolean();
			p.overrides.skip_world_linear_damp = in.boolean();
			p.overrides.skip_world_angular_damp = in.boolean();
			p.overrides.total_linear_damp = in.f32();
			p.overrides.total_angular_damp = in.f32();
			p.overrides.ignore_remaining_gravity = in.boolean();
			p.overrides.ignore_remaining_linear_damp = in.boolean();
			p.overrides.ignore_remaining_angular_damp = in.boolean();
			p.constant_force = in.vec2();
			p.constant_torque = in.f32();
			p.total_gravity = in.vec2();
			p.total_linear_damp = in.f32();
			p.total_angular_damp = in.f32();
			p.initial_linear_velocity = in.vec2();
			p.initial_angular_velocity = in.f32();
			p.use_static_velocities = in.boolean();
			p.static_linear_velocity = in.vec2();
			p.static_angular_velocity = in.f32();
			p.sleeping = in.boolean();
			p.queried_contacts = in.boolean();
			p.omit_force_integration = in.boolean();
			p.linear_damping = in.f32();
			p.angular_damping = in.f32();
			p.mass = in.f32();
			p.inertia = in.f32();
			p.center_of_mass = in.vec2();
			p.override_center_of_mass = in.boolean();
			p.override_inertia = in.boolean();
			p.linear_damp_mode = in.u32();
			p.angular_damp_mode = in.u32();
			p.contact_depth_threshold = in.f32();
			p.contact_ignore_speculative = in.boolean();
			p.character_priority = in.f32();
			p.in_force_list = in.boolean();
			p.force_queue = in.u32();
			const uint32_t exceptions = in.u32();
			if (!in.ok || exceptions > kMaxObjects) {
				return ERR_FILE_CORRUPT;
			}
			for (uint32_t k = 0; k < exceptions; ++k) {
				p.exceptions.push_back(in.u64());
			}
			p.with_exceptions = in.boolean();
			const uint32_t filters = in.u32();
			if (!in.ok || filters > kMaxObjects) {
				return ERR_FILE_CORRUPT;
			}
			for (uint32_t k = 0; k < filters; ++k) {
				p.exception_joints.push_back(in.u32());
			}
			p.max_contacts = int(in.u32());
			const uint32_t contacts = in.u32();
			if (!in.ok || contacts > kMaxShapes) {
				return ERR_FILE_CORRUPT;
			}
			for (uint32_t k = 0; k < contacts; ++k) {
				ContactPlan c;
				c.collider = in.u64();
				c.contact.normal_impulse = in.f32();
				c.contact.local_position = in.vec2();
				c.contact.local_normal = in.vec2();
				c.contact.depth = in.f32();
				c.contact.local_shape = int(in.u32());
				c.contact.collider_position = in.vec2();
				c.contact.collider_shape = int(in.u32());
				if (!map_object(p_objects, in.u64(), c.contact.collider_instance_id)) {
					return ERR_DOES_NOT_EXIST;
				}
				c.contact.collider_velocity = in.vec2();
				c.contact.impulse = in.vec2();
				p.contacts.push_back(c);
			}
			if (!read_callable(in, p.state_sync) || !read_callable(in, p.integration)) {
				return ERR_FILE_CORRUPT;
			}
			error = read_user_data(in, p_objects, p.integration_data);
			if (error != OK) {
				return error;
			}
			p.in_integration_list = in.boolean();
			p.integration_index = in.u32();
			if (p.linear_damp_mode > 1 || p.angular_damp_mode > 1) {
				return ERR_FILE_CORRUPT;
			}
		}
		r.force_count = in.u32();
		r.integration_count = in.u32();
		r.area_count = in.u32();
		const uint32_t joints = in.u32();
		if (!in.ok || default_areas != 1 || joints > kMaxObjects) {
			return ERR_FILE_CORRUPT;
		}
		r.joints.resize(joints);
		for (JointPlan &j : r.joints) {
			j.type = PS2DE::JointType(in.u32());
			j.source_rid = in.u64();
			j.body_a = in.u32();
			j.body_b = in.u32();
			j.in_space = in.boolean();
			j.native = in.u32();
			j.hertz = in.f32();
			j.damping = in.f32();
			j.disabled = in.boolean();
			switch (j.type) {
				case PS2DE::JOINT_TYPE_PIN: {
					b2RevoluteJointDef &d = j.revolute;
					read_joint_base(in, d.base, j.def_a, j.def_b);
					d.targetAngle = in.f32();
					d.enableSpring = in.boolean();
					d.hertz = in.f32();
					d.dampingRatio = in.f32();
					d.enableLimit = in.boolean();
					d.lowerAngle = in.f32();
					d.upperAngle = in.f32();
					d.enableMotor = in.boolean();
					d.maxMotorTorque = in.f32();
					d.motorSpeed = in.f32();
					d.internalValue = int(in.u32());
				} break;
				case PS2DE::JOINT_TYPE_GROOVE: {
					b2WheelJointDef &d = j.wheel;
					read_joint_base(in, d.base, j.def_a, j.def_b);
					d.enableSpring = in.boolean();
					d.hertz = in.f32();
					d.dampingRatio = in.f32();
					d.enableLimit = in.boolean();
					d.lowerTranslation = in.f32();
					d.upperTranslation = in.f32();
					d.enableMotor = in.boolean();
					d.maxMotorTorque = in.f32();
					d.motorSpeed = in.f32();
					d.internalValue = int(in.u32());
				} break;
				case PS2DE::JOINT_TYPE_DAMPED_SPRING: {
					b2DistanceJointDef &d = j.distance;
					read_joint_base(in, d.base, j.def_a, j.def_b);
					d.length = in.f32();
					d.enableSpring = in.boolean();
					d.lowerSpringForce = in.f32();
					d.upperSpringForce = in.f32();
					d.hertz = in.f32();
					d.dampingRatio = in.f32();
					d.enableLimit = in.boolean();
					d.minLength = in.f32();
					d.maxLength = in.f32();
					d.enableMotor = in.boolean();
					d.maxMotorForce = in.f32();
					d.motorSpeed = in.f32();
					d.internalValue = int(in.u32());
					j.stiffness = in.f32();
				} break;
				default: {
					if (j.type < PS2DE::JOINT_TYPE_DISTANCE || j.type > PS2DE::JOINT_TYPE_WHEEL) {
						return ERR_FILE_CORRUPT;
					}
					j.frame_a = in.xform();
					j.frame_b = in.xform();
					const uint32_t length = in.u32();
					if (!in.need(length) || length > 65536) {
						return ERR_FILE_CORRUPT;
					}
					Variant value;
					int used = 0;
					if (decode_variant(value, in.p, int(length), &used, false) != OK || used != int(length) || value.get_type() != Variant::DICTIONARY) {
						return ERR_FILE_CORRUPT;
					}
					in.p += length;
					in.left -= length;
					j.configuration = value;
				} break;
			}
			if (!in.ok) {
				return ERR_FILE_CORRUPT;
			}
		}
		const uint64_t native_size = in.u64();
		if (!in.ok || native_size != in.left) {
			return ERR_FILE_CORRUPT;
		}
		r.native = in.p;
		r.native_size = size_t(native_size);
		return OK;
	}

	static void discard(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, SpacePlan &p_plan) {
		for (JointPlan &j : p_plan.joints) {
			if (j.created) {
				j.created->joint_id = b2_nullJointId;
				p_server->joint_owner.free(j.created->rid);
				memdelete(j.created);
				j.created = nullptr;
			}
		}
		for (ObjectPlan &p : p_plan.objects) {
			if (!p.created) {
				continue;
			}
			Box2DCollisionObject2D *object = p.created;
			if (p_space && object->space == p_space) {
				p_space->objects.erase(object);
				object->space = nullptr;
				object->body_id = b2_nullBodyId;
			}
			for (Box2DShapeInstance &shape : object->shapes) {
				shape.shape_ids.clear();
			}
			if (Box2DBody2D *body = object->as_body()) {
				body->exception_joints.clear();
				body->in_force_integration_list = false;
				body->in_constant_forces_list = false;
			}
			if (Box2DArea2D *area = object->as_area()) {
				area->in_area_step_list = false;
			}
			if (object->is_area()) {
				p_server->area_owner.free(object->rid);
				memdelete(object->as_area());
			} else {
				p_server->body_owner.free(object->rid);
				memdelete(object->as_body());
			}
			p.created = nullptr;
		}
		if (p_space) {
			p_space->areas_to_step.clear();
			p_space->constant_force_list.clear();
			p_space->force_integration_list.clear();
			p_space->bodies_with_exceptions.clear();
			p_space->set_default_area(nullptr);
			p_server->space_owner.free(p_space->rid);
			memdelete(p_space);
		}
	}

	static Dictionary fail_restore(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, SpacePlan &p_plan, Error p_error, int p_line) {
		discard(p_server, p_space, p_plan);
		Dictionary result;
		result["error"] = int(p_error);
		result["refusal"] = p_line;
		return result;
	}

	static Dictionary restore(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_objects, const Dictionary &p_shapes, const Dictionary &p_callables) {
		Dictionary result;
		result["error"] = int(ERR_BUSY);
		if (!Thread::is_main_thread() || p_server->flushing_queries) {
			return result;
		}
		SpacePlan plan;
		Error error = parse(p_server, p_bytes, p_objects, p_shapes, plan);
		if (error != OK) {
			result["error"] = int(error);
			return result;
		}
		// Map source object ids to plan indices for RID references.
		HashMap<uint64_t, uint32_t> by_source;
		HashMap<uint32_t, uint32_t> by_slot;
		for (uint32_t i = 0; i < plan.objects.size(); ++i) {
			by_source.insert(plan.objects[i].source_rid, i);
			by_slot.insert(plan.objects[i].slot, i);
		}
		// Every RID reference must name an object of this checkpoint; callbacks must resolve.
		LocalVector<Callable> state_sync, integration, body_monitor, area_monitor;
		state_sync.resize(plan.objects.size());
		integration.resize(plan.objects.size());
		body_monitor.resize(plan.objects.size());
		area_monitor.resize(plan.objects.size());
		for (uint32_t i = 0; i < plan.objects.size(); ++i) {
			ObjectPlan &p = plan.objects[i];
			const String id = itos(int64_t(p.source_rid));
			for (uint64_t other : p.exceptions) {
				if (!by_source.has(other) || plan.objects[by_source[other]].kind != 0) {
					result["error"] = int(ERR_DOES_NOT_EXIST);
					return result;
				}
			}
			for (const ContactPlan &c : p.contacts) {
				if (!by_source.has(c.collider) || plan.objects[by_source[c.collider]].kind != 0) {
					result["error"] = int(ERR_DOES_NOT_EXIST);
					return result;
				}
			}
			for (const OverlapPlan &o : p.overlaps) {
				if (!by_source.has(o.other)) {
					result["error"] = int(ERR_DOES_NOT_EXIST);
					return result;
				}
			}
			for (const Pair<uint64_t, int> &o : p.overlap_counts) {
				if (!by_source.has(o.first)) {
					result["error"] = int(ERR_DOES_NOT_EXIST);
					return result;
				}
			}
			if ((error = resolve_callable(p.state_sync, "state_sync:" + id, p_objects, p_callables, false, state_sync[i])) != OK ||
					(error = resolve_callable(p.integration, "force_integration:" + id, p_objects, p_callables, true, integration[i])) != OK ||
					(error = resolve_callable(p.body_monitor, "body_monitor:" + id, p_objects, p_callables, true, body_monitor[i])) != OK ||
					(error = resolve_callable(p.area_monitor, "area_monitor:" + id, p_objects, p_callables, true, area_monitor[i])) != OK) {
				result["error"] = int(error);
				return result;
			}
		}
		for (const JointPlan &j : plan.joints) {
			if ((j.body_a != kNone && !by_slot.has(j.body_a)) || (j.body_b != kNone && !by_slot.has(j.body_b))) {
				result["error"] = int(ERR_FILE_CORRUPT);
				return result;
			}
		}
		// Fresh space with an empty native world.
		Box2DSpace2D *space = memnew(Box2DSpace2D);
		RID space_rid = p_server->space_owner.make_rid(space);
		space->set_rid(space_rid);
		if (!spB2PortableWorldEmpty(space->world_id)) {
			return fail_restore(p_server, space, plan, ERR_BUG, __LINE__);
		}
		// Wrappers with new RIDs, configured without touching any native world.
		Box2DArea2D *default_area = nullptr;
		for (uint32_t i = 0; i < plan.objects.size(); ++i) {
			ObjectPlan &p = plan.objects[i];
			Box2DCollisionObject2D *object = nullptr;
			if (p.kind != 0) {
				Box2DArea2D *area = p_server->area_owner.get_or_null(p_server->area_create());
				object = area;
				if (p.kind == 1) {
					default_area = area;
				}
				area->priority = p.priority;
				area->monitorable = p.monitorable;
				area->gravity_strength = p.gravity_strength;
				area->gravity_direction = p.gravity_direction;
				area->linear_damp = p.linear_damp;
				area->angular_damp = p.angular_damp;
				area->gravity_point_enabled = p.gravity_point;
				area->gravity_point_unit_distance = p.unit_distance;
				area->override_gravity_mode = PS2DE::AreaSpaceOverrideMode(p.gravity_mode);
				area->override_linear_damp_mode = PS2DE::AreaSpaceOverrideMode(p.linear_mode);
				area->override_angular_damp_mode = PS2DE::AreaSpaceOverrideMode(p.angular_mode);
				area->body_monitor_callback = body_monitor[i];
				area->area_monitor_callback = area_monitor[i];
			} else {
				Box2DBody2D *body = p_server->body_owner.get_or_null(p_server->body_create());
				object = body;
				body->mass_data = p.mass_data;
				body->area_overrides = p.overrides;
				body->constant_force = p.constant_force;
				body->constant_torque = p.constant_torque;
				body->total_gravity = p.total_gravity;
				body->total_linear_damp = p.total_linear_damp;
				body->total_angular_damp = p.total_angular_damp;
				body->initial_linear_velocity = p.initial_linear_velocity;
				body->initial_angular_velocity = p.initial_angular_velocity;
				body->use_static_velocities = p.use_static_velocities;
				body->static_linear_velocity = p.static_linear_velocity;
				body->static_angular_velocity = p.static_angular_velocity;
				body->sleeping = p.sleeping;
				body->queried_contacts = p.queried_contacts;
				body->omit_force_integration = p.omit_force_integration;
				body->linear_damping = p.linear_damping;
				body->angular_damping = p.angular_damping;
				body->mass = p.mass;
				body->inertia = p.inertia;
				body->center_of_mass = p.center_of_mass;
				body->override_center_of_mass = p.override_center_of_mass;
				body->override_inertia = p.override_inertia;
				body->linear_damp_mode = PS2DE::BodyDampMode(p.linear_damp_mode);
				body->angular_damp_mode = PS2DE::BodyDampMode(p.angular_damp_mode);
				body->contact_depth_threshold = p.contact_depth_threshold;
				body->contact_ignore_speculative = p.contact_ignore_speculative;
				body->character_collision_priority = p.character_priority;
				body->in_constant_forces_list = p.in_force_list;
				body->max_contact_count = p.max_contacts;
				body->body_state_callback = state_sync[i];
				body->body_state_callback_is_valid = false;
				body->force_integration_callback = integration[i];
				body->force_integration_user_data = p.integration_data;
				body->in_force_integration_list = p.in_integration_list;
			}
			p.created = object;
			object->instance_id = p.instance;
			object->canvas_instance_id = p.canvas;
			object->pickable = p.pickable;
			object->is_animatable_body = p.animatable;
			object->current_transform = p.transform;
			object->mode = p.mode;
			object->body_def = p.body_def;
			object->body_def.userData = object;
			object->shape_def = p.shape_def;
			object->user_data = p.user_data;
			object->shapes.reserve(p.shapes.size());
			for (const ShapePlan &sp : p.shapes) {
				object->shapes.push_back(Box2DShapeInstance(object, sp.shape, sp.transform, sp.disabled));
			}
			for (uint32_t s = 0; s < object->shapes.size(); ++s) {
				Box2DShapeInstance &shape = object->shapes[s];
				shape.index = int(s);
				shape.one_way_collision = p.shapes[s].one_way;
				shape.one_way_direction = p.shapes[s].one_way_direction;
				shape.one_way_collision_margin = p.shapes[s].one_way_margin;
			}
		}
		// Joint wrappers with new RIDs and no native joint yet.
		for (JointPlan &j : plan.joints) {
			Box2DJoint2D *joint = nullptr;
			switch (j.type) {
				case PS2DE::JOINT_TYPE_PIN: {
					Box2DPinJoint2D *pin = memnew(Box2DPinJoint2D(Vector2(), nullptr, nullptr));
					pin->revolute_def = j.revolute;
					joint = pin;
				} break;
				case PS2DE::JOINT_TYPE_GROOVE: {
					Box2DGrooveJoint2D *groove = memnew(Box2DGrooveJoint2D(Vector2(), Vector2(), Vector2(), nullptr, nullptr));
					groove->wheel_def = j.wheel;
					joint = groove;
				} break;
				case PS2DE::JOINT_TYPE_DAMPED_SPRING: {
					Box2DDampedSpringJoint2D *spring = memnew(Box2DDampedSpringJoint2D(Vector2(), Vector2(), nullptr, nullptr));
					spring->distance_def = j.distance;
					spring->stiffness = j.stiffness;
					joint = spring;
				} break;
				default: {
					Box2DConfiguredJoint2D *configured = memnew(Box2DConfiguredJoint2D(j.type, nullptr, nullptr, j.frame_a, j.frame_b));
					configured->configuration = j.configuration;
					joint = configured;
				} break;
			}
			j.created = joint;
			const RID rid = p_server->joint_owner.make_rid(joint);
			joint->set_rid(rid);
			joint->body_a = j.body_a != kNone ? plan.objects[by_slot[j.body_a]].created->as_body() : nullptr;
			joint->body_b = j.body_b != kNone ? plan.objects[by_slot[j.body_b]].created->as_body() : nullptr;
			joint->hertz = j.hertz;
			joint->damping_ratio = j.damping;
			joint->disabled_collisions_between_bodies = j.disabled;
			if ((j.body_a != kNone && !joint->body_a) || (j.body_b != kNone && !joint->body_b)) {
				return fail_restore(p_server, space, plan, ERR_FILE_CORRUPT, __LINE__);
			}
		}
		// The destination registry: same symbols, this process's pointers.
		LocalVector<SpB2Symbol> table;
		void *friction = nullptr, *restitution = nullptr;
		default_box2d_callbacks(friction, restitution);
		if (friction) {
			table.push_back({ kSymbolBox2DFriction, friction });
		}
		if (restitution) {
			table.push_back({ kSymbolBox2DRestitution, restitution });
		}
		table.push_back({ kSymbolGodotFriction, reinterpret_cast<void *>(&godot_friction_callback) });
		table.push_back({ kSymbolGodotRestitution, reinterpret_cast<void *>(&godot_restitution_callback) });
		table.push_back({ kSymbolGodotPresolve, reinterpret_cast<void *>(&box2d_godot_presolve) });
		table.push_back({ kSymbolSpace, space });
		for (ObjectPlan &p : plan.objects) {
			table.push_back({ kSymbolObject + p.slot, p.created });
			for (uint32_t s = 0; s < p.shapes.size(); ++s) {
				if (!p.shapes[s].native.is_empty()) {
					table.push_back({ kSymbolShape + p.shapes[s].native[0], &p.created->shapes[s] });
				}
			}
		}
		for (JointPlan &j : plan.joints) {
			if (j.native != kNone) {
				table.push_back({ kSymbolJoint + j.native, j.created });
			}
		}
		const SpB2PortableStatus status = spB2PortableRestore(plan.native, plan.native_size, table.ptr(), table.size(), space->world_id);
		if (status != SP_B2_PORTABLE_OK) {
			Dictionary failed = fail_restore(p_server, space, plan, status == SP_B2_PORTABLE_UNKNOWN_SYMBOL ? ERR_DOES_NOT_EXIST : status == SP_B2_PORTABLE_INCOMPATIBLE ? ERR_FILE_UNRECOGNIZED : status == SP_B2_PORTABLE_NO_MEMORY ? ERR_OUT_OF_MEMORY : status == SP_B2_PORTABLE_DIGEST_MISMATCH ? ERR_INVALID_DATA : ERR_FILE_CORRUPT, __LINE__);
			failed["native_status"] = int(status);
			return failed;
		}
		// Bind wrappers to their native bodies and shapes (layout-preserving restore).
		for (ObjectPlan &p : plan.objects) {
			Box2DCollisionObject2D *object = p.created;
			b2BodyId id = b2_nullBodyId;
			void *user = nullptr;
			if (!spB2PortableBodyAt(space->world_id, p.slot, &id, &user) || user != object) {
				return fail_restore(p_server, space, plan, ERR_INVALID_DATA, __LINE__);
			}
			for (uint32_t s = 0; s < p.shapes.size(); ++s) {
				for (uint32_t native_shape : p.shapes[s].native) {
					b2ShapeId shape_id = b2_nullShapeId;
					void *shape_user = nullptr;
					if (!spB2PortableShapeAt(space->world_id, native_shape, &shape_id, &shape_user) || shape_user != &object->shapes[s] ||
							!B2_ID_EQUALS(b2Shape_GetBody(shape_id), id)) {
						return fail_restore(p_server, space, plan, ERR_INVALID_DATA, __LINE__);
					}
					object->shapes[s].shape_ids.push_back(shape_id);
				}
			}
			object->body_id = id;
			object->space = space;
			space->objects.insert(object);
		}
		if (plan.anchor != kNone) {
			b2BodyId id = b2_nullBodyId;
			void *user = nullptr;
			if (!spB2PortableBodyAt(space->world_id, plan.anchor, &id, &user) || user) {
				return fail_restore(p_server, space, plan, ERR_INVALID_DATA, __LINE__);
			}
			space->world_anchor_body = id;
		}
		// Native joints and filter joints by slot, found through their bodies.
		LocalVector<b2JointId> native;
		native_joints(space->world_id, native);
		auto joint_at = [&](uint32_t p_slot, b2JointId &r_id) {
			for (b2JointId id : native) {
				if (native_slot(id) == p_slot) {
					r_id = id;
					return true;
				}
			}
			return false;
		};
		auto body_id_at = [&](uint32_t p_slot) {
			b2BodyId id = b2_nullBodyId;
			if (p_slot != kNone) {
				spB2PortableBodyAt(space->world_id, p_slot, &id, nullptr);
			}
			return id;
		};
		for (JointPlan &j : plan.joints) {
			Box2DJoint2D *joint = j.created;
			joint->space = j.in_space ? space : nullptr;
			b2JointDef *base = j.type == PS2DE::JOINT_TYPE_PIN ? &static_cast<Box2DPinJoint2D *>(joint)->revolute_def.base : j.type == PS2DE::JOINT_TYPE_GROOVE ? &static_cast<Box2DGrooveJoint2D *>(joint)->wheel_def.base : j.type == PS2DE::JOINT_TYPE_DAMPED_SPRING ? &static_cast<Box2DDampedSpringJoint2D *>(joint)->distance_def.base : nullptr;
			if (base) {
				base->bodyIdA = body_id_at(j.def_a);
				base->bodyIdB = body_id_at(j.def_b);
				base->userData = joint;
			}
			if (j.native != kNone) {
				b2JointId id = b2_nullJointId;
				if (!joint_at(j.native, id) || b2Joint_GetUserData(id) != joint) {
					return fail_restore(p_server, space, plan, ERR_INVALID_DATA, __LINE__);
				}
				joint->joint_id = id;
			}
		}
		for (ObjectPlan &p : plan.objects) {
			Box2DBody2D *body = p.created->as_body();
			if (!body) {
				continue;
			}
			for (uint64_t other : p.exceptions) {
				body->exceptions.insert(plan.objects[by_source[other]].created->rid);
			}
			for (uint32_t slot : p.exception_joints) {
				b2JointId id = b2_nullJointId;
				if (!joint_at(slot, id) || b2Joint_GetUserData(id) != nullptr) {
					return fail_restore(p_server, space, plan, ERR_INVALID_DATA, __LINE__);
				}
				body->exception_joints.push_back(id);
			}
			if (p.with_exceptions) {
				space->bodies_with_exceptions.insert(body);
			}
			for (const ContactPlan &c : p.contacts) {
				Box2DBody2D::Contact contact = c.contact;
				Box2DCollisionObject2D *peer = plan.objects[by_source[c.collider]].created;
				contact.body = peer->as_body();
				contact.collider = peer->rid;
				body->contacts.push_back(contact);
			}
		}
		for (ObjectPlan &p : plan.objects) {
			Box2DArea2D *area = p.created->as_area();
			if (!area) {
				continue;
			}
			area->in_area_step_list = p.in_step_list;
			for (const OverlapPlan &o : p.overlaps) {
				Box2DArea2D::ShapePair pair{ plan.objects[by_source[o.other]].created->rid, o.other_index, o.self_index };
				Box2DArea2D::ObjectAndOverlapCount value;
				value.instance_id = o.instance;
				value.type = Box2DCollisionObject2D::Type(o.type);
				value.count = o.count;
				area->overlaps.insert(pair, value);
			}
			for (const Pair<uint64_t, int> &o : p.overlap_counts) {
				area->object_overlap_count.insert(plan.objects[by_source[o.first]].created->rid, o.second);
			}
		}
		space->set_default_area(default_area);
		space->default_gravity = plan.gravity;
		space->contact_hertz = plan.contact[0];
		space->contact_damping_ratio = plan.contact[1];
		space->contact_max_push_speed = plan.contact[2];
		space->substeps = plan.substeps;
		space->last_step = plan.last_step;
		space->linear_damp_changed = plan.damp[0];
		space->angular_damp_changed = plan.damp[1];
		space->exceptions_dirty = plan.exceptions_dirty;
		for (uint32_t q = 0; q < plan.force_count; ++q) {
			for (ObjectPlan &p : plan.objects) {
				if (p.kind == 0 && p.force_queue == q) {
					space->constant_force_list.push_back(p.created->as_body());
				}
			}
		}
		for (uint32_t q = 0; q < plan.integration_count; ++q) {
			for (ObjectPlan &p : plan.objects) {
				if (p.kind == 0 && p.integration_index == q) {
					space->force_integration_list.push_back(p.created->as_body());
				}
			}
		}
		for (uint32_t q = 0; q < plan.area_count; ++q) {
			for (ObjectPlan &p : plan.objects) {
				if (p.kind == 2 && p.step_index == q) {
					space->areas_to_step.push_back(p.created->as_area());
				}
			}
		}
		if (space->constant_force_list.size() != plan.force_count || space->force_integration_list.size() != plan.integration_count || space->areas_to_step.size() != plan.area_count) {
			return fail_restore(p_server, space, plan, ERR_FILE_CORRUPT, __LINE__);
		}
		Dictionary bodies, areas, joints;
		for (ObjectPlan &p : plan.objects) {
			if (p.kind == 0) {
				bodies[int64_t(p.source_rid)] = p.created->rid;
			} else if (p.kind == 2) {
				areas[int64_t(p.source_rid)] = p.created->rid;
			}
		}
		for (JointPlan &j : plan.joints) {
			joints[int64_t(j.source_rid)] = j.created->rid;
		}
		result["error"] = int(OK);
		result["space"] = space_rid;
		result["default_area"] = default_area->rid;
		result["bodies"] = bodies;
		result["areas"] = areas;
		result["joints"] = joints;
		return result;
	}
};

#endif // EGP_BOX2D_PORTABLE

Dictionary Box2DPortableSpace::capture(Box2DPhysicsServer2D *p_server, RID p_space) {
	Dictionary result;
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = p_server->space_owner.get_or_null(p_space);
	PackedByteArray bytes;
	const Error error = space ? Box2DPortableSpaceAccess::capture_bytes(p_server, space, false, bytes) : ERR_INVALID_PARAMETER;
	result["error"] = int(error);
	result["bytes"] = bytes;
	result["refusal"] = error == OK ? 0 : last_refusal;
#else
	result["error"] = int(ERR_UNAVAILABLE);
	result["bytes"] = PackedByteArray();
#endif
	return result;
}

Error Box2DPortableSpace::request_capture(Box2DPhysicsServer2D *p_server, RID p_space) {
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = p_server->space_owner.get_or_null(p_space);
	if (!space) {
		return ERR_INVALID_PARAMETER;
	}
	PortableState *state = state_for(space);
	if (!state) {
		return ERR_OUT_OF_MEMORY;
	}
	state->requested = true;
	state->has_pending = false;
	state->pending = PackedByteArray();
	return OK;
#else
	return ERR_UNAVAILABLE;
#endif
}

Dictionary Box2DPortableSpace::take_capture(Box2DPhysicsServer2D *p_server, RID p_space) {
	Dictionary result;
	result["error"] = int(ERR_UNAVAILABLE);
	result["bytes"] = PackedByteArray();
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = p_server->space_owner.get_or_null(p_space);
	PortableState **found = space ? states().getptr(space) : nullptr;
	if (!found || !(*found)->has_pending) {
		result["error"] = int((found && (*found)->requested) ? ERR_BUSY : ERR_DOES_NOT_EXIST);
		return result;
	}
	result["error"] = int((*found)->pending_error);
	result["refusal"] = (*found)->pending_error == OK ? 0 : last_refusal;
	result["bytes"] = (*found)->pending;
	(*found)->has_pending = false;
	(*found)->pending = PackedByteArray();
#endif
	return result;
}

Dictionary Box2DPortableSpace::restore(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_object_map, const Dictionary &p_shape_map, const Dictionary &p_callable_map) {
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return Box2DPortableSpaceAccess::restore(p_server, p_bytes, p_object_map, p_shape_map, p_callable_map);
#else
	Dictionary result;
	result["error"] = int(ERR_UNAVAILABLE);
	return result;
#endif
}

PackedByteArray Box2DPortableSpace::digest(Box2DPhysicsServer2D *p_server, RID p_space) {
	PackedByteArray out;
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = p_server->space_owner.get_or_null(p_space);
	if (!space || space->is_locked()) {
		return out;
	}
	LocalVector<SpB2Symbol> table;
	if (Box2DPortableSpaceAccess::symbols(space, table) != OK) {
		return out;
	}
	uint8_t digest[32];
	if (spB2PortableDigest(space->world_id, table.ptr(), table.size(), digest) != SP_B2_PORTABLE_OK) {
		return out;
	}
	out.resize(32);
	std::memcpy(out.ptrw(), digest, 32);
#endif
	return out;
}

Dictionary Box2DPortableSpace::capture_info(Box2DPhysicsServer2D *p_server, RID p_space) {
	Dictionary info;
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DSpace2D *space = p_server->space_owner.get_or_null(p_space);
	PortableState **found = space ? states().getptr(space) : nullptr;
	if (found) {
		const SpB2CaptureStats &s = (*found)->stats;
		info["captures"] = int64_t(s.captures);
		info["births"] = int64_t(s.births);
		info["retirements"] = int64_t(s.retirements);
		info["survivors"] = int64_t(s.survivors);
		info["records"] = int64_t(s.records);
		info["bindings"] = int64_t(s.bindings);
	}
#endif
	return info;
}

int64_t Box2DPortableSpace::identity(Box2DPhysicsServer2D *p_server, RID p_body) {
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	Box2DBody2D *body = p_server->body_owner.get_or_null(p_body);
	if (!body || !body->get_space()) {
		return 0;
	}
	PortableState **found = states().getptr(body->get_space());
	if (!found || B2_IS_NULL(body->get_body_id())) {
		return 0;
	}
	return int64_t(spB2PortableIdentity((*found)->registries, 0, uint32_t(body->get_body_id().index1 - 1)));
#else
	return 0;
#endif
}

void Box2DPortableSpace::after_flush(Box2DSpace2D *p_space) {
#ifdef EGP_BOX2D_PORTABLE
	PortableState **found = states().getptr(p_space);
	if (!found || !(*found)->requested) {
		return;
	}
	PortableState *state = *found;
	state->requested = false;
	state->pending = PackedByteArray();
	state->pending_error = Box2DPortableSpaceAccess::capture_bytes(Box2DPhysicsServer2D::get_singleton(), p_space, true, state->pending);
	state->has_pending = true;
#endif
}

void Box2DPortableSpace::space_destroyed(Box2DSpace2D *p_space) {
#ifdef EGP_BOX2D_PORTABLE
	auto &map = states();
	if (PortableState **found = map.getptr(p_space)) {
		spB2PortableDestroyState((*found)->registries);
		memdelete(*found);
		map.erase(p_space);
	}
#endif
}
