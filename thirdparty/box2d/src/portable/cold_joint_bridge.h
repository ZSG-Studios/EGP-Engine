// SPDX-License-Identifier: MIT
#pragma once
#include "cold_contact_bridge.h"
#include <box2d/types.h>
typedef struct SpColdJoint {void *userData;int setIndex,colorIndex,localIndex;SpContactEdge edges[2];int jointId,islandId,islandIndex;float drawScale;b2JointType type;uint16_t generation;bool collideConnected;} SpColdJoint;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportColdJoint(const void *,SpColdJoint *);
bool spImportColdJoint(const SpColdJoint *,void *);
bool spRoundTripColdJoint(const SpColdJoint *,SpColdJoint *);
#ifdef __cplusplus
}
#endif
