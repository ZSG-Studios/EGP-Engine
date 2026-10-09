// SPDX-License-Identifier: MIT
#include "world_adoption.h"
#include "world_root_bridge.h"
#include "runtime_scratch_bridge.h"
#include "physics_world.h"
#include "body.h"
#include "contact.h"
#include "joint.h"
#include "island.h"
#include "solver_set.h"
#include "constraint_graph.h"
#include "shape.h"
#include "sensor.h"
#include "broad_phase.h"
#include "bitset.h"
#include "id_pool.h"
#include "core.h"
#include "atomic.h"
#include <string.h>
// Native copy of one b2Array with the candidate capacity plus `slack` (a local
// of the enclosing function) when that capacity is non-zero; capacity 0 stays
// empty. Slack 0 is an exact-capacity copy.
#define DUP(dst,src) do{(dst).data=NULL;(dst).count=(src).count;(dst).capacity=(src).capacity>0?(src).capacity+slack:0;if((src).count<0||(src).capacity<(src).count)return false;\
 if((dst).capacity>0){size_t bytes=(size_t)(dst).capacity*sizeof(*(src).data);(dst).data=b2Alloc(bytes);if(!(dst).data)return false;memset((dst).data,0,bytes);if((src).count)memcpy((dst).data,(src).data,(size_t)(src).count*sizeof(*(src).data));}}while(0)
