// SPDX-License-Identifier: MIT
#pragma once
#if defined(BOX2D_AVX2) || defined(BOX2D_DOUBLE_PRECISION) || defined(BOX2D_DISABLE_SIMD)
#error EGP Box2D requires single precision and four-wide SIMD.
#endif
#include "simulation_guard.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid_owner.h"
#include "core/variant/typed_array.h"
#include "scene/2d/physics/animatable_body_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "servers/physics_2d/physics_server_2d.h"

#include <algorithm>
#include <cfenv>
#include <cmath>

template <typename T>
struct Box2DRIDOrder {
	bool operator()(T *a, T *b) const { return a->get_rid().get_id() < b->get_rid().get_id(); }
};
template <typename T>
LocalVector<T *> box2d_sorted(const HashSet<T *> &set) {
	LocalVector<T *> out;
	out.reserve(set.size());
	for (T *value : set) {
		out.push_back(value);
	}
	out.template sort_custom<Box2DRIDOrder<T>>();
	return out;
}

namespace egp::box2d {
extern thread_local HashSet<RID> motion_exclude_bodies;
extern thread_local HashSet<ObjectID> motion_exclude_objects;
} //namespace egp::box2d
