// SPDX-License-Identifier: MIT
#include "contact_bridge.h"
#include "contact.h"
int spContactValidationEnabled(void){
#if B2_ENABLE_VALIDATION
return 1;
#else
return 0;
#endif
}
bool spExportContactSim(const void *pointer,SpContactSim *out){if(!pointer||!out)return false;const b2ContactSim *native=pointer;SpContactSim staged={0};
 staged.bodySimIndexA=native->bodySimIndexA;
 staged.manifold.points[0].separation=native->manifold.points[0].separation;
 staged.manifold.points[0].baseSeparation=native->manifold.points[0].baseSeparation;
 staged.friction=native->friction;
 staged.bodySimIndexB=native->bodySimIndexB;
 staged.cachedRotationA.c=native->cachedRotationA.c;
 staged.invIB=native->invIB;
 staged.manifold.points[1].anchorB.y=native->manifold.points[1].anchorB.y;
 staged.manifold.pointCount=native->manifold.pointCount;
 staged.manifold.normal.x=native->manifold.normal.x;
 staged.cachedRotationB.s=native->cachedRotationB.s;
 staged.cache.count=native->cache.count;
 staged.cachedRelativePose.p.x=native->cachedRelativePose.p.x;
 staged.manifold.points[1].normalVelocity=native->manifold.points[1].normalVelocity;
 staged.cache.indexB[0]=native->cache.indexB[0];
 staged.cachedRelativePose.q.s=native->cachedRelativePose.q.s;
 staged.cachedRelativePose.p.y=native->cachedRelativePose.p.y;
 staged.cache.indexA[2]=native->cache.indexA[2];
 staged.manifold.points[0].normalImpulse=native->manifold.points[0].normalImpulse;
 staged.manifold.points[0].anchorB.x=native->manifold.points[0].anchorB.x;
 staged.manifold.points[1].normalImpulse=native->manifold.points[1].normalImpulse;
 staged.invMassA=native->invMassA;
 staged.rollingResistance=native->rollingResistance;
 staged.contactId=native->contactId;
 staged.manifold.points[0].anchorB.y=native->manifold.points[0].anchorB.y;
 staged.manifold.points[1].anchorB.x=native->manifold.points[1].anchorB.x;
 staged.shapeIdB=native->shapeIdB;
 staged.cache.indexB[2]=native->cache.indexB[2];
 staged.simFlags=native->simFlags;
 staged.manifold.points[1].separation=native->manifold.points[1].separation;
 staged.manifold.points[0].anchorA.y=native->manifold.points[0].anchorA.y;
 staged.manifold.points[1].anchorA.x=native->manifold.points[1].anchorA.x;
 staged.manifold.points[1].tangentImpulse=native->manifold.points[1].tangentImpulse;
 staged.manifold.points[0].anchorA.x=native->manifold.points[0].anchorA.x;
 staged.tangentSpeed=native->tangentSpeed;
 staged.manifold.points[1].anchorA.y=native->manifold.points[1].anchorA.y;
 staged.manifold.rollingImpulse=native->manifold.rollingImpulse;
 staged.shapeIdA=native->shapeIdA;
 staged.cachedRotationB.c=native->cachedRotationB.c;
 staged.manifold.points[0].id=native->manifold.points[0].id;
#if B2_ENABLE_VALIDATION
 staged.bodyIdA=native->bodyIdA;
#else
 staged.bodyIdA=-1;
#endif
 staged.manifold.normal.y=native->manifold.normal.y;
 staged.cache.indexA[0]=native->cache.indexA[0];
 staged.cache.indexA[1]=native->cache.indexA[1];
 staged.manifold.points[0].persisted=native->manifold.points[0].persisted;
 staged.cachedRelativePose.q.c=native->cachedRelativePose.q.c;
#if B2_ENABLE_VALIDATION
 staged.bodyIdB=native->bodyIdB;
#else
 staged.bodyIdB=-1;
#endif
 staged.invMassB=native->invMassB;
 staged.manifold.points[1].id=native->manifold.points[1].id;
 staged.cache.indexB[1]=native->cache.indexB[1];
 staged.manifold.points[1].persisted=native->manifold.points[1].persisted;
 staged.invIA=native->invIA;
 staged.manifold.points[1].totalNormalImpulse=native->manifold.points[1].totalNormalImpulse;
 staged.cachedRotationA.s=native->cachedRotationA.s;
 staged.manifold.points[1].baseSeparation=native->manifold.points[1].baseSeparation;
 staged.manifold.points[0].totalNormalImpulse=native->manifold.points[0].totalNormalImpulse;
 staged.restitution=native->restitution;
 staged.manifold.points[0].tangentImpulse=native->manifold.points[0].tangentImpulse;
 staged.manifold.points[0].normalVelocity=native->manifold.points[0].normalVelocity;
 *out=staged;return true;}
