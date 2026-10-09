// SPDX-License-Identifier: MIT
#include "owned_cold_bridge.h"
#include "contact.h"
#include "joint.h"
SpColdArrayLayout spColdArrayLayout(void){return (SpColdArrayLayout){sizeof(b2Contact),_Alignof(b2Contact),sizeof(b2Joint),_Alignof(b2Joint)};}
bool spPrepareFreeColdContact(void*p,uint32_t generation){if(!p)return false;b2Contact*c=p;*c=(b2Contact){0};c->contactId=c->islandId=c->islandIndex=c->setIndex=c->colorIndex=c->localIndex=c->shapeIdA=c->shapeIdB=-1;for(int e=0;e<2;++e)c->edges[e]=(b2ContactEdge){-1,-1,-1};c->generation=generation;return true;}
bool spPrepareFreeColdJoint(void*p,uint32_t generation){if(!p||generation>UINT16_MAX)return false;b2Joint*j=p;*j=(b2Joint){0};j->jointId=j->islandId=j->islandIndex=j->setIndex=j->colorIndex=j->localIndex=-1;for(int e=0;e<2;++e)j->edges[e]=(b2JointEdge){-1,-1,-1};j->generation=(uint16_t)generation;return true;}
