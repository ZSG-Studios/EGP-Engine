// SPDX-License-Identifier: MIT
#include "portable_world_access.h"
#include "physics_world.h"
#include "body.h"
#include "shape.h"
#include "id_pool.h"
static b2World*world_of(b2WorldId id){if(id.index1<1||id.index1>B2_MAX_WORLDS)return NULL;b2World*w=b2GetWorldFromId(id);return w&&w->inUse&&w->generation==id.generation?w:NULL;}
void*spB2PortableWorldPointer(b2WorldId id){return world_of(id);}
bool spB2PortableRootPointers(b2WorldId id,void*out[7]){b2World*w=world_of(id);if(!w||!out)return false;
 out[0]=(void*)(uintptr_t)w->frictionCallback;out[1]=(void*)(uintptr_t)w->restitutionCallback;out[2]=(void*)(uintptr_t)w->preSolveFcn;out[3]=w->preSolveContext;out[4]=(void*)(uintptr_t)w->customFilterFcn;out[5]=w->customFilterContext;out[6]=w->userData;return true;}
uint32_t spB2PortableBodySlots(b2WorldId id){b2World*w=world_of(id);return w?(uint32_t)w->bodies.count:0;}
uint32_t spB2PortableShapeSlots(b2WorldId id){b2World*w=world_of(id);return w?(uint32_t)w->shapes.count:0;}
bool spB2PortableBodyAt(b2WorldId id,uint32_t slot,b2BodyId*out,void**user_data){b2World*w=world_of(id);if(!w||slot>=(uint32_t)w->bodies.count)return false;const b2Body*b=w->bodies.data+slot;if(b->id!=(int)slot)return false;
 if(out)*out=(b2BodyId){(int)slot+1,(uint16_t)(id.index1-1),b->generation};if(user_data)*user_data=b->userData;return true;}
bool spB2PortableShapeAt(b2WorldId id,uint32_t slot,b2ShapeId*out,void**user_data){b2World*w=world_of(id);if(!w||slot>=(uint32_t)w->shapes.count)return false;const b2Shape*s=w->shapes.data+slot;if(s->id!=(int)slot)return false;
 if(out)*out=(b2ShapeId){(int)slot+1,(uint16_t)(id.index1-1),s->generation};if(user_data)*user_data=s->userData;return true;}
bool spB2PortableWorldEmpty(b2WorldId id){b2World*w=world_of(id);if(!w)return false;
 return b2GetIdCount(&w->bodyIdPool)==0&&b2GetIdCount(&w->shapeIdPool)==0&&b2GetIdCount(&w->chainIdPool)==0&&b2GetIdCount(&w->contactIdPool)==0&&b2GetIdCount(&w->jointIdPool)==0;}
