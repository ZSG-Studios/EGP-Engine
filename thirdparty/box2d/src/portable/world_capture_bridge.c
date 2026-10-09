// SPDX-License-Identifier: MIT
#include "world_capture_bridge.h"
#include "physics_world.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "island.h"
#include "box2d/constants.h"
static bool overlap(const void*p,uint64_t n,const void*q,uint64_t m){uintptr_t a=(uintptr_t)p,b=(uintptr_t)q;return n&&m&&(a<=b?b-a<n:a-b<m);}
static bool array_ok(const void*p,int count,int cap){return count>=0&&cap>=count&&cap<=100000&&(!cap||p);}
static bool bits_overlap(const b2BitSet*b,const void*p,uint64_t bytes){return b->blockCount>b->blockCapacity||b->blockCapacity>100000||(!b->bits&&b->blockCapacity)||overlap(p,bytes,b->bits,(uint64_t)b->blockCapacity*sizeof(uint64_t));}
static void views(const b2World*w,SpWorldCaptureView*v){
 v->source=w;v->ownership=(SpWorldOwnershipView){w->bodies.data,w->contacts.data,w->joints.data,w->islands.data,w->solverSets.data,&w->constraintGraph,(uint32_t)w->bodies.count,(uint32_t)w->contacts.count,(uint32_t)w->joints.count,(uint32_t)w->islands.count,(uint32_t)w->solverSets.count};
 v->geometry=(SpGeometryOwnershipView){&v->ownership,w->shapes.data,w->chainShapes.data,w->sensors.data,&w->broadPhase,(uint32_t)w->shapes.count,(uint32_t)w->chainShapes.count,(uint32_t)w->sensors.count};
 v->step_index=w->stepIndex;v->split_island_id=w->splitIslandId;v->end_event_array_index=w->endEventArrayIndex;v->workers=(uint32_t)w->workerCount;v->native_generation=w->generation;v->native_world_id=w->worldId;
#define VIEW(dst,a) (dst)=(SpWorldArrayView){(a).data,(uint32_t)(a).count,(uint32_t)(a).capacity,sizeof(*(a).data)}
 VIEW(v->cold[0],w->bodies);VIEW(v->cold[1],w->contacts);VIEW(v->cold[2],w->joints);VIEW(v->cold[3],w->islands);VIEW(v->cold[4],w->solverSets);VIEW(v->cold[5],w->shapes);VIEW(v->cold[6],w->chainShapes);VIEW(v->cold[7],w->sensors);
 VIEW(v->events[SP_EVENT_BODY_MOVE],w->bodyMoveEvents);VIEW(v->events[SP_EVENT_SENSOR_BEGIN],w->sensorBeginEvents);VIEW(v->events[SP_EVENT_CONTACT_BEGIN],w->contactBeginEvents);VIEW(v->events[SP_EVENT_SENSOR_END_0],w->sensorEndEvents[0]);VIEW(v->events[SP_EVENT_SENSOR_END_1],w->sensorEndEvents[1]);VIEW(v->events[SP_EVENT_CONTACT_END_0],w->contactEndEvents[0]);VIEW(v->events[SP_EVENT_CONTACT_END_1],w->contactEndEvents[1]);VIEW(v->events[SP_EVENT_CONTACT_HIT],w->contactHitEvents);VIEW(v->events[SP_EVENT_JOINT],w->jointEvents);
#undef VIEW
 const b2IdPool*pools[]={&w->bodyIdPool,&w->contactIdPool,&w->jointIdPool,&w->islandIdPool,&w->solverSetIdPool,&w->shapeIdPool,&w->chainIdPool};
 for(uint32_t i=0;i<SP_POOL_COUNT;++i)v->pools[i]=(SpPoolView){pools[i]->freeArray.data,(uint32_t)pools[i]->freeArray.count,(uint32_t)pools[i]->freeArray.capacity,(uint32_t)pools[i]->nextIndex};
}
bool spWorldCaptureOverlaps(const void*source,const void*p,uint64_t bytes){
 if(!source)return true;const b2World*w=source;
 if(!w->inUse||w->locked||w->activeTaskCount!=0||w->userTreeTask||w->workerCount<1||w->workerCount>B2_MAX_WORKERS||w->endEventArrayIndex<0||w->endEventArrayIndex>1)return true;
 if(w->stack.index!=0||w->stack.allocation!=0||w->stack.capacity<0||w->stack.capacity>1073741824||w->stack.maxAllocation<0||w->stack.maxAllocation>1073741824||(!w->stack.data&&w->stack.capacity)||w->stack.entries.count!=0)return true;
 if(overlap(p,bytes,w,sizeof(*w))||overlap(p,bytes,w->stack.data,(uint64_t)w->stack.capacity))return true;
#define ARRAY(a) do{if(!array_ok((a).data,(a).count,(a).capacity)||overlap(p,bytes,(a).data,(uint64_t)(a).capacity*sizeof(*(a).data)))return true;}while(0)
 ARRAY(w->stack.entries);ARRAY(w->bodies);ARRAY(w->contacts);ARRAY(w->joints);ARRAY(w->islands);ARRAY(w->solverSets);ARRAY(w->shapes);ARRAY(w->chainShapes);ARRAY(w->sensors);
 ARRAY(w->taskContexts);ARRAY(w->sensorTaskContexts);if(w->taskContexts.count!=w->workerCount||w->sensorTaskContexts.count!=w->workerCount)return true;
 ARRAY(w->bodyMoveEvents);ARRAY(w->sensorBeginEvents);ARRAY(w->contactBeginEvents);ARRAY(w->sensorEndEvents[0]);ARRAY(w->sensorEndEvents[1]);ARRAY(w->contactEndEvents[0]);ARRAY(w->contactEndEvents[1]);ARRAY(w->contactHitEvents);ARRAY(w->jointEvents);
 const b2IdPool*pools[]={&w->bodyIdPool,&w->contactIdPool,&w->jointIdPool,&w->islandIdPool,&w->solverSetIdPool,&w->shapeIdPool,&w->chainIdPool};
 const int counts[]={w->bodies.count,w->contacts.count,w->joints.count,w->islands.count,w->solverSets.count,w->shapes.count,w->chainShapes.count};
 for(uint32_t i=0;i<SP_POOL_COUNT;++i){const b2IdPool*pool=pools[i];if(pool->nextIndex!=counts[i]||pool->freeArray.count>pool->nextIndex)return true;ARRAY(pool->freeArray);}
 for(int i=0;i<w->taskContexts.count;++i){const b2TaskContext*t=w->taskContexts.data+i;ARRAY(t->sensorHits);if(bits_overlap(&t->contactStateBitSet,p,bytes)||bits_overlap(&t->hitEventBitSet,p,bytes)||bits_overlap(&t->jointStateBitSet,p,bytes)||bits_overlap(&t->enlargedSimBitSet,p,bytes)||bits_overlap(&t->awakeIslandBitSet,p,bytes))return true;}
 for(int i=0;i<w->sensorTaskContexts.count;++i)if(bits_overlap(&w->sensorTaskContexts.data[i].eventBits,p,bytes))return true;
 if(bits_overlap(&w->debugBodySet,p,bytes)||bits_overlap(&w->debugContactSet,p,bytes)||bits_overlap(&w->debugJointSet,p,bytes)||bits_overlap(&w->debugIslandSet,p,bytes))return true;
#undef ARRAY
 SpWorldCaptureView v={0};views(w,&v);return spWorldOwnershipOverlaps(&v.ownership,p,bytes)||spGeometryOwnershipOverlaps(&v.geometry,p,bytes);
}
bool spCaptureWorldStructure(const void*source,SpWorldCaptureView*out){
 if(!out||spWorldCaptureOverlaps(source,out,sizeof(*out)))return false;
 const b2World*w=source;SpWorldCaptureView candidate={0};views(w,&candidate);
 if(w->splitIslandId!=-1&&(w->splitIslandId<0||w->splitIslandId>=w->islands.count||w->islands.data[w->splitIslandId].islandId!=w->splitIslandId))return false;
 *out=candidate;out->geometry.world=&out->ownership;return true;
}
bool spObserveWorldCaptureSlot(const SpWorldCaptureView*v,enum SpWorldPool pool,uint32_t slot,SpNativeLifetime*out){
 if(!v||pool<SP_POOL_BODY||pool>=SP_POOL_COUNT||slot>=v->pools[pool].allocated_count)return false;
 return pool<=SP_POOL_SET?spObserveWorldOwner(&v->ownership,(enum SpOwnerKind)pool,slot,out):spObserveGeometryOwner(&v->geometry,pool==SP_POOL_CHAIN,slot,out);
}
bool spWorldCaptureMatches(const SpWorldCaptureView*v){
 if(!v)return false;SpWorldCaptureView actual={0};if(!spCaptureWorldStructure(v->source,&actual))return false;
 if(v->geometry.world!=&v->ownership||v->step_index!=actual.step_index||v->split_island_id!=actual.split_island_id||v->end_event_array_index!=actual.end_event_array_index||v->workers!=actual.workers||v->native_generation!=actual.native_generation||v->native_world_id!=actual.native_world_id)return false;
 for(uint32_t i=0;i<SP_POOL_COUNT;++i){const SpPoolView*a=v->pools+i,*b=actual.pools+i;if(a->free_entries!=b->free_entries||a->free_count!=b->free_count||a->free_capacity!=b->free_capacity||a->allocated_count!=b->allocated_count)return false;}
 for(uint32_t i=0;i<8;++i){const SpWorldArrayView*a=v->cold+i,*b=actual.cold+i;if(a->data!=b->data||a->count!=b->count||a->capacity!=b->capacity||a->element_bytes!=b->element_bytes)return false;}
 for(uint32_t i=0;i<SP_EVENT_COUNT;++i){const SpWorldArrayView*a=v->events+i,*b=actual.events+i;if(a->data!=b->data||a->count!=b->count||a->capacity!=b->capacity||a->element_bytes!=b->element_bytes)return false;}
 const SpWorldOwnershipView*a=&v->ownership,*b=&actual.ownership;if(a->bodies!=b->bodies||a->contacts!=b->contacts||a->joints!=b->joints||a->islands!=b->islands||a->sets!=b->sets||a->graph!=b->graph||a->body_count!=b->body_count||a->contact_count!=b->contact_count||a->joint_count!=b->joint_count||a->island_count!=b->island_count||a->set_count!=b->set_count)return false;
 const SpGeometryOwnershipView*g=&v->geometry,*h=&actual.geometry;return g->shapes==h->shapes&&g->chains==h->chains&&g->sensors==h->sensors&&g->broadphase==h->broadphase&&g->shape_count==h->shape_count&&g->chain_count==h->chain_count&&g->sensor_count==h->sensor_count;
}
