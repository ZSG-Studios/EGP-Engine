// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#pragma once
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid_owner.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"
#include "servers/physics_3d/physics_server_3d.h"

#include "modules/box3d/simulation_guard.h"

#include <box3d/box3d.h>
#include <box3d/collision.h>
#include <box3d/math_functions.h>

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

template <typename T>
struct Box3DRIDOrder {
	bool operator()(T *a, T *b) const { return a->get_rid().get_id() < b->get_rid().get_id(); }
};
template <typename T>
LocalVector<T *> box3d_sorted(const HashSet<T *> &set) {
	LocalVector<T *> out;
	out.reserve(set.size());
	for (T *value : set) {
		out.push_back(value);
	}
	out.template sort_custom<Box3DRIDOrder<T>>();
	return out;
}
