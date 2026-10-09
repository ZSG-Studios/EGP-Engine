// SPDX-License-Identifier: MIT
#include "world_ownership_bridge.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "island.h"
#include "solver_set.h"
#include "constraint_graph.h"

static bool index_ok(int i,uint32_t count){return i>=0&&(uint32_t)i<count;}
static bool array_ok(const void*p,int n,int capacity){return n>=0&&capacity>=n&&capacity<=100000&&(!capacity||p);}
static bool view_ok(const SpWorldOwnershipView*v){return v&&v->graph&&v->body_count<=100000&&v->contact_count<=100000&&v->joint_count<=100000&&v->island_count<=100000&&v->set_count>=3&&v->set_count<=100000&&(!v->body_count||v->bodies)&&(!v->contact_count||v->contacts)&&(!v->joint_count||v->joints)&&(!v->island_count||v->islands)&&v->sets;}
#define ARRAY_OK(a) array_ok((a).data,(a).count,(a).capacity)

static const b2ContactSim* contact_sim(const SpWorldOwnershipView*v,const b2Contact*c){
 const b2SolverSet*sets=v->sets;const b2ConstraintGraph*g=v->graph;
 if(!index_ok(c->setIndex,v->set_count)||sets[c->setIndex].setIndex!=c->setIndex||c->localIndex<0)return NULL;
 if(c->colorIndex==-1){const b2SolverSet*s=sets+c->setIndex;return c->localIndex<s->contactSims.count?s->contactSims.data+c->localIndex:NULL;}
 if(c->setIndex!=b2_awakeSet||c->colorIndex<0||c->colorIndex>=B2_GRAPH_COLOR_COUNT)return NULL;
 const b2GraphColor*color=g->colors+c->colorIndex;return c->localIndex<color->contactSims.count?color->contactSims.data+c->localIndex:NULL;
}
static const b2JointSim* joint_sim(const SpWorldOwnershipView*v,const b2Joint*j){
 const b2SolverSet*sets=v->sets;const b2ConstraintGraph*g=v->graph;
 if(!index_ok(j->setIndex,v->set_count)||sets[j->setIndex].setIndex!=j->setIndex||j->localIndex<0)return NULL;
 if(j->colorIndex==-1){const b2SolverSet*s=sets+j->setIndex;return j->localIndex<s->jointSims.count?s->jointSims.data+j->localIndex:NULL;}
 if(j->setIndex!=b2_awakeSet||j->colorIndex<0||j->colorIndex>=B2_GRAPH_COLOR_COUNT)return NULL;
 const b2GraphColor*color=g->colors+j->colorIndex;return j->localIndex<color->jointSims.count?color->jointSims.data+j->localIndex:NULL;
}
static bool contact_edges(const SpWorldOwnershipView*v,const b2Contact*x){
 const b2Body*b=v->bodies;const b2Contact*c=v->contacts;
 for(int e=0;e<2;++e){int own=2*x->contactId+e,prev=x->edges[e].prevKey,next=x->edges[e].nextKey,body=x->edges[e].bodyId;if(prev==-1){if(b[body].headContactKey!=own)return false;}else{if(prev<0||!index_ok(prev>>1,v->contact_count))return false;const b2Contact*p=c+(prev>>1);if(p->contactId!=(prev>>1)||p->edges[prev&1].bodyId!=body||p->edges[prev&1].nextKey!=own)return false;}if(next!=-1){if(next<0||!index_ok(next>>1,v->contact_count))return false;const b2Contact*n=c+(next>>1);if(n->contactId!=(next>>1)||n->edges[next&1].bodyId!=body||n->edges[next&1].prevKey!=own)return false;}}
 return true;
}
static bool joint_edges(const SpWorldOwnershipView*v,const b2Joint*x){
 const b2Body*b=v->bodies;const b2Joint*j=v->joints;
 for(int e=0;e<2;++e){int own=2*x->jointId+e,prev=x->edges[e].prevKey,next=x->edges[e].nextKey,body=x->edges[e].bodyId;if(prev==-1){if(b[body].headJointKey!=own)return false;}else{if(prev<0||!index_ok(prev>>1,v->joint_count))return false;const b2Joint*p=j+(prev>>1);if(p->jointId!=(prev>>1)||p->edges[prev&1].bodyId!=body||p->edges[prev&1].nextKey!=own)return false;}if(next!=-1){if(next<0||!index_ok(next>>1,v->joint_count))return false;const b2Joint*n=j+(next>>1);if(n->jointId!=(next>>1)||n->edges[next&1].bodyId!=body||n->edges[next&1].prevKey!=own)return false;}}
 return true;
}
bool spValidateWorldOwnership(const SpWorldOwnershipView*v){
 if(!view_ok(v))return false;
 const b2Body*b=v->bodies;const b2Contact*c=v->contacts;const b2Joint*j=v->joints;const b2Island*is=v->islands;const b2SolverSet*sets=v->sets;const b2ConstraintGraph*g=v->graph;
 uint64_t contact_edges_seen=0,joint_edges_seen=0,live_contacts=0,live_joints=0;
 // Validate every nested allocation descriptor before dereferencing children.
 for(uint32_t k=0;k<v->set_count;++k){const b2SolverSet*s=sets+k;if(!ARRAY_OK(s->bodySims)||!ARRAY_OK(s->bodyStates)||!ARRAY_OK(s->contactSims)||!ARRAY_OK(s->jointSims)||!ARRAY_OK(s->islandSims))return false;if(s->setIndex==-1){if(k<3||s->bodySims.count||s->bodyStates.count||s->contactSims.count||s->jointSims.count||s->islandSims.count)return false;continue;}if(s->setIndex!=(int)k)return false;if(k==b2_awakeSet){if(s->bodySims.count!=s->bodyStates.count||s->jointSims.count)return false;}else if(s->bodyStates.count)return false;if(k<2&&s->islandSims.count)return false;if(k==0&&s->contactSims.count)return false;}
 for(int k=0;k<B2_GRAPH_COLOR_COUNT;++k){const b2GraphColor*color=g->colors+k;if(!ARRAY_OK(color->contactSims)||!ARRAY_OK(color->jointSims))return false;}
 for(uint32_t k=0;k<v->island_count;++k)if(is[k].islandId!=-1&&(!ARRAY_OK(is[k].bodies)||!ARRAY_OK(is[k].contacts)||!ARRAY_OK(is[k].joints)))return false;
 // Each solver slot has exactly one cold owner; checking its stored local index
 // rejects duplicates without allocating a world-sized visited table.
 for(uint32_t k=0;k<v->set_count;++k){const b2SolverSet*s=sets+k;if(s->setIndex==-1)continue;
  for(int q=0;q<s->bodySims.count;++q){int id=s->bodySims.data[q].bodyId;if(!index_ok(id,v->body_count)||b[id].id!=id||b[id].setIndex!=(int)k||b[id].localIndex!=q)return false;}
  for(int q=0;q<s->contactSims.count;++q){int id=s->contactSims.data[q].contactId;if(!index_ok(id,v->contact_count)||c[id].contactId!=id||c[id].setIndex!=(int)k||c[id].colorIndex!=-1||c[id].localIndex!=q)return false;}
  for(int q=0;q<s->jointSims.count;++q){int id=s->jointSims.data[q].jointId;if(!index_ok(id,v->joint_count)||j[id].jointId!=id||j[id].setIndex!=(int)k||j[id].colorIndex!=-1||j[id].localIndex!=q)return false;}
  for(int q=0;q<s->islandSims.count;++q){int id=s->islandSims.data[q].islandId;if(!index_ok(id,v->island_count)||is[id].islandId!=id||is[id].setIndex!=(int)k||is[id].localIndex!=q)return false;}
 }
 for(int k=0;k<B2_GRAPH_COLOR_COUNT;++k){const b2GraphColor*color=g->colors+k;
  for(int q=0;q<color->contactSims.count;++q){int id=color->contactSims.data[q].contactId;if(!index_ok(id,v->contact_count)||c[id].contactId!=id||c[id].setIndex!=b2_awakeSet||c[id].colorIndex!=k||c[id].localIndex!=q)return false;}
  for(int q=0;q<color->jointSims.count;++q){int id=color->jointSims.data[q].jointId;if(!index_ok(id,v->joint_count)||j[id].jointId!=id||j[id].setIndex!=b2_awakeSet||j[id].colorIndex!=k||j[id].localIndex!=q)return false;}
 }
 for(uint32_t k=0;k<v->body_count;++k){const b2Body*x=b+k;if(x->id==-1)continue;if(x->id!=(int)k||!x->generation||!index_ok(x->setIndex,v->set_count)||sets[x->setIndex].setIndex!=x->setIndex||!index_ok(x->localIndex,(uint32_t)sets[x->setIndex].bodySims.count))return false;
  const b2SolverSet*s=sets+x->setIndex;const b2BodySim*sim=s->bodySims.data+x->localIndex;if(sim->bodyId!=(int)k||x->type<b2_staticBody||x->type>b2_dynamicBody)return false;
  uint32_t flags=x->flags&~b2_bodyTransientFlags;if((sim->flags&flags)!=flags||(x->setIndex==b2_awakeSet&&(s->bodyStates.data[x->localIndex].flags&flags)!=flags)||(x->type==b2_dynamicBody&&!(x->flags&b2_dynamicFlag)))return false;
  if(x->setIndex==b2_staticSet&&x->type!=b2_staticBody)return false;if(x->setIndex>=b2_awakeSet&&x->type==b2_staticBody)return false;
  if(x->setIndex<=b2_disabledSet){if(x->islandId!=-1||x->islandIndex!=-1)return false;}else{if(!index_ok(x->islandId,v->island_count))return false;const b2Island*a=is+x->islandId;if(a->islandId!=x->islandId||a->setIndex!=x->setIndex||!index_ok(x->islandIndex,(uint32_t)a->bodies.count)||a->bodies.data[x->islandIndex]!=(int)k)return false;}
  // Verify the complete endpoint lists, not just an arbitrary endpoint match.
  int key=x->headContactKey,prev=-1,count=0;while(key!=-1){if(key<0||count>=x->contactCount||count>=(int)v->contact_count)return false;int id=key>>1,edge=key&1;if(!index_ok(id,v->contact_count)||c[id].contactId!=id||c[id].edges[edge].bodyId!=(int)k||c[id].edges[edge].prevKey!=prev)return false;prev=key;key=c[id].edges[edge].nextKey;++count;}if(count!=x->contactCount||(x->setIndex==b2_disabledSet&&count))return false;contact_edges_seen+=(uint32_t)count;
  key=x->headJointKey;prev=-1;count=0;while(key!=-1){if(key<0||count>=x->jointCount||count>=(int)v->joint_count)return false;int id=key>>1,edge=key&1;if(!index_ok(id,v->joint_count)||j[id].jointId!=id||j[id].edges[edge].bodyId!=(int)k||j[id].edges[edge].prevKey!=prev)return false;prev=key;key=j[id].edges[edge].nextKey;++count;}if(count!=x->jointCount)return false;joint_edges_seen+=(uint32_t)count;
 }
 for(uint32_t k=0;k<v->contact_count;++k){const b2Contact*x=c+k;if(x->contactId==-1)continue;++live_contacts;if(x->contactId!=(int)k||!x->generation||!index_ok(x->edges[0].bodyId,v->body_count)||!index_ok(x->edges[1].bodyId,v->body_count)||x->edges[0].bodyId==x->edges[1].bodyId)return false;const b2Body*a=b+x->edges[0].bodyId,*z=b+x->edges[1].bodyId;if(a->id!=x->edges[0].bodyId||z->id!=x->edges[1].bodyId||a->setIndex==b2_disabledSet||z->setIndex==b2_disabledSet)return false;if(!contact_edges(v,x))return false;const b2ContactSim*sim=contact_sim(v,x);if(!sim||sim->contactId!=(int)k||sim->shapeIdA!=x->shapeIdA||sim->shapeIdB!=x->shapeIdB)return false;
  bool touching=(x->flags&b2_contactTouchingFlag)!=0;if(touching!=((sim->simFlags&b2_simTouchingFlag)!=0))return false;if(x->setIndex==b2_awakeSet){if(touching!=(x->colorIndex>=0))return false;}else if(x->setIndex>=b2_firstSleepingSet){if(!touching||x->colorIndex!=-1)return false;}else if(x->setIndex!=b2_disabledSet||touching||x->colorIndex!=-1)return false;
  if(!touching){if(x->islandId!=-1||x->islandIndex!=-1)return false;}else{if(!index_ok(x->islandId,v->island_count))return false;const b2Island*a2=is+x->islandId;if(a2->islandId!=x->islandId||a2->setIndex!=x->setIndex||!index_ok(x->islandIndex,(uint32_t)a2->contacts.count)||a2->contacts.data[x->islandIndex].contactId!=(int)k)return false;}
 }
 for(uint32_t k=0;k<v->joint_count;++k){const b2Joint*x=j+k;if(x->jointId==-1)continue;++live_joints;if(x->jointId!=(int)k||!x->generation||!index_ok(x->edges[0].bodyId,v->body_count)||!index_ok(x->edges[1].bodyId,v->body_count)||x->edges[0].bodyId==x->edges[1].bodyId)return false;const b2Body*a=b+x->edges[0].bodyId,*z=b+x->edges[1].bodyId;if(a->id!=x->edges[0].bodyId||z->id!=x->edges[1].bodyId)return false;if(!joint_edges(v,x))return false;const b2JointSim*sim=joint_sim(v,x);if(!sim||sim->jointId!=(int)k||sim->bodyIdA!=x->edges[0].bodyId||sim->bodyIdB!=x->edges[1].bodyId)return false;
  int expected;if(a->setIndex==b2_disabledSet||z->setIndex==b2_disabledSet)expected=b2_disabledSet;else if(a->type!=b2_dynamicBody&&z->type!=b2_dynamicBody)expected=b2_staticSet;else if(a->setIndex==b2_awakeSet||z->setIndex==b2_awakeSet)expected=b2_awakeSet;else expected=a->setIndex>=b2_firstSleepingSet?a->setIndex:z->setIndex;if(x->setIndex!=expected||(expected==b2_awakeSet)!=(x->colorIndex>=0))return false;
  if(expected<2){if(x->islandId!=-1||x->islandIndex!=-1)return false;}else{if(!index_ok(x->islandId,v->island_count))return false;const b2Island*a2=is+x->islandId;if(a2->islandId!=x->islandId||a2->setIndex!=x->setIndex||!index_ok(x->islandIndex,(uint32_t)a2->joints.count)||a2->joints.data[x->islandIndex].jointId!=(int)k)return false;}
 }
 for(uint32_t k=0;k<v->island_count;++k){const b2Island*a=is+k;if(a->islandId==-1)continue;if(a->islandId!=(int)k||a->setIndex<b2_awakeSet||!index_ok(a->setIndex,v->set_count)||sets[a->setIndex].setIndex!=a->setIndex||!index_ok(a->localIndex,(uint32_t)sets[a->setIndex].islandSims.count)||sets[a->setIndex].islandSims.data[a->localIndex].islandId!=(int)k||a->constraintRemoveCount<0||!a->bodies.count)return false;
  for(int q=0;q<a->bodies.count;++q){int id=a->bodies.data[q];if(!index_ok(id,v->body_count)||b[id].id!=id||b[id].islandId!=(int)k||b[id].islandIndex!=q||b[id].setIndex!=a->setIndex)return false;}
  for(int q=0;q<a->contacts.count;++q){b2ContactLink link=a->contacts.data[q];if(!index_ok(link.contactId,v->contact_count))return false;const b2Contact*x=c+link.contactId;if(x->contactId!=link.contactId||x->islandId!=(int)k||x->islandIndex!=q||x->setIndex!=a->setIndex||link.bodyIdA!=x->edges[0].bodyId||link.bodyIdB!=x->edges[1].bodyId)return false;for(int e=0;e<2;++e){const b2Body*z=b+x->edges[e].bodyId;if(z->type!=b2_staticBody&&z->islandId!=(int)k)return false;}}
  for(int q=0;q<a->joints.count;++q){b2JointLink link=a->joints.data[q];if(!index_ok(link.jointId,v->joint_count))return false;const b2Joint*x=j+link.jointId;if(x->jointId!=link.jointId||x->islandId!=(int)k||x->islandIndex!=q||x->setIndex!=a->setIndex||link.bodyIdA!=x->edges[0].bodyId||link.bodyIdB!=x->edges[1].bodyId)return false;for(int e=0;e<2;++e){const b2Body*z=b+x->edges[e].bodyId;if(z->type!=b2_staticBody&&z->islandId!=(int)k)return false;}}
 }
 return contact_edges_seen==2*live_contacts&&joint_edges_seen==2*live_joints;
}
bool spObserveWorldOwner(const SpWorldOwnershipView*v,enum SpOwnerKind kind,uint32_t slot,SpNativeLifetime*out){if(!out||!view_ok(v))return false;switch(kind){case SP_OWNER_BODY:if(slot>=v->body_count)return false;out->native_id=((const b2Body*)v->bodies)[slot].id;out->generation=((const b2Body*)v->bodies)[slot].generation;return true;case SP_OWNER_CONTACT:if(slot>=v->contact_count)return false;out->native_id=((const b2Contact*)v->contacts)[slot].contactId;out->generation=((const b2Contact*)v->contacts)[slot].generation;return true;case SP_OWNER_JOINT:if(slot>=v->joint_count)return false;out->native_id=((const b2Joint*)v->joints)[slot].jointId;out->generation=((const b2Joint*)v->joints)[slot].generation;return true;case SP_OWNER_ISLAND:if(slot>=v->island_count)return false;out->native_id=((const b2Island*)v->islands)[slot].islandId;out->generation=0;return true;case SP_OWNER_SET:if(slot>=v->set_count)return false;out->native_id=((const b2SolverSet*)v->sets)[slot].setIndex;out->generation=0;return true;default:return false;}}
bool spCopyWorldSolverBodies(const SpWorldOwnershipView*v,uint32_t set,int*out,uint32_t capacity,uint32_t*count){if(!view_ok(v)||set>=v->set_count||!count)return false;const b2SolverSet*s=(const b2SolverSet*)v->sets+set;if(s->setIndex!=(int)set||!ARRAY_OK(s->bodySims)||(uint32_t)s->bodySims.count>capacity||(s->bodySims.count&&!out))return false;for(int q=0;q<s->bodySims.count;++q)out[q]=s->bodySims.data[q].bodyId;*count=(uint32_t)s->bodySims.count;return true;}
static bool overlap(const void*p,uint64_t n,const void*q,uint64_t m){uintptr_t a=(uintptr_t)p,b=(uintptr_t)q;return n&&m&&(a<=b?b-a<n:a-b<m);}
bool spWorldOwnershipOverlaps(const SpWorldOwnershipView*v,const void*p,uint64_t n){
 if(!view_ok(v))return true;
 if(overlap(p,n,v,sizeof(*v))||overlap(p,n,v->bodies,(uint64_t)v->body_count*sizeof(b2Body))||overlap(p,n,v->contacts,(uint64_t)v->contact_count*sizeof(b2Contact))||overlap(p,n,v->joints,(uint64_t)v->joint_count*sizeof(b2Joint))||overlap(p,n,v->islands,(uint64_t)v->island_count*sizeof(b2Island))||overlap(p,n,v->sets,(uint64_t)v->set_count*sizeof(b2SolverSet))||overlap(p,n,v->graph,sizeof(b2ConstraintGraph)))return true;
 const b2SolverSet*s=v->sets;const b2Island*i=v->islands;const b2ConstraintGraph*g=v->graph;
#define CHECK_ARRAY(a) do{if(!ARRAY_OK(a)||overlap(p,n,(a).data,(uint64_t)(a).capacity*sizeof(*(a).data)))return true;}while(0)
 for(uint32_t k=0;k<v->set_count;++k){CHECK_ARRAY(s[k].bodySims);CHECK_ARRAY(s[k].bodyStates);CHECK_ARRAY(s[k].contactSims);CHECK_ARRAY(s[k].jointSims);CHECK_ARRAY(s[k].islandSims);}
 for(uint32_t k=0;k<v->island_count;++k)if(i[k].islandId!=-1){CHECK_ARRAY(i[k].bodies);CHECK_ARRAY(i[k].contacts);CHECK_ARRAY(i[k].joints);}
 for(int k=0;k<B2_GRAPH_COLOR_COUNT;++k){CHECK_ARRAY(g->colors[k].contactSims);CHECK_ARRAY(g->colors[k].jointSims);const b2BitSet*b=&g->colors[k].bodySet;if(b->blockCapacity<b->blockCount||b->blockCapacity>100000||(!b->bits&&b->blockCapacity)||overlap(p,n,b->bits,(uint64_t)b->blockCapacity*sizeof(uint64_t)))return true;}
#undef CHECK_ARRAY
 return false;
}
