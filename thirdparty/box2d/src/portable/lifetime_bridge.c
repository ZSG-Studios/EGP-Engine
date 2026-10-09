// SPDX-License-Identifier: MIT
#include "lifetime_bridge.h"
#include "id_pool.h"
#include "body.h"
#include <limits.h>
static bool load(SpPoolView*v,b2IdPool*p){if(!v||v->free_count>v->free_capacity||v->allocated_count>INT_MAX||v->free_capacity>INT_MAX||(!v->free_entries&&v->free_capacity))return false;p->freeArray.data=(int*)v->free_entries;p->freeArray.count=(int)v->free_count;p->freeArray.capacity=(int)v->free_capacity;p->nextIndex=(int)v->allocated_count;return true;}
static void save(SpPoolView*v,const b2IdPool*p){v->free_entries=p->freeArray.data;v->free_count=(uint32_t)p->freeArray.count;v->free_capacity=(uint32_t)p->freeArray.capacity;v->allocated_count=(uint32_t)p->nextIndex;}
bool spLifecyclePoolAllocate(SpPoolView*v,uint32_t maximum,int*out){b2IdPool p;if(!out||!load(v,&p)||maximum>INT_MAX||v->allocated_count>maximum||(!v->free_count&&v->allocated_count==maximum))return false;if(v->free_count&&(v->free_entries[v->free_count-1]<0||(uint32_t)v->free_entries[v->free_count-1]>=v->allocated_count))return false;*out=b2AllocId(&p);save(v,&p);return true;}
bool spLifecyclePoolFree(SpPoolView*v,int id){b2IdPool p;if(!load(v,&p)||id<0||(uint32_t)id>=v->allocated_count||v->free_count==v->free_capacity)return false;for(uint32_t i=0;i<v->free_count;++i)if(v->free_entries[i]==id)return false;b2FreeId(&p,id);save(v,&p);return true;}
bool spObserveBodyLifetimes(const void*p,uint32_t count,SpNativeLifetime*out){if(count&&(!p||!out))return false;const b2Body*b=p;for(uint32_t i=0;i<count;++i){out[i].native_id=b[i].id;out[i].generation=b[i].generation;}return true;}
