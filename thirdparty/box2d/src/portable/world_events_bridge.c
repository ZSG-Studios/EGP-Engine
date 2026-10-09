// SPDX-License-Identifier: MIT
#include "world_events_bridge.h"
#include "physics_world.h"
#define OUT(a,e) do{if((a).count<0||(a).capacity<(a).count||((a).capacity&&!(a).data))return false;(e)=(SpEventArray){(void*)(a).data,(uint32_t)(a).count,(uint32_t)(a).capacity};}while(0)
bool spExportWorldEvents(const void*p,SpWorldEventArrays*o){
 if(!p||!o)return false;const b2World*w=p;SpWorldEventArrays r={0};
 OUT(w->bodyMoveEvents,r.move);OUT(w->sensorBeginEvents,r.sensor_begin);OUT(w->contactBeginEvents,r.contact_begin);OUT(w->contactHitEvents,r.hit);OUT(w->jointEvents,r.joint);
 for(int i=0;i<2;++i){OUT(w->sensorEndEvents[i],r.sensor_end[i]);OUT(w->contactEndEvents[i],r.contact_end[i]);}
 r.world_id=w->worldId;*o=r;return true;
}
#undef OUT
static bool ok(const SpEventArray*a){return a->count<=a->capacity&&a->capacity<=100000&&(a->data||!a->capacity);}
#define BIND(a,e) do{(a).data=(e).data;(a).count=(int)(e).count;(a).capacity=(int)(e).capacity;}while(0)
bool spBindWorldEvents(void*p,const SpWorldEventArrays*e){
 if(!p||!e||!ok(&e->move)||!ok(&e->sensor_begin)||!ok(&e->contact_begin)||!ok(&e->hit)||!ok(&e->joint))return false;for(int i=0;i<2;++i)if(!ok(&e->sensor_end[i])||!ok(&e->contact_end[i]))return false;
 b2World*w=p;BIND(w->bodyMoveEvents,e->move);BIND(w->sensorBeginEvents,e->sensor_begin);BIND(w->contactBeginEvents,e->contact_begin);BIND(w->contactHitEvents,e->hit);BIND(w->jointEvents,e->joint);
 for(int i=0;i<2;++i){BIND(w->sensorEndEvents[i],e->sensor_end[i]);BIND(w->contactEndEvents[i],e->contact_end[i]);}
 return true;
}
#undef BIND
