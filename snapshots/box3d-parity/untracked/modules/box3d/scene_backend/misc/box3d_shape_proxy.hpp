// SPDX-License-Identifier: MIT
// Adapted from godot-box3d, Copyright (c) 2026 Mark Arneman.
#include "precompiled.hpp"
#pragma once

#include <box3d/types.h>

class Box3DShapeImpl3D;

// Builds a b3ShapeProxy (point cloud + radius, as used by b3World_OverlapShape/CastShape)
// matching a given Godot Shape3D: sphere -> 1 point + radius; capsule -> 2 points +
// radius; box/convex hull -> N hull points + 0 radius, all transformed into p_transform's
// space. Mesh/heightfield/world-boundary shapes have no finite point-cloud representation
// usable with Box3D's GJK-based queries, so they report is_supported() == false; callers
// should fall back to a different strategy (e.g. AABB overlap) for those shape types.
class Box3DShapeProxy3D {
public:
	Box3DShapeProxy3D(const Box3DShapeImpl3D *p_shape, const Transform3D &p_transform, real_t p_margin = 0.0);

	bool is_supported() const { return supported; }

	const b3ShapeProxy &get_proxy() const { return proxy; }

private:
	LocalVector<b3Vec3> points;
	b3ShapeProxy proxy{};
	bool supported = false;
};
