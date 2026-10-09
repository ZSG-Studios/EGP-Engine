// SPDX-License-Identifier: MIT
#include "runtime_scratch_bridge.h"
#include "physics_world.h"
#include "sensor.h"
#include <string.h>
bool spValidateRuntimeScratchBarrier(const void*p){
 if(!p)return false;const b2World*w=p;
 if(w->locked||w->activeTaskCount!=0||w->userTreeTask!=NULL)return false;
 if(w->stack.index!=0||w->stack.allocation!=0||w->stack.entries.count!=0||w->stack.capacity<0)return false;
 if(w->workerCount<0||w->taskContexts.count!=w->workerCount||w->sensorTaskContexts.count!=w->workerCount)return false;
 return true;
}
static bool empty_bits(const b2BitSet*b){return !b->bits&&!b->blockCapacity&&!b->blockCount;}
bool spValidateNormalizedRuntimeScratch(const void*p){
 if(!p)return false;const b2World*w=p;const b2Profile zero={0};
 if(w->taskContexts.data||w->taskContexts.count||w->taskContexts.capacity||w->sensorTaskContexts.data||w->sensorTaskContexts.count||w->sensorTaskContexts.capacity)return false;
 if(w->stack.data||w->stack.capacity||w->stack.index||w->stack.allocation||w->stack.maxAllocation||w->stack.entries.data||w->stack.entries.count||w->stack.entries.capacity)return false;
 if(!empty_bits(&w->debugBodySet)||!empty_bits(&w->debugJointSet)||!empty_bits(&w->debugContactSet)||!empty_bits(&w->debugIslandSet))return false;
 if(memcmp(&w->profile,&zero,sizeof(zero))!=0||w->activeTaskCount||w->taskCount||w->locked||w->userTreeTask)return false;
 return true;
}
