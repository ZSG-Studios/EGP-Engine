// SPDX-License-Identifier: MIT
#include "world_digest_bridge.h"
#include "physics_world.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "shape.h"
static const b2IdPool*pool_of(const b2World*w,uint32_t p){switch(p){case 0:return &w->bodyIdPool;case 1:return &w->contactIdPool;case 2:return &w->jointIdPool;case 3:return &w->islandIdPool;case 4:return &w->solverSetIdPool;case 5:return &w->shapeIdPool;case 6:return &w->chainIdPool;default:return NULL;}}
bool spWorldPoolView(const void*p,uint32_t pool,SpPoolView*out){if(!p||!out)return false;const b2IdPool*ip=pool_of(p,pool);return ip&&spExportPool(ip,out);}
bool spWorldSlotGeneration(const void*p,uint32_t pool,uint32_t slot,uint32_t*g){
 if(!p||!g)return false;const b2World*w=p;
 switch(pool){
 case 0:if(slot>=(uint32_t)w->bodies.count)return false;*g=w->bodies.data[slot].generation;return true;
 case 1:if(slot>=(uint32_t)w->contacts.count)return false;*g=w->contacts.data[slot].generation;return true;
 case 2:if(slot>=(uint32_t)w->joints.count)return false;*g=w->joints.data[slot].generation;return true;
 case 3:if(slot>=(uint32_t)w->islands.count)return false;*g=0;return true;
 case 4:if(slot>=(uint32_t)w->solverSets.count)return false;*g=0;return true;
 case 5:if(slot>=(uint32_t)w->shapes.count)return false;*g=w->shapes.data[slot].generation;return true;
 case 6:if(slot>=(uint32_t)w->chainShapes.count)return false;*g=w->chainShapes.data[slot].generation;return true;
 default:return false;}
}
