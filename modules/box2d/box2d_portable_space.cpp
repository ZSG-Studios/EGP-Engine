// SPDX-License-Identifier: MIT
#include "box2d_portable_space.h"

#include "modules/box2d/bodies/box2d_area_2d.h"
#include "modules/box2d/bodies/box2d_body_2d.h"
#include "modules/box2d/box2d_physics_server_2d.h"
#include "modules/box2d/simulation_guard.h"
#include "modules/box2d/spaces/box2d_space_2d.h"

#include "core/crypto/crypto_core.h"
#include "core/io/marshalls.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"

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
constexpr uint32_t kVersion = 1;
constexpr uint64_t kSymbolBox2DFriction = 1, kSymbolBox2DRestitution = 2, kSymbolGodotFriction = 3, kSymbolGodotRestitution = 4,
				   kSymbolGodotPresolve = 5, kSymbolSpace = 16, kSymbolObject = 0x10000000ull, kSymbolShape = 0x20000000ull;
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
	// Names every pointer the native world holds: callbacks, the pre-solve
	// context (the space), each collision object by its native body slot and
	// each shape instance by its first native shape slot.
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
		return OK;
	}

	static uint32_t native_slot(b2BodyId p_id) { return B2_IS_NULL(p_id) ? kNone : uint32_t(p_id.index1 - 1); }
	static uint32_t native_slot(b2ShapeId p_id) { return B2_IS_NULL(p_id) ? kNone : uint32_t(p_id.index1 - 1); }

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
	static b2BodyDef read_body_def(Reader &r, void *p_user) {
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
		d.userData = p_user;
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

	static Error check_profile(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space) {
		if (!p_space->areas_to_step.is_empty() || !p_space->force_integration_list.is_empty() || !p_space->bodies_with_exceptions.is_empty() ||
				!p_space->bodies_with_overrides.is_empty() || !p_space->contact_hit_events.is_empty() || !p_space->joint_events.is_empty()) {
			SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
		}
		if (b2World_GetCounters(p_space->world_id).jointCount != 0) {
			SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
		}
		for (Box2DCollisionObject2D *object : p_space->objects) {
			if (!object || object->_is_freed || object->space != p_space || !b2Body_IsValid(object->body_id) || b2Body_GetUserData(object->body_id) != object) {
				SP_PORTABLE_FAIL(ERR_INVALID_DATA);
			}
			if (object->body_def.name != nullptr) {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
			if (object == p_space->default_area) {
				if (!object->shapes.is_empty() || p_server->area_owner.get_or_null(object->rid) != object) {
					SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
				}
				continue;
			}
			Box2DBody2D *body = object->as_body();
			if (!body || p_server->body_owner.get_or_null(object->rid) != body) {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
			if (!body->force_integration_callback.is_null() || body->force_integration_user_data.get_type() != Variant::NIL || !body->exceptions.is_empty() ||
					!body->exception_joints.is_empty() || body->max_contact_count != 0 || !body->contacts.is_empty() || body->in_force_integration_list) {
				SP_PORTABLE_FAIL(ERR_UNAVAILABLE);
			}
			for (const Box2DShapeInstance &shape : body->shapes) {
				if (shape.object != body || !shape.shape || p_server->shape_owner.get_or_null(shape.shape->get_rid()) != shape.shape) {
					SP_PORTABLE_FAIL(ERR_INVALID_DATA);
				}
				for (b2ShapeId id : shape.shape_ids) {
					if (!b2Shape_IsValid(id) || b2Shape_GetUserData(id) != &shape) {
						SP_PORTABLE_FAIL(ERR_INVALID_DATA);
					}
				}
			}
		}
		return OK;
	}

	static Error encode_wrappers(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, Writer &w) {
		w.raw(kMagic, 8);
		w.u32(kVersion);
		w.u32(spB2PortablePositionBits());
		w.u32(uint32_t(sizeof(real_t) * 8));
		// Space and default area.
		w.vec2(p_space->default_gravity);
		w.f32(p_space->contact_hertz);
		w.f32(p_space->contact_damping_ratio);
		w.f32(p_space->contact_max_push_speed);
		w.u32(uint32_t(p_space->substeps));
		w.f32(p_space->last_step);
		w.boolean(p_space->linear_damp_changed);
		w.boolean(p_space->angular_damp_changed);
		w.u32(native_slot(p_space->world_anchor_body));
		// Objects in native body slot order.
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
			const bool area = object == p_space->default_area;
			w.u8(area ? 1 : 0);
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
			if (area) {
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
			uint32_t queue = kNone;
			for (uint32_t i = 0; i < p_space->constant_force_list.size(); ++i) {
				if (p_space->constant_force_list[i] == b) {
					queue = i;
				}
			}
			w.boolean(b->in_constant_forces_list);
			w.u32(queue);
			if (b->shapes.size() > kMaxShapes) {
				SP_PORTABLE_FAIL(ERR_OUT_OF_MEMORY);
			}
			w.u32(b->shapes.size());
			for (const Box2DShapeInstance &shape : b->shapes) {
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
		}
		w.u32(p_space->constant_force_list.size());
		return OK;
	}

	struct ObjectSlotOrder {
		bool operator()(const Box2DCollisionObject2D *a, const Box2DCollisionObject2D *b) const {
			return native_slot(a->body_id) < native_slot(b->body_id);
		}
	};

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
			last_refusal = 100000 + int(status);
			return status == SP_B2_PORTABLE_BUSY ? ERR_BUSY : status == SP_B2_PORTABLE_UNKNOWN_SYMBOL ? ERR_UNAVAILABLE : status == SP_B2_PORTABLE_NO_MEMORY ? ERR_OUT_OF_MEMORY : ERR_INVALID_DATA;
		}
		state->stats = stats;
		w.u64(native.size());
		w.raw(native.ptr(), native.size());
		uint8_t hash[32];
		if (CryptoCore::sha256(w.bytes.ptr(), w.bytes.size(), hash) != OK) {
			return ERR_BUG;
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
	struct ObjectPlan {
		bool area = false;
		uint32_t slot = kNone;
		uint64_t source_rid = 0;
		ObjectID instance, canvas;
		bool pickable = true, animatable = false;
		Transform2D transform;
		PS2DE::BodyMode mode = PS2DE::BODY_MODE_STATIC;
		b2BodyDef body_def;
		b2ShapeDef shape_def;
		Variant user_data;
		// Area.
		int priority = 0;
		bool monitorable = false, gravity_point = false;
		real_t gravity_strength = 0, linear_damp = 0, angular_damp = 0, unit_distance = 0;
		Vector2 gravity_direction;
		uint32_t gravity_mode = 0, linear_mode = 0, angular_mode = 0;
		// Body.
		b2MassData mass_data = {};
		Box2DBody2D::AreaOverrideAccumulator overrides;
		Vector2 constant_force, total_gravity, initial_linear_velocity, static_linear_velocity, center_of_mass;
		real_t constant_torque = 0, total_linear_damp = 0, total_angular_damp = 0, initial_angular_velocity = 0, static_angular_velocity = 0;
		real_t linear_damping = 0, angular_damping = 0, mass = 0, inertia = 0, contact_depth_threshold = 0, character_priority = 0;
		bool use_static_velocities = false, sleeping = false, queried_contacts = false, omit_force_integration = false;
		bool override_center_of_mass = false, override_inertia = false, contact_ignore_speculative = true, in_force_list = false;
		uint32_t linear_damp_mode = 0, angular_damp_mode = 0, force_queue = kNone;
		LocalVector<ShapePlan> shapes;
		Box2DCollisionObject2D *created = nullptr;
	};

	static Error parse(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_objects, const Dictionary &p_shapes,
			LocalVector<ObjectPlan> &r_plans, Vector2 &r_gravity, float r_contact[3], int &r_substeps, real_t &r_last_step, bool r_damp[2],
			uint32_t &r_anchor, uint32_t &r_force_count, const uint8_t *&r_native, size_t &r_native_size) {
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
		Reader r{ data + 8, size - 8 - 32 };
		if (r.u32() != kVersion || r.u32() != spB2PortablePositionBits() || r.u32() != uint32_t(sizeof(real_t) * 8)) {
			return ERR_FILE_UNRECOGNIZED;
		}
		r_gravity = r.vec2();
		r_contact[0] = r.f32();
		r_contact[1] = r.f32();
		r_contact[2] = r.f32();
		r_substeps = int(r.u32());
		r_last_step = r.f32();
		r_damp[0] = r.boolean();
		r_damp[1] = r.boolean();
		r_anchor = r.u32();
		const uint32_t count = r.u32();
		if (!r.ok || count > kMaxObjects) {
			return ERR_FILE_CORRUPT;
		}
		r_plans.clear();
		r_plans.resize(count);
		uint32_t areas = 0, prior = 0;
		for (uint32_t i = 0; i < count; ++i) {
			ObjectPlan &p = r_plans[i];
			const uint8_t kind = r.u8();
			if (kind > 1) {
				return ERR_FILE_CORRUPT;
			}
			p.area = kind == 1;
			areas += p.area ? 1 : 0;
			p.slot = r.u32();
			if (p.slot == kNone || (i && p.slot <= prior)) {
				return ERR_FILE_CORRUPT;
			}
			prior = p.slot;
			p.source_rid = r.u64();
			if (!map_object(p_objects, r.u64(), p.instance) || !map_object(p_objects, r.u64(), p.canvas)) {
				return ERR_DOES_NOT_EXIST;
			}
			p.pickable = r.boolean();
			p.animatable = r.boolean();
			p.transform = r.xform();
			p.mode = PS2DE::BodyMode(r.u32());
			p.body_def = read_body_def(r, nullptr);
			p.shape_def = read_shape_def(r);
			Error error = read_user_data(r, p_objects, p.user_data);
			if (error != OK) {
				return error;
			}
			if (uint32_t(p.mode) > uint32_t(PS2DE::BODY_MODE_RIGID_LINEAR)) {
				return ERR_FILE_CORRUPT;
			}
			if (p.area) {
				p.priority = int(r.u32());
				p.monitorable = r.boolean();
				p.gravity_strength = r.f32();
				p.gravity_direction = r.vec2();
				p.linear_damp = r.f32();
				p.angular_damp = r.f32();
				p.gravity_point = r.boolean();
				p.unit_distance = r.f32();
				p.gravity_mode = r.u32();
				p.linear_mode = r.u32();
				p.angular_mode = r.u32();
				continue;
			}
			p.mass_data.mass = r.f32();
			p.mass_data.center = r.bvec2();
			p.mass_data.rotationalInertia = r.f32();
			p.overrides.total_gravity = r.vec2();
			p.overrides.skip_world_gravity = r.boolean();
			p.overrides.skip_world_linear_damp = r.boolean();
			p.overrides.skip_world_angular_damp = r.boolean();
			p.overrides.total_linear_damp = r.f32();
			p.overrides.total_angular_damp = r.f32();
			p.overrides.ignore_remaining_gravity = r.boolean();
			p.overrides.ignore_remaining_linear_damp = r.boolean();
			p.overrides.ignore_remaining_angular_damp = r.boolean();
			p.constant_force = r.vec2();
			p.constant_torque = r.f32();
			p.total_gravity = r.vec2();
			p.total_linear_damp = r.f32();
			p.total_angular_damp = r.f32();
			p.initial_linear_velocity = r.vec2();
			p.initial_angular_velocity = r.f32();
			p.use_static_velocities = r.boolean();
			p.static_linear_velocity = r.vec2();
			p.static_angular_velocity = r.f32();
			p.sleeping = r.boolean();
			p.queried_contacts = r.boolean();
			p.omit_force_integration = r.boolean();
			p.linear_damping = r.f32();
			p.angular_damping = r.f32();
			p.mass = r.f32();
			p.inertia = r.f32();
			p.center_of_mass = r.vec2();
			p.override_center_of_mass = r.boolean();
			p.override_inertia = r.boolean();
			p.linear_damp_mode = r.u32();
			p.angular_damp_mode = r.u32();
			p.contact_depth_threshold = r.f32();
			p.contact_ignore_speculative = r.boolean();
			p.character_priority = r.f32();
			p.in_force_list = r.boolean();
			p.force_queue = r.u32();
			if (p.linear_damp_mode > 1 || p.angular_damp_mode > 1) {
				return ERR_FILE_CORRUPT;
			}
			const uint32_t shapes = r.u32();
			if (!r.ok || shapes > kMaxShapes) {
				return ERR_FILE_CORRUPT;
			}
			p.shapes.resize(shapes);
			for (uint32_t s = 0; s < shapes; ++s) {
				ShapePlan &sp = p.shapes[s];
				const uint64_t source = r.u64();
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
				sp.transform = r.xform();
				sp.disabled = r.boolean();
				sp.one_way = r.boolean();
				sp.one_way_direction = r.vec2();
				sp.one_way_margin = r.f32();
				const uint32_t native = r.u32();
				if (!r.ok || native > kMaxShapes) {
					return ERR_FILE_CORRUPT;
				}
				for (uint32_t n = 0; n < native; ++n) {
					sp.native.push_back(r.u32());
				}
			}
		}
		r_force_count = r.u32();
		const uint64_t native_size = r.u64();
		if (!r.ok || areas != 1 || native_size != r.left) {
			return ERR_FILE_CORRUPT;
		}
		r_native = r.p;
		r_native_size = size_t(native_size);
		return OK;
	}

	static void discard(Box2DPhysicsServer2D *p_server, Box2DSpace2D *p_space, LocalVector<ObjectPlan> &p_plans) {
		for (ObjectPlan &p : p_plans) {
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
			if (p.area) {
				p_server->area_owner.free(object->rid);
				memdelete(object->as_area());
			} else {
				p_server->body_owner.free(object->rid);
				memdelete(object->as_body());
			}
			p.created = nullptr;
		}
		if (p_space) {
			p_space->set_default_area(nullptr);
			p_server->space_owner.free(p_space->rid);
			memdelete(p_space);
		}
	}

	static Dictionary restore(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_objects, const Dictionary &p_shapes) {
		Dictionary result;
		result["error"] = int(ERR_BUSY);
		if (!Thread::is_main_thread() || p_server->flushing_queries) {
			return result;
		}
		LocalVector<ObjectPlan> plans;
		Vector2 gravity;
		float contact[3] = {};
		int substeps = 0;
		real_t last_step = 0;
		bool damp[2] = {};
		uint32_t anchor = kNone, force_count = 0;
		const uint8_t *native = nullptr;
		size_t native_size = 0;
		Error error = parse(p_server, p_bytes, p_objects, p_shapes, plans, gravity, contact, substeps, last_step, damp, anchor, force_count, native, native_size);
		result["error"] = int(error);
		if (error != OK) {
			return result;
		}
		// Fresh space with an empty native world.
		Box2DSpace2D *space = memnew(Box2DSpace2D);
		RID space_rid = p_server->space_owner.make_rid(space);
		space->set_rid(space_rid);
		if (!spB2PortableWorldEmpty(space->world_id)) {
			discard(p_server, space, plans);
			result["error"] = int(ERR_BUG);
			return result;
		}
		// Wrappers with new RIDs, configured without touching any native world.
		Box2DArea2D *default_area = nullptr;
		LocalVector<SpB2Symbol> table;
		for (ObjectPlan &p : plans) {
			Box2DCollisionObject2D *object = nullptr;
			if (p.area) {
				default_area = p_server->area_owner.get_or_null(p_server->area_create());
				object = default_area;
				default_area->priority = p.priority;
				default_area->monitorable = p.monitorable;
				default_area->gravity_strength = p.gravity_strength;
				default_area->gravity_direction = p.gravity_direction;
				default_area->linear_damp = p.linear_damp;
				default_area->angular_damp = p.angular_damp;
				default_area->gravity_point_enabled = p.gravity_point;
				default_area->gravity_point_unit_distance = p.unit_distance;
				default_area->override_gravity_mode = PS2DE::AreaSpaceOverrideMode(p.gravity_mode);
				default_area->override_linear_damp_mode = PS2DE::AreaSpaceOverrideMode(p.linear_mode);
				default_area->override_angular_damp_mode = PS2DE::AreaSpaceOverrideMode(p.angular_mode);
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
		// The destination registry: same symbols, this process's pointers.
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
		for (ObjectPlan &p : plans) {
			table.push_back({ kSymbolObject + p.slot, p.created });
			for (uint32_t s = 0; s < p.shapes.size(); ++s) {
				if (!p.shapes[s].native.is_empty()) {
					table.push_back({ kSymbolShape + p.shapes[s].native[0], &p.created->shapes[s] });
				}
			}
		}
		const SpB2PortableStatus status = spB2PortableRestore(native, native_size, table.ptr(), table.size(), space->world_id);
		if (status != SP_B2_PORTABLE_OK) {
			discard(p_server, space, plans);
			result["error"] = int(status == SP_B2_PORTABLE_UNKNOWN_SYMBOL ? ERR_DOES_NOT_EXIST : status == SP_B2_PORTABLE_INCOMPATIBLE ? ERR_FILE_UNRECOGNIZED : status == SP_B2_PORTABLE_NO_MEMORY ? ERR_OUT_OF_MEMORY : status == SP_B2_PORTABLE_DIGEST_MISMATCH ? ERR_INVALID_DATA : ERR_FILE_CORRUPT);
			result["native_status"] = int(status);
			return result;
		}
		// Bind wrappers to their native bodies and shapes (layout-preserving restore).
		for (ObjectPlan &p : plans) {
			Box2DCollisionObject2D *object = p.created;
			b2BodyId id = b2_nullBodyId;
			void *user = nullptr;
			bool bound = spB2PortableBodyAt(space->world_id, p.slot, &id, &user) && user == object;
			for (uint32_t s = 0; bound && s < p.shapes.size(); ++s) {
				for (uint32_t native_slot_index : p.shapes[s].native) {
					b2ShapeId shape_id = b2_nullShapeId;
					void *shape_user = nullptr;
					if (!spB2PortableShapeAt(space->world_id, native_slot_index, &shape_id, &shape_user) || shape_user != &object->shapes[s] ||
							!B2_ID_EQUALS(b2Shape_GetBody(shape_id), id)) {
						bound = false;
						break;
					}
					object->shapes[s].shape_ids.push_back(shape_id);
				}
			}
			if (!bound) {
				discard(p_server, space, plans);
				result["error"] = int(ERR_INVALID_DATA);
				return result;
			}
			object->body_id = id;
			object->space = space;
			space->objects.insert(object);
		}
		if (anchor != kNone) {
			b2BodyId id = b2_nullBodyId;
			void *user = nullptr;
			if (!spB2PortableBodyAt(space->world_id, anchor, &id, &user) || user) {
				discard(p_server, space, plans);
				result["error"] = int(ERR_INVALID_DATA);
				return result;
			}
			space->world_anchor_body = id;
		}
		space->set_default_area(default_area);
		space->default_gravity = gravity;
		space->contact_hertz = contact[0];
		space->contact_damping_ratio = contact[1];
		space->contact_max_push_speed = contact[2];
		space->substeps = substeps;
		space->last_step = last_step;
		space->linear_damp_changed = damp[0];
		space->angular_damp_changed = damp[1];
		space->constant_force_list.clear();
		for (uint32_t q = 0; q < force_count; ++q) {
			for (ObjectPlan &p : plans) {
				if (!p.area && p.force_queue == q) {
					space->constant_force_list.push_back(p.created->as_body());
				}
			}
		}
		Dictionary bodies;
		for (ObjectPlan &p : plans) {
			if (!p.area) {
				bodies[int64_t(p.source_rid)] = p.created->rid;
			}
		}
		result["error"] = int(OK);
		result["space"] = space_rid;
		result["default_area"] = default_area->rid;
		result["bodies"] = bodies;
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

Dictionary Box2DPortableSpace::restore(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_object_map, const Dictionary &p_shape_map) {
#ifdef EGP_BOX2D_PORTABLE
	std::lock_guard<std::recursive_mutex> guard(egp::box2d::get_simulation_mutex());
	return Box2DPortableSpaceAccess::restore(p_server, p_bytes, p_object_map, p_shape_map);
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
