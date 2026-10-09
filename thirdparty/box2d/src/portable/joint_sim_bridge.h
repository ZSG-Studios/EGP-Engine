// SPDX-License-Identifier: MIT
#pragma once
#include "joint_bridge.h"
#include <box2d/types.h>
typedef struct SpJointSim {int jointId,bodyIdA,bodyIdB; b2JointType type;b2Transform localFrameA,localFrameB;float invMassA,invMassB,invIA,invIB,constraintHertz,constraintDampingRatio,forceThreshold,torqueThreshold;SpSoftness constraintSoftness;
 SpDistanceJoint distanceJoint;
 SpMotorJoint motorJoint;
 SpPrismaticJoint prismaticJoint;
 SpRevoluteJoint revoluteJoint;
 SpWeldJoint weldJoint;
 SpWheelJoint wheelJoint;
} SpJointSim;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportJointSim(const void *,SpJointSim *);
bool spImportJointSim(const SpJointSim *,void *);
bool spRoundTripJointSim(const SpJointSim *,SpJointSim *);
#ifdef __cplusplus
}
#endif
