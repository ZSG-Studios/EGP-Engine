// SPDX-License-Identifier: MIT
#include "sensor_bridge.h"
#include "sensor.h"
static bool shape(const SpSensorView *s){return s&&s->hit_count<=s->hit_capacity&&s->first_count<=s->first_capacity&&s->second_count<=s->second_capacity&&(!s->hit_count||s->hits)&&(!s->first_count||s->overlaps1)&&(!s->second_count||s->overlaps2);}
bool spExportSensor(const void *p,SpSensorView *s){if(!p||!shape(s))return false;const b2Sensor *n=p;
 #define CHECK(a,outCount,outCap) if(n->a.count<0||n->a.capacity<0||n->a.count>n->a.capacity||(uint32_t)n->a.count>s->outCap||(!n->a.data&&n->a.count))return false
 CHECK(hits,hit_count,hit_capacity);CHECK(overlaps1,first_count,first_capacity);CHECK(overlaps2,second_count,second_capacity);
 #undef CHECK
 #define COPY(a,d,outCount,outCap) for(int i=0;i<n->a.count;++i)s->d[i]=(SpVisitor){n->a.data[i].shapeId,n->a.data[i].generation};s->outCount=n->a.count;s->outCap=n->a.capacity
 COPY(hits,hits,hit_count,hit_capacity);COPY(overlaps1,overlaps1,first_count,first_capacity);COPY(overlaps2,overlaps2,second_count,second_capacity);
 #undef COPY
 s->shape=n->shapeId;return true;}
bool spImportSensorCandidate(const SpSensorView *s,void *p){if(!p||!shape(s))return false;b2Sensor *n=p;
 #define CHECK(a,outCap,outCount) if(n->a.capacity<0||(uint32_t)n->a.capacity!=s->outCap||(!n->a.data&&s->outCount))return false
 CHECK(hits,hit_capacity,hit_count);CHECK(overlaps1,first_capacity,first_count);CHECK(overlaps2,second_capacity,second_count);
 #undef CHECK
 #define COPY(a,d,outCount) for(uint32_t i=0;i<s->outCount;++i)n->a.data[i]=(b2Visitor){s->d[i].shape,s->d[i].generation};n->a.count=s->outCount
 COPY(hits,hits,hit_count);COPY(overlaps1,overlaps1,first_count);COPY(overlaps2,overlaps2,second_count);
 #undef COPY
 n->shapeId=s->shape;return true;}
bool spSensorFixtureRoundTrip(const SpSensorView *s,SpSensorView *out){if(!shape(s)||s->hit_capacity>8||s->first_capacity>8||s->second_capacity>8)return false;b2Visitor hits[8],first[8],second[8];b2Sensor n={0};n.hits.data=hits;n.hits.capacity=s->hit_capacity;n.overlaps1.data=first;n.overlaps1.capacity=s->first_capacity;n.overlaps2.data=second;n.overlaps2.capacity=s->second_capacity;return spImportSensorCandidate(s,&n)&&spExportSensor(&n,out);}
