// SPDX-License-Identifier: MIT
#include "constraint_graph_bridge.h"
#include "constraint_graph.h"
#include <stddef.h>
bool spConstraintGraphFixtureRoundTrip(const SpGraphColorView *in,SpGraphColorView *out,uint32_t colors){
 if(!in||!out||colors!=B2_GRAPH_COLOR_COUNT)return false;
 b2ConstraintGraph graph={0};b2ContactSim contacts[64];b2JointSim joints[64];uint32_t ci=0,ji=0;
 for(uint32_t i=0;i<colors;++i){if(in[i].contact_capacity>64-ci||in[i].joint_capacity>64-ji)return false;graph.colors[i].contactSims.data=in[i].contact_capacity?contacts+ci:NULL;graph.colors[i].contactSims.capacity=(int)in[i].contact_capacity;graph.colors[i].jointSims.data=in[i].joint_capacity?joints+ji:NULL;graph.colors[i].jointSims.capacity=(int)in[i].joint_capacity;ci+=in[i].contact_capacity;ji+=in[i].joint_capacity;if(!spImportGraphColorCandidate(in+i,graph.colors+i))return false;if(graph.colors[i].wideConstraints||graph.colors[i].wideConstraintCount)return false;}
 for(uint32_t i=0;i<colors;++i){graph.colors[i].wideConstraints=(struct b2ContactConstraintWide*)(uintptr_t)1;graph.colors[i].wideConstraintCount=2147483647;if(!spExportGraphColor(graph.colors+i,out+i,in[i].contact_count,in[i].joint_count))return false;}return true;
}