static bool dup_bits(b2BitSet*d,const b2BitSet*s){*d=(b2BitSet){0};if(s->blockCount>s->blockCapacity)return false;if(!s->blockCapacity)return true;size_t bytes=(size_t)s->blockCapacity*sizeof(uint64_t);d->bits=b2Alloc(bytes);if(!d->bits)return false;memset(d->bits,0,bytes);if(s->blockCount)memcpy(d->bits,s->bits,(size_t)s->blockCount*sizeof(uint64_t));d->blockCapacity=s->blockCapacity;d->blockCount=s->blockCount;return true;}
static bool dup_raw(void**d,const void*s,size_t bytes){*d=NULL;if(!bytes)return true;*d=b2Alloc(bytes);if(!*d)return false;if(s)memcpy(*d,s,bytes);else memset(*d,0,bytes);return true;}
static bool dup_tree(b2DynamicTree*d,const b2DynamicTree*s){
 *d=*s;d->nodes=NULL;d->leafIndices=NULL;d->leafBoxes=NULL;d->leafCenters=NULL;d->binIndices=NULL;
 if(s->nodeCapacity<0||s->rebuildCapacity<0)return false;
 if(!dup_raw((void**)&d->nodes,s->nodes,(size_t)s->nodeCapacity*sizeof(b2TreeNode)))return false;
 const size_t r=(size_t)s->rebuildCapacity;
 // Rebuild arrays absent in the candidate stay absent (the heuristic-0 profile
 // never allocates box/bin arrays); present arrays keep their exact capacity.
 if((s->leafIndices&&!dup_raw((void**)&d->leafIndices,s->leafIndices,r*sizeof(int32_t)))||(s->leafBoxes&&!dup_raw((void**)&d->leafBoxes,s->leafBoxes,r*sizeof(b2AABB)))||(s->leafCenters&&!dup_raw((void**)&d->leafCenters,s->leafCenters,r*sizeof(b2Vec2)))||(s->binIndices&&!dup_raw((void**)&d->binIndices,s->binIndices,r*sizeof(int32_t))))return false;
 return true;
}
static void free_placeholders(b2World*w){
 b2Array_Destroy(w->bodyMoveEvents);b2Array_Destroy(w->sensorBeginEvents);b2Array_Destroy(w->sensorEndEvents[0]);b2Array_Destroy(w->sensorEndEvents[1]);b2Array_Destroy(w->contactBeginEvents);b2Array_Destroy(w->contactEndEvents[0]);b2Array_Destroy(w->contactEndEvents[1]);b2Array_Destroy(w->contactHitEvents);b2Array_Destroy(w->jointEvents);
 for(int i=0;i<w->chainShapes.count;++i)if(w->chainShapes.data[i].id!=B2_NULL_INDEX)b2FreeChainData(w->chainShapes.data+i);
 for(int i=0;i<w->sensors.count;++i){b2Array_Destroy(w->sensors.data[i].hits);b2Array_Destroy(w->sensors.data[i].overlaps1);b2Array_Destroy(w->sensors.data[i].overlaps2);}
 b2Array_Destroy(w->sensors);b2Array_Destroy(w->bodies);b2Array_Destroy(w->shapes);b2Array_Destroy(w->chainShapes);b2Array_Destroy(w->contacts);b2Array_Destroy(w->joints);
 for(int i=0;i<w->islands.count;++i){b2Array_Destroy(w->islands.data[i].bodies);b2Array_Destroy(w->islands.data[i].contacts);b2Array_Destroy(w->islands.data[i].joints);}
 b2Array_Destroy(w->islands);
 for(int i=0;i<w->solverSets.count;++i){b2SolverSet*s=w->solverSets.data+i;b2Array_Destroy(s->bodySims);b2Array_Destroy(s->bodyStates);b2Array_Destroy(s->contactSims);b2Array_Destroy(s->jointSims);b2Array_Destroy(s->islandSims);}
 b2Array_Destroy(w->solverSets);b2DestroyGraph(&w->constraintGraph);b2DestroyBroadPhase(&w->broadPhase);
 b2DestroyIdPool(&w->bodyIdPool);b2DestroyIdPool(&w->shapeIdPool);b2DestroyIdPool(&w->chainIdPool);b2DestroyIdPool(&w->contactIdPool);b2DestroyIdPool(&w->jointIdPool);b2DestroyIdPool(&w->islandIdPool);b2DestroyIdPool(&w->solverSetIdPool);
}
static bool dup_pool(b2IdPool*d,const b2IdPool*s,int slack){*d=(b2IdPool){0};DUP(d->freeArray,s->freeArray);d->nextIndex=s->nextIndex;return true;}
static bool copy_content(b2World*w,const b2World*c,int slack){
 DUP(w->bodies,c->bodies);DUP(w->contacts,c->contacts);DUP(w->joints,c->joints);DUP(w->shapes,c->shapes);
 DUP(w->chainShapes,c->chainShapes);
 for(int i=0;i<c->chainShapes.count;++i){const b2ChainShape*s=c->chainShapes.data+i;b2ChainShape*d=w->chainShapes.data+i;d->shapeIndices=NULL;d->materials=NULL;if(s->id==B2_NULL_INDEX)continue;
  if(s->count<0||s->materialCount<0||!dup_raw((void**)&d->shapeIndices,s->shapeIndices,(size_t)s->count*sizeof(int))||!dup_raw((void**)&d->materials,s->materials,(size_t)s->materialCount*sizeof(b2SurfaceMaterial)))return false;}
 DUP(w->sensors,c->sensors);
 for(int i=0;i<c->sensors.count;++i){const b2Sensor*s=c->sensors.data+i;b2Sensor*d=w->sensors.data+i;DUP(d->hits,s->hits);DUP(d->overlaps1,s->overlaps1);DUP(d->overlaps2,s->overlaps2);}
 DUP(w->islands,c->islands);
 for(int i=0;i<c->islands.count;++i){const b2Island*s=c->islands.data+i;b2Island*d=w->islands.data+i;DUP(d->bodies,s->bodies);DUP(d->contacts,s->contacts);DUP(d->joints,s->joints);}
 DUP(w->solverSets,c->solverSets);
 for(int i=0;i<c->solverSets.count;++i){const b2SolverSet*s=c->solverSets.data+i;b2SolverSet*d=w->solverSets.data+i;DUP(d->bodySims,s->bodySims);DUP(d->bodyStates,s->bodyStates);DUP(d->contactSims,s->contactSims);DUP(d->jointSims,s->jointSims);DUP(d->islandSims,s->islandSims);}
 w->constraintGraph=(b2ConstraintGraph){0};
 for(int i=0;i<B2_GRAPH_COLOR_COUNT;++i){const b2GraphColor*s=c->constraintGraph.colors+i;b2GraphColor*d=w->constraintGraph.colors+i;if(!dup_bits(&d->bodySet,&s->bodySet))return false;DUP(d->contactSims,s->contactSims);DUP(d->jointSims,s->jointSims);}
 const b2BroadPhase*sb=&c->broadPhase;b2BroadPhase*db=&w->broadPhase;*db=(b2BroadPhase){0};
 for(int t=0;t<b2_bodyTypeCount;++t){if(!dup_tree(db->trees+t,sb->trees+t)||!dup_bits(db->movedProxies+t,sb->movedProxies+t))return false;}
 DUP(db->moveArray,sb->moveArray);
 db->pairSet=sb->pairSet;db->pairSet.items=NULL;if(!dup_raw((void**)&db->pairSet.items,sb->pairSet.items,(size_t)sb->pairSet.capacity*sizeof(b2SetItem)))return false;
 db->moveResults=NULL;db->movePairs=NULL;db->movePairCapacity=0;b2AtomicStoreInt(&db->movePairIndex,0);
 if(!dup_pool(&w->bodyIdPool,&c->bodyIdPool,slack)||!dup_pool(&w->contactIdPool,&c->contactIdPool,slack)||!dup_pool(&w->jointIdPool,&c->jointIdPool,slack)||!dup_pool(&w->islandIdPool,&c->islandIdPool,slack)||!dup_pool(&w->solverSetIdPool,&c->solverSetIdPool,slack)||!dup_pool(&w->shapeIdPool,&c->shapeIdPool,slack)||!dup_pool(&w->chainIdPool,&c->chainIdPool,slack))return false;
 DUP(w->bodyMoveEvents,c->bodyMoveEvents);DUP(w->sensorBeginEvents,c->sensorBeginEvents);DUP(w->contactBeginEvents,c->contactBeginEvents);DUP(w->contactHitEvents,c->contactHitEvents);DUP(w->jointEvents,c->jointEvents);
 for(int i=0;i<2;++i){DUP(w->sensorEndEvents[i],c->sensorEndEvents[i]);DUP(w->contactEndEvents[i],c->contactEndEvents[i]);}
 return true;
}
bool spAdoptCandidateWorld(void*p,const void*candidate){return spAdoptCandidateWorldWithSlack(p,candidate,0);}
bool spAdoptCandidateWorldWithSlack(void*p,const void*candidate,int slack){
 if(!p||!candidate||slack<0||slack>4096)return false;b2World*w=p;const b2World*c=candidate;
 if(!w->inUse||w->locked||!spValidateNormalizedRuntimeScratch(candidate))return false;
 // Only an empty, freshly created world is a valid target: adoption frees its
 // placeholder containers, which would leak or orphan any populated content.
 if(w->bodies.count||w->shapes.count||w->chainShapes.count||w->contacts.count||w->joints.count||w->islands.count||w->sensors.count)return false;
 SpWorldRoot root;if(!spExportWorldRoot(candidate,&root))return false;
 free_placeholders(w);
 // A partially copied world cannot be returned to b2CreateWorld's state; the
 // caller must treat failure as fatal for this live world.
 if(!copy_content(w,c,slack))return false;
 return spImportWorldRoot(&root,w);
}
