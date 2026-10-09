// SPDX-License-Identifier: MIT
#include "inactive_constraints_bridge.h"
#include "contact.h"
#include "joint.h"
#include "solver_set.h"
SpInactiveConstraintLayout spInactiveConstraintLayout(void){return (SpInactiveConstraintLayout){sizeof(b2ContactSim),_Alignof(b2ContactSim),sizeof(b2JointSim),_Alignof(b2JointSim)};}
bool spInactiveContactAdmissible(const SpContactSim*c,int set){
 if(!c||set<b2_disabledSet)return false;bool touching=(c->simFlags&b2_simTouchingFlag)!=0;
 if(set==b2_disabledSet||set==b2_awakeSet)return !touching&&c->manifold.pointCount==0;
 return touching&&c->manifold.pointCount>0;
}
int*spInactiveJointIndex(SpJointSim*j,int endpoint){
 if(!j||endpoint<0||endpoint>1)return NULL;
#define PICK(m) return endpoint?&j->m.indexB:&j->m.indexA
 switch(j->type){case b2_distanceJoint:PICK(distanceJoint);case b2_motorJoint:PICK(motorJoint);case b2_prismaticJoint:PICK(prismaticJoint);case b2_revoluteJoint:PICK(revoluteJoint);case b2_weldJoint:PICK(weldJoint);case b2_wheelJoint:PICK(wheelJoint);default:return NULL;}
#undef PICK
}
bool spColdContactTouching(const SpColdContact*c){return c&&(c->flags&b2_contactTouchingFlag)!=0;}
