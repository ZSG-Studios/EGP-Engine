// SPDX-License-Identifier: MIT
#include "solver_payload_bridge.h"
#include "solver_set.h"
#include <string.h>
static bool valid(const SpSolverPayload*s){if(!s||s->set_index<0)return false;const void*p[5]={s->body_sims,s->body_states,s->joints,s->contacts,s->islands};for(int i=0;i<5;++i)if(s->count[i]>s->capacity[i]||(!p[i]&&s->count[i]))return false;return true;}
bool spExportSolverPayload(const void*p,SpSolverPayload*out,const uint32_t storage[5]){
 if(!p||!out||!storage)return false;const b2SolverSet*n=p;
 const int count[5]={n->bodySims.count,n->bodyStates.count,n->jointSims.count,n->contactSims.count,n->islandSims.count};const int cap[5]={n->bodySims.capacity,n->bodyStates.capacity,n->jointSims.capacity,n->contactSims.capacity,n->islandSims.capacity};const void*src[5]={n->bodySims.data,n->bodyStates.data,n->jointSims.data,n->contactSims.data,n->islandSims.data};const void*dst[5]={out->body_sims,out->body_states,out->joints,out->contacts,out->islands};
 for(int i=0;i<5;++i)if(count[i]<0||cap[i]<count[i]||(uint32_t)count[i]>storage[i]||(!src[i]&&count[i])||(!dst[i]&&count[i]))return false;
 if(count[0])memcpy(out->body_sims,n->bodySims.data,(size_t)count[0]*sizeof(b2BodySim));if(count[1])memcpy(out->body_states,n->bodyStates.data,(size_t)count[1]*sizeof(b2BodyState));
 for(int i=0;i<count[2];++i)if(!spExportJointSim(n->jointSims.data+i,out->joints+i))return false;for(int i=0;i<count[3];++i)if(!spExportContactSim(n->contactSims.data+i,out->contacts+i))return false;for(int i=0;i<count[4];++i)out->islands[i]=n->islandSims.data[i].islandId;
 out->set_index=n->setIndex;for(int i=0;i<5;++i){out->count[i]=(uint32_t)count[i];out->capacity[i]=(uint32_t)cap[i];}return true;
}
bool spImportSolverPayloadCandidate(const SpSolverPayload*s,void*p){
 if(!valid(s)||!p)return false;b2SolverSet*n=p;const int cap[5]={n->bodySims.capacity,n->bodyStates.capacity,n->jointSims.capacity,n->contactSims.capacity,n->islandSims.capacity};const void*dst[5]={n->bodySims.data,n->bodyStates.data,n->jointSims.data,n->contactSims.data,n->islandSims.data};for(int i=0;i<5;++i)if(cap[i]<0||(uint32_t)cap[i]!=s->capacity[i]||(!dst[i]&&s->capacity[i]))return false;
 if(s->count[0])memcpy(n->bodySims.data,s->body_sims,(size_t)s->count[0]*sizeof(b2BodySim));if(s->count[1])memcpy(n->bodyStates.data,s->body_states,(size_t)s->count[1]*sizeof(b2BodyState));for(uint32_t i=0;i<s->count[2];++i)if(!spImportJointSim(s->joints+i,n->jointSims.data+i))return false;for(uint32_t i=0;i<s->count[3];++i)if(!spImportContactSim(s->contacts+i,n->contactSims.data+i))return false;for(uint32_t i=0;i<s->count[4];++i)n->islandSims.data[i].islandId=s->islands[i];
 n->setIndex=s->set_index;n->bodySims.count=(int)s->count[0];n->bodyStates.count=(int)s->count[1];n->jointSims.count=(int)s->count[2];n->contactSims.count=(int)s->count[3];n->islandSims.count=(int)s->count[4];return true;
}
bool spSolverPayloadFixtureRoundTrip(const SpSolverPayload*s,SpSolverPayload*out,const uint32_t storage[5]){
 if(!valid(s))return false;for(int i=0;i<5;++i)if(s->capacity[i]>4)return false;b2BodySim b[4];b2BodyState st[4];b2JointSim j[4];b2ContactSim c[4];b2IslandSim is[4];b2SolverSet n={0};
 n.bodySims.data=b;n.bodyStates.data=st;n.jointSims.data=j;n.contactSims.data=c;n.islandSims.data=is;n.bodySims.capacity=(int)s->capacity[0];n.bodyStates.capacity=(int)s->capacity[1];n.jointSims.capacity=(int)s->capacity[2];n.contactSims.capacity=(int)s->capacity[3];n.islandSims.capacity=(int)s->capacity[4];return spImportSolverPayloadCandidate(s,&n)&&spExportSolverPayload(&n,out,storage);
}
