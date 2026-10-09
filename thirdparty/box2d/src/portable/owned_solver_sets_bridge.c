// SPDX-License-Identifier: MIT
#include "owned_solver_sets_bridge.h"
#include "solver_set.h"
#include <string.h>
SpNativeSolverSetLayout spNativeSolverSetLayout(void){return (SpNativeSolverSetLayout){sizeof(b2SolverSet),_Alignof(b2SolverSet),sizeof(b2IslandSim),_Alignof(b2IslandSim)};}
static bool span_ok(const void*p,uint32_t n,uint32_t capacity){return n<=capacity&&capacity<=100000&&(p||!capacity);}
bool spAssembleOwnedSolverSet(void*p,int set_index,const SpOwnedSetArrays*a){
 if(!p||!a||set_index<-1||!span_ok(a->bodies,a->body_count,a->body_capacity)||!span_ok(a->states,a->state_count,a->state_capacity)||!span_ok(a->joints,a->joint_count,a->joint_capacity)||!span_ok(a->contacts,a->contact_count,a->contact_capacity)||!span_ok(a->island_sims,a->island_count,a->island_capacity)||(a->island_count&&!a->island_ids))return false;
 if(set_index==-1&&(a->body_capacity||a->state_capacity||a->joint_capacity||a->contact_capacity||a->island_capacity))return false;
 b2SolverSet*s=p;*s=(b2SolverSet){0};s->setIndex=set_index;
#define BIND(field,ptr,n,c) s->field.data=ptr;s->field.count=(int)(n);s->field.capacity=(int)(c)
 BIND(bodySims,a->bodies,a->body_count,a->body_capacity);BIND(bodyStates,a->states,a->state_count,a->state_capacity);BIND(jointSims,a->joints,a->joint_count,a->joint_capacity);BIND(contactSims,a->contacts,a->contact_count,a->contact_capacity);BIND(islandSims,a->island_sims,a->island_count,a->island_capacity);
#undef BIND
 if(a->island_capacity)memset(a->island_sims,0,(size_t)a->island_capacity*sizeof(b2IslandSim));
 for(uint32_t i=0;i<a->island_count;++i)s->islandSims.data[i].islandId=a->island_ids[i];
 return true;
}
