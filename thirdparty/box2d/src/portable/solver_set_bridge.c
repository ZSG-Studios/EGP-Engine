// SPDX-License-Identifier: MIT
#include "solver_set_bridge.h"
#include "solver_set.h"
static bool valid(const SpSolverMembership *s){return s&&s->body_count<=s->body_capacity&&s->state_count<=s->state_capacity&&s->joint_count<=s->joint_capacity&&s->contact_count<=s->contact_capacity&&s->island_count<=s->island_capacity&&(!s->body_count||s->body_ids)&&(!s->joint_count||s->joint_ids)&&(!s->contact_count||s->contact_ids)&&(!s->island_count||s->island_ids);}
bool spExportSolverMembership(const void *p,SpSolverMembership *s){
 if(!p||!valid(s))return false;const b2SolverSet *n=p;
 #define CHECK(a,d) if(n->a.count<0||n->a.capacity<0||n->a.count>n->a.capacity||(uint32_t)n->a.count>s->d##_capacity||(!n->a.data&&n->a.count))return false
 CHECK(bodySims,body);CHECK(bodyStates,state);CHECK(jointSims,joint);CHECK(contactSims,contact);CHECK(islandSims,island);
 #undef CHECK
 for(int i=0;i<n->bodySims.count;++i)s->body_ids[i]=n->bodySims.data[i].bodyId;
 for(int i=0;i<n->jointSims.count;++i)s->joint_ids[i]=n->jointSims.data[i].jointId;
 for(int i=0;i<n->contactSims.count;++i)s->contact_ids[i]=n->contactSims.data[i].contactId;
 for(int i=0;i<n->islandSims.count;++i)s->island_ids[i]=n->islandSims.data[i].islandId;
 s->set_index=n->setIndex;
 #define COPY(a,d) s->d##_count=(uint32_t)n->a.count;s->d##_capacity=(uint32_t)n->a.capacity
 COPY(bodySims,body);COPY(bodyStates,state);COPY(jointSims,joint);COPY(contactSims,contact);COPY(islandSims,island);
 #undef COPY
 return true;
}
bool spSolverMembershipFixtureRoundTrip(const SpSolverMembership *in,SpSolverMembership *out){
 if(!valid(in)||in->body_capacity>4||in->state_capacity>4||in->joint_capacity>4||in->contact_capacity>4||in->island_capacity>4)return false;
 b2BodySim bodies[4]={0};b2BodyState states[4]={0};b2JointSim joints[4]={0};b2ContactSim contacts[4]={0};b2IslandSim islands[4]={0};b2SolverSet n={0};
 for(uint32_t i=0;i<in->body_count;++i)bodies[i].bodyId=in->body_ids[i];
 for(uint32_t i=0;i<in->joint_count;++i)joints[i].jointId=in->joint_ids[i];
 for(uint32_t i=0;i<in->contact_count;++i)contacts[i].contactId=in->contact_ids[i];
 for(uint32_t i=0;i<in->island_count;++i)islands[i].islandId=in->island_ids[i];
 n.setIndex=in->set_index;
 #define ATTACH(a,d,p) n.a.data=p;n.a.count=in->d##_count;n.a.capacity=in->d##_capacity
 ATTACH(bodySims,body,bodies);ATTACH(bodyStates,state,states);ATTACH(jointSims,joint,joints);ATTACH(contactSims,contact,contacts);ATTACH(islandSims,island,islands);
 #undef ATTACH
 return spExportSolverMembership(&n,out);
}

bool spIslandSimNativeRoundTrip(int id,int *out){if(!out)return false;b2IslandSim sim={0};sim.islandId=id;*out=sim.islandId;return true;}
