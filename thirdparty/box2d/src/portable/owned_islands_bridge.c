// SPDX-License-Identifier: MIT
#include "owned_islands_bridge.h"
#include "island.h"
#include <stddef.h>
SpNativeIslandLayout spNativeIslandLayout(void){return (SpNativeIslandLayout){sizeof(b2Island),_Alignof(b2Island),sizeof(b2ContactLink),_Alignof(b2ContactLink)};}
SpOwnedIslandDescriptor spOwnedIslandDescriptor(const void*p){if(!p)return (SpOwnedIslandDescriptor){0};const b2Island*n=p;return (SpOwnedIslandDescriptor){n->bodies.data,(uint32_t)n->bodies.capacity};}
bool spPrepareOwnedIsland(void*p,int*bodies,uint32_t bc,void*contacts,uint32_t cc,void*joints,uint32_t jc){
 if(!p||bc>100000||cc>100000||jc>100000||(!bodies&&bc)||(!contacts&&cc)||(!joints&&jc)||sizeof(b2ContactLink)!=sizeof(b2JointLink)||_Alignof(b2ContactLink)!=_Alignof(b2JointLink))return false;
 b2Island*n=p;*n=(b2Island){0};n->setIndex=n->localIndex=n->islandId=-1;n->bodies.data=bodies;n->bodies.capacity=(int)bc;n->contacts.data=contacts;n->contacts.capacity=(int)cc;n->joints.data=joints;n->joints.capacity=(int)jc;return true;
}
