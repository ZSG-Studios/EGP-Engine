// SPDX-License-Identifier: MIT
#include "cold_joint_bridge.h"
#include "joint.h"
bool spExportColdJoint(const void *pointer,SpColdJoint *out){if(!pointer||!out)return false;const b2Joint *native=pointer;SpColdJoint s={0};
 s.userData=native->userData;
 s.setIndex=native->setIndex;
 s.colorIndex=native->colorIndex;
 s.localIndex=native->localIndex;
 s.edges[0].bodyId=native->edges[0].bodyId;
 s.edges[0].prevKey=native->edges[0].prevKey;
 s.edges[0].nextKey=native->edges[0].nextKey;
 s.edges[1].bodyId=native->edges[1].bodyId;
 s.edges[1].prevKey=native->edges[1].prevKey;
 s.edges[1].nextKey=native->edges[1].nextKey;
 s.jointId=native->jointId;
 s.islandId=native->islandId;
 s.islandIndex=native->islandIndex;
 s.drawScale=native->drawScale;
 s.type=native->type;
 s.generation=native->generation;
 s.collideConnected=native->collideConnected;
 *out=s;return true;}
bool spImportColdJoint(const SpColdJoint *input,void *pointer){if(!input||!pointer)return false;b2Joint s={0};
 s.userData=input->userData;
 s.setIndex=input->setIndex;
 s.colorIndex=input->colorIndex;
 s.localIndex=input->localIndex;
 s.edges[0].bodyId=input->edges[0].bodyId;
 s.edges[0].prevKey=input->edges[0].prevKey;
 s.edges[0].nextKey=input->edges[0].nextKey;
 s.edges[1].bodyId=input->edges[1].bodyId;
 s.edges[1].prevKey=input->edges[1].prevKey;
 s.edges[1].nextKey=input->edges[1].nextKey;
 s.jointId=input->jointId;
 s.islandId=input->islandId;
 s.islandIndex=input->islandIndex;
 s.drawScale=input->drawScale;
 s.type=input->type;
 s.generation=input->generation;
 s.collideConnected=input->collideConnected;
 *(b2Joint *)pointer=s;return true;}
bool spRoundTripColdJoint(const SpColdJoint *input,SpColdJoint *out){b2Joint s={0};return spImportColdJoint(input,&s)&&spExportColdJoint(&s,out);}
