// SPDX-License-Identifier: MIT
#include "prepared_map_bridge.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "solver_set.h"
#include "constraint_graph.h"
static bool id_ok(int id,uint32_t n){return id>=0&&(uint32_t)id<n;}
static bool array_ok(const void*p,int n,int cap){return n>=0&&cap>=n&&cap<=100000&&(!cap||p);}
bool spReadPreparedEndpoints(const SpWorldOwnershipView*v,enum SpOwnerKind kind,uint32_t slot,SpPreparedEndpoints*out){
 if(!v||!out||!v->bodies||!v->sets||!v->graph||v->set_count>100000||v->body_count>100000)return false;
 const b2Body*b=v->bodies;const b2SolverSet*sets=v->sets;const b2ConstraintGraph*g=v->graph;SpPreparedEndpoints p={0};int set,color,local;
 if(kind==SP_OWNER_CONTACT){if(slot>=v->contact_count||!v->contacts)return false;const b2Contact*c=(const b2Contact*)v->contacts+slot;if(c->contactId!=(int)slot)return false;set=c->setIndex;color=c->colorIndex;local=c->localIndex;if(!id_ok(set,v->set_count)||sets[set].setIndex!=set||local<0)return false;const b2ContactSim*sim;
  if(color==-1){const b2SolverSet*s=sets+set;if(!array_ok(s->contactSims.data,s->contactSims.count,s->contactSims.capacity)||local>=s->contactSims.count)return false;sim=s->contactSims.data+local;}else{if(set!=b2_awakeSet||color<0||color>=B2_GRAPH_COLOR_COUNT)return false;const b2GraphColor*c2=g->colors+color;if(!array_ok(c2->contactSims.data,c2->contactSims.count,c2->contactSims.capacity)||local>=c2->contactSims.count)return false;sim=c2->contactSims.data+local;}
  if(sim->contactId!=(int)slot)return false;p.body_a=c->edges[0].bodyId;p.body_b=c->edges[1].bodyId;p.index_a=sim->bodySimIndexA;p.index_b=sim->bodySimIndexB;p.has_indices=true;p.active=color!=-1;
#if B2_ENABLE_VALIDATION
  if(sim->bodyIdA!=p.body_a||sim->bodyIdB!=p.body_b)return false;
#endif
 }else if(kind==SP_OWNER_JOINT){if(slot>=v->joint_count||!v->joints)return false;const b2Joint*j=(const b2Joint*)v->joints+slot;if(j->jointId!=(int)slot)return false;set=j->setIndex;color=j->colorIndex;local=j->localIndex;if(!id_ok(set,v->set_count)||sets[set].setIndex!=set||local<0)return false;const b2JointSim*sim;
  if(color==-1){const b2SolverSet*s=sets+set;if(!array_ok(s->jointSims.data,s->jointSims.count,s->jointSims.capacity)||local>=s->jointSims.count)return false;sim=s->jointSims.data+local;}else{if(set!=b2_awakeSet||color<0||color>=B2_GRAPH_COLOR_COUNT)return false;const b2GraphColor*c=g->colors+color;if(!array_ok(c->jointSims.data,c->jointSims.count,c->jointSims.capacity)||local>=c->jointSims.count)return false;sim=c->jointSims.data+local;}
  p.body_a=j->edges[0].bodyId;p.body_b=j->edges[1].bodyId;if(sim->jointId!=(int)slot||sim->type!=j->type||sim->bodyIdA!=p.body_a||sim->bodyIdB!=p.body_b)return false;p.active=color!=-1;p.has_indices=true;
#define READ_INDICES(member) p.index_a=sim->member.indexA;p.index_b=sim->member.indexB;break
  switch(sim->type){case b2_distanceJoint:READ_INDICES(distanceJoint);case b2_motorJoint:READ_INDICES(motorJoint);case b2_prismaticJoint:READ_INDICES(prismaticJoint);case b2_revoluteJoint:READ_INDICES(revoluteJoint);case b2_weldJoint:READ_INDICES(weldJoint);case b2_wheelJoint:READ_INDICES(wheelJoint);case b2_filterJoint:p.has_indices=false;p.index_a=p.index_b=-1;break;default:return false;}
#undef READ_INDICES
 }else return false;
 if(!id_ok(p.body_a,v->body_count)||!id_ok(p.body_b,v->body_count)||p.body_a==p.body_b||b[p.body_a].id!=p.body_a||b[p.body_b].id!=p.body_b)return false;
 if(p.index_a< -1||p.index_b< -1||(p.index_a>=0&&(uint32_t)p.index_a>=v->body_count)||(p.index_b>=0&&(uint32_t)p.index_b>=v->body_count)||(p.index_a>=0&&p.index_a==p.index_b))return false;
 if(p.active&&p.has_indices){int expected_a=b[p.body_a].setIndex==b2_awakeSet?b[p.body_a].localIndex:-1,expected_b=b[p.body_b].setIndex==b2_awakeSet?b[p.body_b].localIndex:-1;if(p.index_a!=expected_a||p.index_b!=expected_b)return false;}
 *out=p;return true;
}
