// SPDX-License-Identifier: MIT
#include "pool_bridge.h"
#include "id_pool.h"
#include <limits.h>
bool spExportPool(const void *pointer,SpPoolView *out){
 if(!pointer||!out)return false;const b2IdPool *pool=pointer;
 if(pool->nextIndex<0||pool->freeArray.count<0||pool->freeArray.capacity<pool->freeArray.count||(!pool->freeArray.data&&pool->freeArray.capacity))return false;
 *out=(SpPoolView){pool->freeArray.data,(uint32_t)pool->freeArray.count,(uint32_t)pool->freeArray.capacity,(uint32_t)pool->nextIndex};return true;
}
// Only isolated, already-charged candidate storage is accepted by the C++ owner.
// Semantic/reference/duplicate checks run in C++ before this scalar publication.
bool spRestorePoolCandidate(const SpPoolView *in,void *pointer){
 if(!in||!pointer||in->free_count>in->free_capacity||in->allocated_count>INT_MAX||in->free_capacity>INT_MAX)return false;
 b2IdPool *pool=pointer;if(pool->freeArray.capacity!=(int)in->free_capacity||(!pool->freeArray.data&&in->free_capacity)||(!in->free_entries&&in->free_count))return false;
 for(uint32_t i=0;i<in->free_count;++i)pool->freeArray.data[i]=in->free_entries[i];
 pool->freeArray.count=(int)in->free_count;pool->freeArray.capacity=(int)in->free_capacity;pool->nextIndex=(int)in->allocated_count;return true;
}
bool spPoolFixtureRoundTrip(const SpPoolView *in,int *storage,uint32_t capacity,SpPoolView *out){
 if(capacity>INT_MAX||(!storage&&capacity))return false;b2IdPool pool={0};pool.freeArray.data=storage;pool.freeArray.capacity=(int)capacity;
 return spRestorePoolCandidate(in,&pool)&&spExportPool(&pool,out);
}
