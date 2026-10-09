// SPDX-License-Identifier: MIT
#include "geometry_root_bridge.h"
#include "physics_world.h"
#include "shape.h"
#include "sensor.h"
#include "broad_phase.h"
#include <stddef.h>
#include <string.h>
SpGeometryLayout spGeometryLayout(void){return (SpGeometryLayout){sizeof(b2ChainShape),_Alignof(b2ChainShape),sizeof(b2Sensor),_Alignof(b2Sensor),sizeof(b2Visitor),_Alignof(b2Visitor)};}
bool spCopyWorldRoot(void*d,const void*s){if(!d||!s||d==s)return false;memcpy(d,s,sizeof(b2World));return true;}
void*spWorldRootBroadPhase(void*w){return w?&((b2World*)w)->broadPhase:NULL;}
bool spPrepareChainCandidate(void*p,int*shapes,uint32_t count,b2SurfaceMaterial*materials,uint32_t material_count){if(!p||count>100000||material_count>100000||(!shapes&&count)||(!materials&&material_count))return false;b2ChainShape*c=p;*c=(b2ChainShape){0};c->shapeIndices=shapes;c->count=(int)count;c->materials=materials;c->materialCount=(int)material_count;c->id=c->bodyId=c->nextChainId=-1;return true;}
bool spPrepareFreeChain(void*p,uint32_t generation){if(!p||generation>UINT16_MAX)return false;b2ChainShape*c=p;*c=(b2ChainShape){0};c->id=c->bodyId=c->nextChainId=-1;c->generation=(uint16_t)generation;return true;}
bool spPrepareSensorCandidate(void*p,void*hits,uint32_t hc,void*first,uint32_t fc,void*second,uint32_t sc){if(!p||hc>100000||fc>100000||sc>100000||(!hits&&hc)||(!first&&fc)||(!second&&sc))return false;b2Sensor*s=p;*s=(b2Sensor){0};s->hits.data=hits;s->hits.capacity=(int)hc;s->overlaps1.data=first;s->overlaps1.capacity=(int)fc;s->overlaps2.data=second;s->overlaps2.capacity=(int)sc;s->shapeId=-1;return true;}
static bool pool_ok(const SpPoolView*v){return v&&v->free_count<=v->free_capacity&&v->free_capacity<=100000&&v->allocated_count<=100000&&(v->free_entries||!v->free_capacity);}
static void bind_pool(b2IdPool*pool,const SpPoolView*v){pool->freeArray.data=(int*)v->free_entries;pool->freeArray.count=(int)v->free_count;pool->freeArray.capacity=(int)v->free_capacity;pool->nextIndex=(int)v->allocated_count;}
bool spBindWorldRootGeometry(void*p,void*shapes,uint32_t sc,void*chains,uint32_t cc,void*sensors,uint32_t nc,const SpPoolView*sp,const SpPoolView*cp){
 if(!p||!pool_ok(sp)||!pool_ok(cp)||sc>100000||cc>100000||nc>100000||(!shapes&&sc)||(!chains&&cc)||(!sensors&&nc))return false;b2World*w=p;
 w->shapes.data=shapes;w->shapes.count=w->shapes.capacity=(int)sc;w->chainShapes.data=chains;w->chainShapes.count=w->chainShapes.capacity=(int)cc;w->sensors.data=sensors;w->sensors.count=w->sensors.capacity=(int)nc;
 bind_pool(&w->shapeIdPool,sp);bind_pool(&w->chainIdPool,cp);return true;
}
static bool pool_matches(const b2IdPool*pool,int count,const void*data,size_t stride,size_t id_offset){
 if(pool->nextIndex!=count||pool->freeArray.count<0||pool->freeArray.count>count)return false;int free=0;
 for(int i=0;i<count;++i){int id;memcpy(&id,(const char*)data+(size_t)i*stride+id_offset,sizeof(id));if(id==-1)++free;else if(id!=i)return false;}
 if(free!=pool->freeArray.count)return false;
 for(int k=0;k<pool->freeArray.count;++k){int s=pool->freeArray.data[k];if(s<0||s>=count)return false;int id;memcpy(&id,(const char*)data+(size_t)s*stride+id_offset,sizeof(id));if(id!=-1)return false;for(int q=0;q<k;++q)if(pool->freeArray.data[q]==s)return false;}
 return true;
}
bool spValidateWorldRootGeometryPools(const void*p){if(!p)return false;const b2World*w=p;return pool_matches(&w->shapeIdPool,w->shapes.count,w->shapes.data,sizeof(b2Shape),offsetof(b2Shape,id))&&pool_matches(&w->chainIdPool,w->chainShapes.count,w->chainShapes.data,sizeof(b2ChainShape),offsetof(b2ChainShape,id));}
bool spWorldRootGeometryView(const void*p,const SpWorldOwnershipView*o,SpGeometryOwnershipView*out){if(!p||!o||!out)return false;const b2World*w=p;if(w->shapes.count<0||w->chainShapes.count<0||w->sensors.count<0)return false;*out=(SpGeometryOwnershipView){o,w->shapes.data,w->chainShapes.data,w->sensors.data,&w->broadPhase,(uint32_t)w->shapes.count,(uint32_t)w->chainShapes.count,(uint32_t)w->sensors.count};return true;}
