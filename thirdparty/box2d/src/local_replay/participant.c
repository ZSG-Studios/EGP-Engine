#include "participant.h"
#include "physics_world.h"
#include "box2d/box2d.h"
#include <string.h>

bool spB2ParticipantQuiescent(b2WorldId id) {
    if (!b2World_IsValid(id)) return false;
    b2World *w = b2GetWorldFromId(id);
    return !w->locked && w->workerCount == 1 && !w->recording &&
        !w->activeTaskCount && !w->userTreeTask && !w->scheduler;
}
static bool vector_equal(b2Vec2 a, b2Vec2 b) { return a.x == b.x && a.y == b.y; }
static bool geometry_equal(const b2Shape *a, const b2Shape *b) {
    switch(a->type) {
    case b2_circleShape:
        return vector_equal(a->circle.center,b->circle.center) && a->circle.radius==b->circle.radius;
    case b2_capsuleShape:
        return vector_equal(a->capsule.center1,b->capsule.center1) && vector_equal(a->capsule.center2,b->capsule.center2) && a->capsule.radius==b->capsule.radius;
    case b2_segmentShape:
        return vector_equal(a->segment.point1,b->segment.point1) && vector_equal(a->segment.point2,b->segment.point2);
    case b2_polygonShape:
        if (a->polygon.count!=b->polygon.count || a->polygon.radius!=b->polygon.radius ||
            !vector_equal(a->polygon.centroid,b->polygon.centroid)) return false;
        for(int i=0;i<a->polygon.count;++i)
            if (!vector_equal(a->polygon.vertices[i],b->polygon.vertices[i]) || !vector_equal(a->polygon.normals[i],b->polygon.normals[i])) return false;
        return true;
    default: return false; /* Chains are not admitted by this participant. */
    }
}
bool spB2ParticipantSameTopology(b2WorldId current, b2WorldId candidate) {
    if (!spB2ParticipantQuiescent(current) || !spB2ParticipantQuiescent(candidate)) return false;
    b2World *a=b2GetWorldFromId(current), *b=b2GetWorldFromId(candidate);
    if (!vector_equal(a->gravity,b->gravity) || a->hitEventThreshold!=b->hitEventThreshold ||
        a->restitutionThreshold!=b->restitutionThreshold || a->maxLinearSpeed!=b->maxLinearSpeed ||
        a->contactSpeed!=b->contactSpeed || a->contactHertz!=b->contactHertz ||
        a->contactDampingRatio!=b->contactDampingRatio || a->contactRecycleDistance!=b->contactRecycleDistance ||
        a->enableSleep!=b->enableSleep || a->enableWarmStarting!=b->enableWarmStarting ||
        a->enableContactSoftening!=b->enableContactSoftening || a->enableContinuous!=b->enableContinuous ||
        a->enableSpeculative!=b->enableSpeculative) return false;
    if (a->bodies.count!=b->bodies.count || a->shapes.count!=b->shapes.count ||
        a->chainShapes.count || b->chainShapes.count || b2GetIdCount(&a->jointIdPool) || b2GetIdCount(&b->jointIdPool) ||
        a->sensors.count || b->sensors.count) return false;
    for(int i=0;i<a->bodies.count;++i) {
        const b2Body *x=a->bodies.data+i,*y=b->bodies.data+i;
        if(x->id!=y->id) return false;
        if(x->id==B2_NULL_INDEX) continue;
        if(x->generation!=y->generation || x->type!=y->type || x->headShapeId!=y->headShapeId ||
            x->shapeCount!=y->shapeCount || x->headChainId!=y->headChainId || x->jointCount || y->jointCount ||
            x->mass!=y->mass || x->inertia!=y->inertia || x->sleepThreshold!=y->sleepThreshold) return false;
    }
    for(int i=0;i<a->shapes.count;++i) {
        const b2Shape *x=a->shapes.data+i,*y=b->shapes.data+i;
        if(x->id!=y->id) return false;
        if(x->id==B2_NULL_INDEX) continue;
        if(x->generation!=y->generation || x->bodyId!=y->bodyId || x->prevShapeId!=y->prevShapeId ||
            x->nextShapeId!=y->nextShapeId || x->type!=y->type || x->sensorIndex!=y->sensorIndex ||
            x->density!=y->density || x->aabbMargin!=y->aabbMargin ||
            x->material.friction!=y->material.friction || x->material.restitution!=y->material.restitution ||
            x->material.rollingResistance!=y->material.rollingResistance || x->material.tangentSpeed!=y->material.tangentSpeed ||
            x->material.userMaterialId!=y->material.userMaterialId ||
            x->material.customColor!=y->material.customColor ||
            x->filter.categoryBits!=y->filter.categoryBits || x->filter.maskBits!=y->filter.maskBits || x->filter.groupIndex!=y->filter.groupIndex ||
            x->enableSensorEvents!=y->enableSensorEvents || x->enableContactEvents!=y->enableContactEvents ||
            x->enableCustomFiltering!=y->enableCustomFiltering || x->enableHitEvents!=y->enableHitEvents ||
            x->enablePreSolveEvents!=y->enablePreSolveEvents || !geometry_equal(x,y)) return false;
    }
    return true;
}
bool spB2ParticipantCopyWiring(b2WorldId current,b2WorldId candidate) {
    if (!spB2ParticipantQuiescent(current) || !spB2ParticipantQuiescent(candidate)) return false;
    b2World *a=b2GetWorldFromId(current),*b=b2GetWorldFromId(candidate);
    b->frictionCallback=a->frictionCallback; b->restitutionCallback=a->restitutionCallback;
    b->preSolveFcn=a->preSolveFcn; b->preSolveContext=a->preSolveContext;
    b->customFilterFcn=a->customFilterFcn; b->customFilterContext=a->customFilterContext;
    b->enqueueTaskFcn=a->enqueueTaskFcn; b->finishTaskFcn=a->finishTaskFcn;
    b->userTaskContext=a->userTaskContext; b->userData=a->userData;
    /* Events have been consumed by the wrapper boundary and are not part of
     * the upstream image. Never leave a body indexing the empty event array. */
    for(int i=0;i<b->bodies.count;++i)if(b->bodies.data[i].id!=B2_NULL_INDEX)
        b->bodies.data[i].bodyMoveIndex=B2_NULL_INDEX;
    return true;
}
