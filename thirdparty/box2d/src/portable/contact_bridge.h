// SPDX-License-Identifier: MIT
#pragma once
#include <box2d/collision.h>
#include <stdbool.h>
typedef struct SpContactSim {
 int contactId;
 b2Rot cachedRotationA;
 b2Rot cachedRotationB;
 b2Transform cachedRelativePose;
 int bodyIdA;
 int bodyIdB;
 int bodySimIndexA;
 int bodySimIndexB;
 int shapeIdA;
 int shapeIdB;
 float invMassA;
 float invIA;
 float invMassB;
 float invIB;
 b2Manifold manifold;
 float friction;
 float restitution;
 float rollingResistance;
 float tangentSpeed;
 uint32_t simFlags;
 b2SimplexCache cache;
} SpContactSim;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportContactSim(const void *,SpContactSim *);
bool spImportContactSim(const SpContactSim *,void *);
bool spRoundTripContactSim(const SpContactSim *,SpContactSim *);
int spContactValidationEnabled(void);
#ifdef __cplusplus
}
#endif
