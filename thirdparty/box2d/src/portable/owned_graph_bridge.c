// SPDX-License-Identifier: MIT
#include "owned_graph_bridge.h"
#include "constraint_graph.h"
SpOwnedGraphLayout spOwnedGraphLayout(void){return (SpOwnedGraphLayout){sizeof(b2ConstraintGraph),_Alignof(b2ConstraintGraph),sizeof(b2ContactSim),_Alignof(b2ContactSim),sizeof(b2JointSim),_Alignof(b2JointSim)};}
bool spPrepareOwnedGraph(void*p){if(!p)return false;*(b2ConstraintGraph*)p=(b2ConstraintGraph){0};return true;}
bool spPrepareOwnedGraphColor(void*p,uint32_t color,void*contacts,uint32_t cc,void*joints,uint32_t jc){if(!p||color>=B2_GRAPH_COLOR_COUNT||cc>100000||jc>100000||(!contacts&&cc)||(!joints&&jc))return false;b2GraphColor*c=((b2ConstraintGraph*)p)->colors+color;c->contactSims.data=contacts;c->contactSims.capacity=(int)cc;c->jointSims.data=joints;c->jointSims.capacity=(int)jc;return true;}
const void*spOwnedGraphColor(const void*p,uint32_t color){return p&&color<B2_GRAPH_COLOR_COUNT?((const b2ConstraintGraph*)p)->colors+color:NULL;}
uint32_t spOwnedGraphColorCount(const void*p,uint32_t color,bool joints){if(!p||color>=B2_GRAPH_COLOR_COUNT)return 0;const b2GraphColor*c=((const b2ConstraintGraph*)p)->colors+color;int n=joints?c->jointSims.count:c->contactSims.count;return n>0?(uint32_t)n:0;}
bool spOwnedGraphContactAt(const void*p,uint32_t color,uint32_t local,int*id){if(!p||!id||local>=spOwnedGraphColorCount(p,color,false))return false;*id=((const b2ConstraintGraph*)p)->colors[color].contactSims.data[local].contactId;return true;}
bool spOwnedGraphJointAt(const void*p,uint32_t color,uint32_t local,int*id,int*type,int*a,int*b){if(!p||!id||!type||!a||!b||local>=spOwnedGraphColorCount(p,color,true))return false;const b2JointSim*j=((const b2ConstraintGraph*)p)->colors[color].jointSims.data+local;*id=j->jointId;*type=(int)j->type;*a=j->bodyIdA;*b=j->bodyIdB;return true;}
