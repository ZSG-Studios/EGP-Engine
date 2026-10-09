// SPDX-License-Identifier: MIT
// Private C facade: these are scalar staging values, never native wire layout.
#pragma once
#include <box2d/math_functions.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct SpSoftness {float biasRate,massScale,impulseScale;} SpSoftness;
typedef struct SpDistanceJoint {
 b2Vec2 anchorB;
 float motorImpulse;
 float hertz;
 b2Vec2 anchorA;
 float maxLength;
 int indexB;
 SpSoftness distanceSoftness;
 float impulse;
 float dampingRatio;
 bool enableLimit;
 float lowerImpulse;
 float upperSpringForce;
 bool enableMotor;
 bool enableSpring;
 float length;
 float lowerSpringForce;
 float axialMass;
 float maxMotorForce;
 b2Vec2 deltaCenter;
 float upperImpulse;
 int indexA;
 float motorSpeed;
 float minLength;
} SpDistanceJoint;
typedef struct SpMotorJoint {
 float angularDampingRatio;
 b2Vec2 deltaCenter;
 float angularHertz;
 float maxVelocityTorque;
 int indexB;
 b2Vec2 linearVelocityImpulse;
 float linearDampingRatio;
 int indexA;
 b2Vec2 linearSpringImpulse;
 b2Transform frameB;
 b2Mat22 linearMass;
 float angularSpringImpulse;
 float maxSpringForce;
 b2Transform frameA;
 float linearHertz;
 float maxSpringTorque;
 SpSoftness angularSpring;
 SpSoftness linearSpring;
 float angularVelocityImpulse;
 b2Vec2 linearVelocity;
 float angularMass;
 float maxVelocityForce;
 float angularVelocity;
} SpMotorJoint;
typedef struct SpPrismaticJoint {
 float upperTranslation;
 float dampingRatio;
 bool enableMotor;
 float upperImpulse;
 float maxMotorForce;
 float hertz;
 float motorSpeed;
 b2Transform frameA;
 float motorImpulse;
 b2Vec2 deltaCenter;
 int indexA;
 bool enableLimit;
 SpSoftness springSoftness;
 float targetTranslation;
 float lowerImpulse;
 bool enableSpring;
 float lowerTranslation;
 b2Vec2 impulse;
 float springImpulse;
 b2Transform frameB;
 int indexB;
} SpPrismaticJoint;
typedef struct SpRevoluteJoint {
 float lowerImpulse;
 float dampingRatio;
 b2Vec2 deltaCenter;
 float lowerAngle;
 b2Vec2 linearImpulse;
 float motorSpeed;
 bool enableLimit;
 float targetAngle;
 b2Transform frameB;
 int indexB;
 SpSoftness springSoftness;
 float motorImpulse;
 b2Transform frameA;
 float hertz;
 float springImpulse;
 bool enableSpring;
 float upperImpulse;
 float maxMotorTorque;
 float upperAngle;
 int indexA;
 float axialMass;
 bool enableMotor;
} SpRevoluteJoint;
typedef struct SpWeldJoint {
 float axialMass;
 float linearDampingRatio;
 b2Vec2 linearImpulse;
 SpSoftness linearSpring;
 b2Vec2 deltaCenter;
 SpSoftness angularSpring;
 b2Transform frameB;
 float angularDampingRatio;
 int indexB;
 int indexA;
 float linearHertz;
 b2Transform frameA;
 float angularImpulse;
 float angularHertz;
} SpWeldJoint;
typedef struct SpWheelJoint {
 float upperImpulse;
 float springImpulse;
 float motorImpulse;
 float upperTranslation;
 bool enableMotor;
 float perpImpulse;
 float maxMotorTorque;
 float lowerImpulse;
 int indexB;
 float motorMass;
 float motorSpeed;
 SpSoftness springSoftness;
 bool enableSpring;
 float lowerTranslation;
 int indexA;
 b2Transform frameA;
 float dampingRatio;
 float hertz;
 bool enableLimit;
 float axialMass;
 b2Transform frameB;
 float perpMass;
 b2Vec2 deltaCenter;
} SpWheelJoint;
#ifdef __cplusplus
extern "C" {
#endif
bool spExportDistanceJoint(const void *,SpDistanceJoint *);
bool spImportDistanceJoint(const SpDistanceJoint *,void *);
bool spRoundTripDistanceJoint(const SpDistanceJoint *,SpDistanceJoint *);
bool spExportMotorJoint(const void *,SpMotorJoint *);
bool spImportMotorJoint(const SpMotorJoint *,void *);
bool spRoundTripMotorJoint(const SpMotorJoint *,SpMotorJoint *);
bool spExportPrismaticJoint(const void *,SpPrismaticJoint *);
bool spImportPrismaticJoint(const SpPrismaticJoint *,void *);
bool spRoundTripPrismaticJoint(const SpPrismaticJoint *,SpPrismaticJoint *);
bool spExportRevoluteJoint(const void *,SpRevoluteJoint *);
bool spImportRevoluteJoint(const SpRevoluteJoint *,void *);
bool spRoundTripRevoluteJoint(const SpRevoluteJoint *,SpRevoluteJoint *);
bool spExportWeldJoint(const void *,SpWeldJoint *);
bool spImportWeldJoint(const SpWeldJoint *,void *);
bool spRoundTripWeldJoint(const SpWeldJoint *,SpWeldJoint *);
bool spExportWheelJoint(const void *,SpWheelJoint *);
bool spImportWheelJoint(const SpWheelJoint *,void *);
bool spRoundTripWheelJoint(const SpWheelJoint *,SpWheelJoint *);
#ifdef __cplusplus
}
#endif