bool spImportContactSim(const SpContactSim *input,void *pointer){if(!input||!pointer)return false;b2ContactSim native={0};
 native.bodySimIndexA=input->bodySimIndexA;
 native.manifold.points[0].separation=input->manifold.points[0].separation;
 native.manifold.points[0].baseSeparation=input->manifold.points[0].baseSeparation;
 native.friction=input->friction;
 native.bodySimIndexB=input->bodySimIndexB;
 native.cachedRotationA.c=input->cachedRotationA.c;
 native.invIB=input->invIB;
 native.manifold.points[1].anchorB.y=input->manifold.points[1].anchorB.y;
 native.manifold.pointCount=input->manifold.pointCount;
 native.manifold.normal.x=input->manifold.normal.x;
 native.cachedRotationB.s=input->cachedRotationB.s;
 native.cache.count=input->cache.count;
 native.cachedRelativePose.p.x=input->cachedRelativePose.p.x;
 native.manifold.points[1].normalVelocity=input->manifold.points[1].normalVelocity;
 native.cache.indexB[0]=input->cache.indexB[0];
 native.cachedRelativePose.q.s=input->cachedRelativePose.q.s;
 native.cachedRelativePose.p.y=input->cachedRelativePose.p.y;
 native.cache.indexA[2]=input->cache.indexA[2];
 native.manifold.points[0].normalImpulse=input->manifold.points[0].normalImpulse;
 native.manifold.points[0].anchorB.x=input->manifold.points[0].anchorB.x;
 native.manifold.points[1].normalImpulse=input->manifold.points[1].normalImpulse;
 native.invMassA=input->invMassA;
 native.rollingResistance=input->rollingResistance;
 native.contactId=input->contactId;
 native.manifold.points[0].anchorB.y=input->manifold.points[0].anchorB.y;
 native.manifold.points[1].anchorB.x=input->manifold.points[1].anchorB.x;
 native.shapeIdB=input->shapeIdB;
 native.cache.indexB[2]=input->cache.indexB[2];
 native.simFlags=input->simFlags;
 native.manifold.points[1].separation=input->manifold.points[1].separation;
 native.manifold.points[0].anchorA.y=input->manifold.points[0].anchorA.y;
 native.manifold.points[1].anchorA.x=input->manifold.points[1].anchorA.x;
 native.manifold.points[1].tangentImpulse=input->manifold.points[1].tangentImpulse;
 native.manifold.points[0].anchorA.x=input->manifold.points[0].anchorA.x;
 native.tangentSpeed=input->tangentSpeed;
 native.manifold.points[1].anchorA.y=input->manifold.points[1].anchorA.y;
 native.manifold.rollingImpulse=input->manifold.rollingImpulse;
 native.shapeIdA=input->shapeIdA;
 native.cachedRotationB.c=input->cachedRotationB.c;
 native.manifold.points[0].id=input->manifold.points[0].id;
#if B2_ENABLE_VALIDATION
 native.bodyIdA=input->bodyIdA;
#else
 if(input->bodyIdA!=-1)return false;
#endif
 native.manifold.normal.y=input->manifold.normal.y;
 native.cache.indexA[0]=input->cache.indexA[0];
 native.cache.indexA[1]=input->cache.indexA[1];
 native.manifold.points[0].persisted=input->manifold.points[0].persisted;
 native.cachedRelativePose.q.c=input->cachedRelativePose.q.c;
#if B2_ENABLE_VALIDATION
 native.bodyIdB=input->bodyIdB;
#else
 if(input->bodyIdB!=-1)return false;
#endif
 native.invMassB=input->invMassB;
 native.manifold.points[1].id=input->manifold.points[1].id;
 native.cache.indexB[1]=input->cache.indexB[1];
 native.manifold.points[1].persisted=input->manifold.points[1].persisted;
 native.invIA=input->invIA;
 native.manifold.points[1].totalNormalImpulse=input->manifold.points[1].totalNormalImpulse;
 native.cachedRotationA.s=input->cachedRotationA.s;
 native.manifold.points[1].baseSeparation=input->manifold.points[1].baseSeparation;
 native.manifold.points[0].totalNormalImpulse=input->manifold.points[0].totalNormalImpulse;
 native.restitution=input->restitution;
 native.manifold.points[0].tangentImpulse=input->manifold.points[0].tangentImpulse;
 native.manifold.points[0].normalVelocity=input->manifold.points[0].normalVelocity;
 *(b2ContactSim *)pointer=native;return true;}
bool spRoundTripContactSim(const SpContactSim *in,SpContactSim *out){b2ContactSim native={0};return spImportContactSim(in,&native)&&spExportContactSim(&native,out); }
