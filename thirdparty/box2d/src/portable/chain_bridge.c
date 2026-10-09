// SPDX-License-Identifier: MIT
#include "chain_bridge.h"
#include "shape.h"
static bool valid(const SpChainView *s){return s&&s->count<=s->shape_capacity&&s->material_count<=s->material_capacity&&(!s->count||s->shapes)&&(!s->material_count||s->materials);}
bool spExportChain(const void *p,SpChainView *out){if(!p||!valid(out))return false;const b2ChainShape *n=p;if(n->count<0||n->materialCount<0||(uint32_t)n->count>out->shape_capacity||(uint32_t)n->materialCount>out->material_capacity||(!n->shapeIndices&&n->count)||(!n->materials&&n->materialCount))return false;
 for(int i=0;i<n->count;++i)out->shapes[i]=n->shapeIndices[i];for(int i=0;i<n->materialCount;++i)out->materials[i]=n->materials[i];out->id=n->id;out->body=n->bodyId;out->next=n->nextChainId;out->generation=n->generation;out->count=n->count;out->material_count=n->materialCount;return true;}
bool spImportChainCandidate(const SpChainView *s,void *p){if(!p||!valid(s))return false;b2ChainShape *n=p;
 // Native allocation sizes are exactly the count fields. The caller owns both
 // allocations and must stage a complete private candidate before attaching it.
 if(n->count<0||n->materialCount<0||(uint32_t)n->count!=s->count||(uint32_t)n->materialCount!=s->material_count||(!n->shapeIndices&&s->count)||(!n->materials&&s->material_count))return false;
 for(uint32_t i=0;i<s->count;++i)n->shapeIndices[i]=s->shapes[i];for(uint32_t i=0;i<s->material_count;++i)n->materials[i]=s->materials[i];n->id=s->id;n->bodyId=s->body;n->nextChainId=s->next;n->generation=s->generation;return true;}
bool spChainFixtureRoundTrip(const SpChainView *s,SpChainView *out){if(!valid(s)||s->count>8||s->material_count>8)return false;int shapes[8];b2SurfaceMaterial materials[8];b2ChainShape n={0};n.count=s->count;n.materialCount=s->material_count;n.shapeIndices=shapes;n.materials=materials;return spImportChainCandidate(s,&n)&&spExportChain(&n,out);}
