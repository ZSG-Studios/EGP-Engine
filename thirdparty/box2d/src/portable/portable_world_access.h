// SPDX-License-Identifier: MIT
#pragma once
// Narrow native access used by engine participants that bind wrapper objects
// to a restored world. Every function validates its world id and slot.
#include "box2d/id.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// The world's internal pointer for the portable codecs, or null if invalid.
void*spB2PortableWorldPointer(b2WorldId world);
// Root pointers in a fixed order: friction callback, restitution callback,
// pre-solve function, pre-solve context, custom filter function, custom filter
// context, world user data. Returns false for an invalid world.
bool spB2PortableRootPointers(b2WorldId world,void*out[7]);
// Allocated slot counts of the body and shape pools.
uint32_t spB2PortableBodySlots(b2WorldId world);
uint32_t spB2PortableShapeSlots(b2WorldId world);
// Live ids by native slot, with their user data. False for a free slot.
bool spB2PortableBodyAt(b2WorldId world,uint32_t slot,b2BodyId*out,void**user_data);
bool spB2PortableShapeAt(b2WorldId world,uint32_t slot,b2ShapeId*out,void**user_data);
// True when the world holds no bodies, shapes, chains, contacts or joints.
bool spB2PortableWorldEmpty(b2WorldId world);
#ifdef __cplusplus
}
#endif
