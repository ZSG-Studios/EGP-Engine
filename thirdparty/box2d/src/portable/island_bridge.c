// SPDX-License-Identifier: MIT
#include "island_bridge.h"
#include "island.h"
static bool shape(const SpIslandView *s){return s&&s->body_count<=s->body_capacity&&s->contact_count<=s->contact_capacity&&s->joint_count<=s->joint_capacity&&(!s->body_count||s->bodies)&&(!s->contact_count||s->contacts)&&(!s->joint_count||s->joints);}
bool spExportIsland(const void *p,SpIslandView *s){
 if(!p||!shape(s))return false;const b2Island *n=p;
 if(n->bodies.count<0||n->bodies.capacity<0||n->contacts.count<0||n->contacts.capacity<0||n->joints.count<0||n->joints.capacity<0||n->bodies.count>n->bodies.capacity||n->contacts.count>n->contacts.capacity||n->joints.count>n->joints.capacity)return false;
 if((uint32_t)n->bodies.count>s->body_capacity||(uint32_t)n->contacts.count>s->contact_capacity||(uint32_t)n->joints.count>s->joint_capacity||(!n->bodies.data&&n->bodies.count)||(!n->contacts.data&&n->contacts.count)||(!n->joints.data&&n->joints.count))return false;
 for(int i=0;i<n->bodies.count;++i)s->bodies[i]=n->bodies.data[i];
 for(int i=0;i<n->contacts.count;++i){b2ContactLink v=n->contacts.data[i];s->contacts[i]=(SpIslandLink){v.contactId,v.bodyIdA,v.bodyIdB};}
 for(int i=0;i<n->joints.count;++i){b2JointLink v=n->joints.data[i];s->joints[i]=(SpIslandLink){v.jointId,v.bodyIdA,v.bodyIdB};}
 s->set_index=n->setIndex;s->local_index=n->localIndex;s->island_id=n->islandId;s->removed_constraints=n->constraintRemoveCount;
 s->body_count=n->bodies.count;s->contact_count=n->contacts.count;s->joint_count=n->joints.count;
 s->body_capacity=n->bodies.capacity;s->contact_capacity=n->contacts.capacity;s->joint_capacity=n->joints.capacity;return true;
}
bool spImportIslandCandidate(const SpIslandView *s,void *p){
 if(!p||!shape(s))return false;b2Island *n=p;
 // The candidate has already reserved its exact allocations. This operation
 // neither allocates nor frees and is never used to publish a live world.
 if(n->bodies.capacity<0||n->contacts.capacity<0||n->joints.capacity<0||(uint32_t)n->bodies.capacity!=s->body_capacity||(uint32_t)n->contacts.capacity!=s->contact_capacity||(uint32_t)n->joints.capacity!=s->joint_capacity||(!n->bodies.data&&s->body_count)||(!n->contacts.data&&s->contact_count)||(!n->joints.data&&s->joint_count))return false;
 for(uint32_t i=0;i<s->body_count;++i)n->bodies.data[i]=s->bodies[i];
 for(uint32_t i=0;i<s->contact_count;++i){SpIslandLink v=s->contacts[i];n->contacts.data[i]=(b2ContactLink){v.object,v.body_a,v.body_b};}
 for(uint32_t i=0;i<s->joint_count;++i){SpIslandLink v=s->joints[i];n->joints.data[i]=(b2JointLink){v.object,v.body_a,v.body_b};}
 n->bodies.count=s->body_count;n->contacts.count=s->contact_count;n->joints.count=s->joint_count;
 n->setIndex=s->set_index;n->localIndex=s->local_index;n->islandId=s->island_id;n->constraintRemoveCount=s->removed_constraints;return true;
}
bool spIslandFixtureRoundTrip(const SpIslandView *in,SpIslandView *out){
 if(!shape(in)||in->body_capacity>8||in->contact_capacity>8||in->joint_capacity>8)return false;
 int bodies[8];b2ContactLink contacts[8];b2JointLink joints[8];b2Island n={0};
 n.bodies.data=bodies;n.bodies.capacity=in->body_capacity;n.contacts.data=contacts;n.contacts.capacity=in->contact_capacity;n.joints.data=joints;n.joints.capacity=in->joint_capacity;
 return spImportIslandCandidate(in,&n)&&spExportIsland(&n,out);
}
