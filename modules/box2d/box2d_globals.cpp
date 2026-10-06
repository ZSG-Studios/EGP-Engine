/**************************************************************************/
/*  box2d_globals.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

// SPDX-License-Identifier: MIT
// Adapted from godot-box2d, Copyright (c) 2024-present Andrew Song.
#include "box2d_globals.h"

static_assert(sizeof(b2Pos) == 2 * sizeof(float), "EGP Box2D requires single-precision positions.");

void box2d_set_pixels_per_meter(real_t p_value) {
	// A non-positive length unit zeroes every tolerance Box2D derives from it, so bail before
	// the world is built rather than simulate with no slop and no speculative distance.
	ERR_FAIL_COND_MSG(p_value <= 0.0f, "Pixels per meter must be positive.");

	// Must run before any b2Default*Def call, those bake the length unit into their defaults.
	b2SetLengthUnitsPerMeter(p_value);
}

// TODO: revisit, consider implementing Godot-style cast function
real_t box2d_compute_safe_fraction(real_t p_unsafe_fraction, real_t p_total_distance, real_t p_amount) {
	if (p_amount <= 0.0f) {
		p_amount = 2.0f * B2_LINEAR_SLOP;
	}

	if (p_total_distance <= 0.0f) {
		return 0.0f;
	}

	if (p_unsafe_fraction >= 1.0f) {
		return 1.0f;
	}

	real_t distance = p_unsafe_fraction * p_total_distance;
	real_t adjusted_distance = MAX(0.0f, distance - p_amount);

	return adjusted_distance / p_total_distance;
}

ShapeCollideResult box2d_collide_shapes(
		const Box2DShapePrimitive &p_shape_a,
		const b2Transform &xfa,
		const Box2DShapePrimitive &p_shape_b,
		const b2Transform &xfb,
		bool p_swapped) {
	b2ShapeType type_a = p_shape_a.type;
	b2ShapeType type_b = p_shape_b.type;

	// The collide functions work in frame A and want B relative to it.
	b2Transform xf = b2InvMulTransforms(xfa, xfb);

	b2LocalManifold manifold = {};

	switch (type_a) {
		case b2ShapeType::b2_capsuleShape: {
			b2Capsule a = p_shape_a.capsule;
			switch (type_b) {
				case b2ShapeType::b2_capsuleShape: {
					manifold = b2CollideCapsules(&a, &p_shape_b.capsule, xf);
					break;
				}
				case b2ShapeType::b2_circleShape: {
					manifold = b2CollideCapsuleAndCircle(&a, &p_shape_b.circle, xf);
					break;
				}
				case b2ShapeType::b2_polygonShape:
				case b2ShapeType::b2_segmentShape:
				case b2ShapeType::b2_chainSegmentShape: {
					return box2d_collide_shapes(p_shape_b, xfb, p_shape_a, xfa, true);
				}
				default: {
					ERR_FAIL_V({});
				}
			}
			break;
		}
		case b2ShapeType::b2_circleShape: {
			b2Circle a = p_shape_a.circle;
			switch (type_b) {
				case b2ShapeType::b2_capsuleShape:
				case b2ShapeType::b2_polygonShape:
				case b2ShapeType::b2_segmentShape:
				case b2ShapeType::b2_chainSegmentShape: {
					return box2d_collide_shapes(p_shape_b, xfb, p_shape_a, xfa, true);
				}
				case b2ShapeType::b2_circleShape: {
					manifold = b2CollideCircles(&a, &p_shape_b.circle, xf);
					break;
				}
				default: {
					ERR_FAIL_V({});
				}
			}
			break;
		}
		case b2ShapeType::b2_polygonShape: {
			b2Polygon a = p_shape_a.polygon;
			switch (type_b) {
				case b2ShapeType::b2_capsuleShape: {
					manifold = b2CollidePolygonAndCapsule(&a, &p_shape_b.capsule, xf);
					break;
				}
				case b2ShapeType::b2_circleShape: {
					manifold = b2CollidePolygonAndCircle(&a, &p_shape_b.circle, xf);
					break;
				}
				case b2ShapeType::b2_polygonShape: {
					manifold = b2CollidePolygons(&a, &p_shape_b.polygon, xf);
					break;
				}
				case b2ShapeType::b2_segmentShape:
				case b2ShapeType::b2_chainSegmentShape: {
					return box2d_collide_shapes(p_shape_b, xfb, p_shape_a, xfa, true);
				}
				default: {
					ERR_FAIL_V({});
				}
			}
			break;
		}
		case b2ShapeType::b2_segmentShape: {
			b2Segment a = p_shape_a.segment;
			switch (type_b) {
				case b2ShapeType::b2_capsuleShape: {
					manifold = b2CollideSegmentAndCapsule(&a, &p_shape_b.capsule, xf);
					break;
				}
				case b2ShapeType::b2_circleShape: {
					manifold = b2CollideSegmentAndCircle(&a, &p_shape_b.circle, xf);
					break;
				}
				case b2ShapeType::b2_polygonShape: {
					manifold = b2CollideSegmentAndPolygon(&a, &p_shape_b.polygon, xf);
					break;
				}
				case b2ShapeType::b2_segmentShape:
				case b2ShapeType::b2_chainSegmentShape: {
					return {};
				}
				default: {
					ERR_FAIL_V({});
				}
			}
			break;
		}
		case b2ShapeType::b2_chainSegmentShape: {
			b2ChainSegment a = p_shape_a.chain_segment;
			switch (type_b) {
				case b2ShapeType::b2_capsuleShape: {
					b2SimplexCache cache{};
					manifold = b2CollideChainSegmentAndCapsule(&a, &p_shape_b.capsule, xf, &cache);
					break;
				}
				case b2ShapeType::b2_circleShape: {
					manifold = b2CollideChainSegmentAndCircle(&a, &p_shape_b.circle, xf);
					break;
				}
				case b2ShapeType::b2_polygonShape: {
					b2SimplexCache cache{};
					manifold = b2CollideChainSegmentAndPolygon(&a, &p_shape_b.polygon, xf, &cache);
					break;
				}
				case b2ShapeType::b2_segmentShape:
				case b2ShapeType::b2_chainSegmentShape: {
					return {};
				}
				default: {
					ERR_FAIL_V({});
				}
			}
			break;
		}
		default: {
			ERR_FAIL_V({});
		}
	}

	ShapeCollideResult result;

	result.point_count = manifold.pointCount;

	if (result.point_count == 0) {
		return result;
	}

	result.normal = -to_godot_normalized(b2RotateVector(xfa.q, manifold.normal));

	if (p_swapped) {
		result.normal *= -1.0f;
	}

	// Box2D puts the contact point midway between the surfaces. Godot wants it on one of them.
	for (int i = 0; i < manifold.pointCount; i++) {
		result.points[i].depth = -to_godot(manifold.points[i].separation);
		result.points[i].point = to_godot(b2TransformPoint(xfa, manifold.points[i].point)) + (0.5f * result.points[i].depth * result.normal);
	}

	return result;
}
