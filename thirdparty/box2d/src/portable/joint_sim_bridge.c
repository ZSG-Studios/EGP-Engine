// SPDX-License-Identifier: MIT
#include "joint_sim_bridge.h"
#include "joint.h"
bool spExportJointSim(const void *pointer,SpJointSim *out){if(!pointer||!out)return false;const b2JointSim *native=pointer;SpJointSim staged={0};
 staged.constraintSoftness.impulseScale=native->constraintSoftness.impulseScale;
 staged.constraintHertz=native->constraintHertz;
 staged.forceThreshold=native->forceThreshold;
 staged.localFrameA.q.s=native->localFrameA.q.s;
 staged.localFrameA.p.x=native->localFrameA.p.x;
 staged.constraintDampingRatio=native->constraintDampingRatio;
 staged.constraintSoftness.biasRate=native->constraintSoftness.biasRate;
 staged.localFrameA.q.c=native->localFrameA.q.c;
 staged.invMassB=native->invMassB;
 staged.invIA=native->invIA;
 staged.bodyIdA=native->bodyIdA;
 staged.type=native->type;
 staged.invMassA=native->invMassA;
 staged.localFrameB.p.y=native->localFrameB.p.y;
 staged.constraintSoftness.massScale=native->constraintSoftness.massScale;
 staged.localFrameA.p.y=native->localFrameA.p.y;
 staged.localFrameB.q.s=native->localFrameB.q.s;
 staged.torqueThreshold=native->torqueThreshold;
 staged.jointId=native->jointId;
 staged.localFrameB.q.c=native->localFrameB.q.c;
 staged.bodyIdB=native->bodyIdB;
 staged.localFrameB.p.x=native->localFrameB.p.x;
 staged.invIB=native->invIB;
 switch(native->type){
 case b2_filterJoint:break;
 case 0:if(!spExportDistanceJoint(&native->distanceJoint,&staged.distanceJoint))return false;break;
 case 2:if(!spExportMotorJoint(&native->motorJoint,&staged.motorJoint))return false;break;
 case 3:if(!spExportPrismaticJoint(&native->prismaticJoint,&staged.prismaticJoint))return false;break;
 case 4:if(!spExportRevoluteJoint(&native->revoluteJoint,&staged.revoluteJoint))return false;break;
 case 5:if(!spExportWeldJoint(&native->weldJoint,&staged.weldJoint))return false;break;
 case 6:if(!spExportWheelJoint(&native->wheelJoint,&staged.wheelJoint))return false;break;
 default:return false;}*out=staged;return true;}
bool spImportJointSim(const SpJointSim *input,void *pointer){if(!input||!pointer)return false;b2JointSim staged={0};
 staged.constraintSoftness.impulseScale=input->constraintSoftness.impulseScale;
 staged.constraintHertz=input->constraintHertz;
 staged.forceThreshold=input->forceThreshold;
 staged.localFrameA.q.s=input->localFrameA.q.s;
 staged.localFrameA.p.x=input->localFrameA.p.x;
 staged.constraintDampingRatio=input->constraintDampingRatio;
 staged.constraintSoftness.biasRate=input->constraintSoftness.biasRate;
 staged.localFrameA.q.c=input->localFrameA.q.c;
 staged.invMassB=input->invMassB;
 staged.invIA=input->invIA;
 staged.bodyIdA=input->bodyIdA;
 staged.type=input->type;
 staged.invMassA=input->invMassA;
 staged.localFrameB.p.y=input->localFrameB.p.y;
 staged.constraintSoftness.massScale=input->constraintSoftness.massScale;
 staged.localFrameA.p.y=input->localFrameA.p.y;
 staged.localFrameB.q.s=input->localFrameB.q.s;
 staged.torqueThreshold=input->torqueThreshold;
 staged.jointId=input->jointId;
 staged.localFrameB.q.c=input->localFrameB.q.c;
 staged.bodyIdB=input->bodyIdB;
 staged.localFrameB.p.x=input->localFrameB.p.x;
 staged.invIB=input->invIB;
 switch(input->type){
 case b2_filterJoint:break;
 case 0:if(!spImportDistanceJoint(&input->distanceJoint,&staged.distanceJoint))return false;break;
 case 2:if(!spImportMotorJoint(&input->motorJoint,&staged.motorJoint))return false;break;
 case 3:if(!spImportPrismaticJoint(&input->prismaticJoint,&staged.prismaticJoint))return false;break;
 case 4:if(!spImportRevoluteJoint(&input->revoluteJoint,&staged.revoluteJoint))return false;break;
 case 5:if(!spImportWeldJoint(&input->weldJoint,&staged.weldJoint))return false;break;
 case 6:if(!spImportWheelJoint(&input->wheelJoint,&staged.wheelJoint))return false;break;
 default:return false;}*(b2JointSim *)pointer=staged;return true;}
bool spRoundTripJointSim(const SpJointSim *in,SpJointSim *out){b2JointSim native={0};return spImportJointSim(in,&native)&&spExportJointSim(&native,out);}
