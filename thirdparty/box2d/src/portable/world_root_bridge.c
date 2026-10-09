// SPDX-License-Identifier: MIT
#include "world_root_bridge.h"
#include "runtime_scratch_bridge.h"
#include "physics_world.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "island.h"
#include "solver_set.h"
#include "constraint_graph.h"
#include <stddef.h>
#include <string.h>
#define FN(p) ((void*)(uintptr_t)(p))
SpWorldRootLayout spWorldRootLayout(void){return (SpWorldRootLayout){sizeof(b2World),_Alignof(b2World)};}
bool spExportWorldRoot(const void*p,SpWorldRoot*o){
 if(!p||!o||!spValidateRuntimeScratchBarrier(p))return false;const b2World*w=p;SpWorldRoot r={0};
 r.gravity[0]=w->gravity.x;r.gravity[1]=w->gravity.y;r.hitEventThreshold=w->hitEventThreshold;r.restitutionThreshold=w->restitutionThreshold;r.maxLinearSpeed=w->maxLinearSpeed;r.contactSpeed=w->contactSpeed;r.contactHertz=w->contactHertz;r.contactDampingRatio=w->contactDampingRatio;r.contactRecycleDistance=w->contactRecycleDistance;r.inv_h=w->inv_h;r.inv_dt=w->inv_dt;
 r.enableSleep=w->enableSleep;r.enableWarmStarting=w->enableWarmStarting;r.enableContactSoftening=w->enableContactSoftening;r.enableContinuous=w->enableContinuous;r.enableSpeculative=w->enableSpeculative;
 r.stepIndex=w->stepIndex;r.splitIslandId=w->splitIslandId;r.endEventArrayIndex=w->endEventArrayIndex;
 r.maxCapacity[0]=w->maxCapacity.staticShapeCount;r.maxCapacity[1]=w->maxCapacity.dynamicShapeCount;r.maxCapacity[2]=w->maxCapacity.staticBodyCount;r.maxCapacity[3]=w->maxCapacity.dynamicBodyCount;r.maxCapacity[4]=w->maxCapacity.contactCount;
 r.frictionCallback=FN(w->frictionCallback);r.restitutionCallback=FN(w->restitutionCallback);r.preSolveFcn=FN(w->preSolveFcn);r.preSolveContext=w->preSolveContext;r.customFilterFcn=FN(w->customFilterFcn);r.customFilterContext=w->customFilterContext;r.userData=w->userData;
 *o=r;return true;
}
bool spImportWorldRoot(const SpWorldRoot*r,void*p){
 if(!r||!p||r->endEventArrayIndex<0||r->endEventArrayIndex>1||r->splitIslandId<-1)return false;for(int i=0;i<5;++i)if(r->maxCapacity[i]<0)return false;b2World*w=p;
 w->gravity=(b2Vec2){r->gravity[0],r->gravity[1]};w->hitEventThreshold=r->hitEventThreshold;w->restitutionThreshold=r->restitutionThreshold;w->maxLinearSpeed=r->maxLinearSpeed;w->contactSpeed=r->contactSpeed;w->contactHertz=r->contactHertz;w->contactDampingRatio=r->contactDampingRatio;w->contactRecycleDistance=r->contactRecycleDistance;w->inv_h=r->inv_h;w->inv_dt=r->inv_dt;
 w->enableSleep=r->enableSleep;w->enableWarmStarting=r->enableWarmStarting;w->enableContactSoftening=r->enableContactSoftening;w->enableContinuous=r->enableContinuous;w->enableSpeculative=r->enableSpeculative;
 w->stepIndex=r->stepIndex;w->splitIslandId=r->splitIslandId;w->endEventArrayIndex=r->endEventArrayIndex;
 w->maxCapacity=(b2Capacity){r->maxCapacity[0],r->maxCapacity[1],r->maxCapacity[2],r->maxCapacity[3],r->maxCapacity[4]};
 w->frictionCallback=(b2FrictionCallback*)(uintptr_t)r->frictionCallback;w->restitutionCallback=(b2RestitutionCallback*)(uintptr_t)r->restitutionCallback;w->preSolveFcn=(b2PreSolveFcn*)(uintptr_t)r->preSolveFcn;w->preSolveContext=r->preSolveContext;w->customFilterFcn=(b2CustomFilterFcn*)(uintptr_t)r->customFilterFcn;w->customFilterContext=r->customFilterContext;w->userData=r->userData;
 return true;
}
static bool pool_ok(const SpPoolView*v){return v->free_count<=v->free_capacity&&v->free_capacity<=100000&&v->allocated_count<=100000&&(v->free_entries||!v->free_capacity);}
static void bind_pool(b2IdPool*pool,const SpPoolView*v){pool->freeArray.data=(int*)v->free_entries;pool->freeArray.count=(int)v->free_count;pool->freeArray.capacity=(int)v->free_capacity;pool->nextIndex=(int)v->allocated_count;}
bool spAssembleWorldRoot(void*p,const SpWorldRootArrays*a){
 if(!p||!a||!a->graph||!pool_ok(&a->body_pool)||!pool_ok(&a->contact_pool)||!pool_ok(&a->joint_pool)||!pool_ok(&a->island_pool)||!pool_ok(&a->set_pool))return false;
 if(a->body_count>100000||a->contact_count>100000||a->joint_count>100000||a->island_count>100000||a->set_count>100000||(!a->bodies&&a->body_count)||(!a->contacts&&a->contact_count)||(!a->joints&&a->joint_count)||(!a->islands&&a->island_count)||!a->sets)return false;
 b2World*w=p;memset(w,0,sizeof(*w));
#define BIND(field,ptr,n) w->field.data=ptr;w->field.count=w->field.capacity=(int)(n)
 BIND(bodies,a->bodies,a->body_count);BIND(contacts,a->contacts,a->contact_count);BIND(joints,a->joints,a->joint_count);BIND(islands,a->islands,a->island_count);BIND(solverSets,a->sets,a->set_count);
#undef BIND
 w->constraintGraph=*(const b2ConstraintGraph*)a->graph;
 bind_pool(&w->bodyIdPool,&a->body_pool);bind_pool(&w->contactIdPool,&a->contact_pool);bind_pool(&w->jointIdPool,&a->joint_pool);bind_pool(&w->islandIdPool,&a->island_pool);bind_pool(&w->solverSetIdPool,&a->set_pool);
 w->splitIslandId=-1;return true;
}
bool spWorldRootOwnershipView(const void*p,SpWorldOwnershipView*v){
 if(!p||!v)return false;const b2World*w=p;if(w->bodies.count<0||w->contacts.count<0||w->joints.count<0||w->islands.count<0||w->solverSets.count<0)return false;
 *v=(SpWorldOwnershipView){w->bodies.data,w->contacts.data,w->joints.data,w->islands.data,w->solverSets.data,&w->constraintGraph,(uint32_t)w->bodies.count,(uint32_t)w->contacts.count,(uint32_t)w->joints.count,(uint32_t)w->islands.count,(uint32_t)w->solverSets.count};return true;
}
static bool pool_matches(const b2IdPool*pool,int count,const void*data,size_t stride,size_t id_offset){
 if(pool->nextIndex!=count||pool->freeArray.count<0||pool->freeArray.count>count)return false;int free=0;
 for(int i=0;i<count;++i){int id;memcpy(&id,(const char*)data+(size_t)i*stride+id_offset,sizeof(id));if(id==-1)++free;else if(id!=i)return false;}
 if(free!=pool->freeArray.count)return false;
 for(int k=0;k<pool->freeArray.count;++k){int s=pool->freeArray.data[k];if(s<0||s>=count)return false;int id;memcpy(&id,(const char*)data+(size_t)s*stride+id_offset,sizeof(id));if(id!=-1)return false;for(int q=0;q<k;++q)if(pool->freeArray.data[q]==s)return false;}
 return true;
}
bool spValidateWorldRootPools(const void*p){
 if(!p)return false;const b2World*w=p;
 if(!pool_matches(&w->bodyIdPool,w->bodies.count,w->bodies.data,sizeof(b2Body),offsetof(b2Body,id))||!pool_matches(&w->contactIdPool,w->contacts.count,w->contacts.data,sizeof(b2Contact),offsetof(b2Contact,contactId))||!pool_matches(&w->jointIdPool,w->joints.count,w->joints.data,sizeof(b2Joint),offsetof(b2Joint,jointId))||!pool_matches(&w->islandIdPool,w->islands.count,w->islands.data,sizeof(b2Island),offsetof(b2Island,islandId))||!pool_matches(&w->solverSetIdPool,w->solverSets.count,w->solverSets.data,sizeof(b2SolverSet),offsetof(b2SolverSet,setIndex)))return false;
 if(w->splitIslandId!=-1&&(w->splitIslandId<0||w->splitIslandId>=w->islands.count||w->islands.data[w->splitIslandId].islandId!=w->splitIslandId))return false;
 return true;
}
