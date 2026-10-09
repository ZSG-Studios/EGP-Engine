#include "binding.h"
#include "box2d/box2d.h"
#include "physics_world.h"
#include <stdbool.h>
#include <limits.h>

static bool extent(const void* pointer, size_t count, size_t width,
                   uintptr_t* begin, uintptr_t* end) {
    if (!pointer || count > SIZE_MAX / width) return false;
    size_t bytes = count * width;
    *begin = (uintptr_t)pointer;
    if (bytes > UINTPTR_MAX - *begin) return false;
    *end = *begin + bytes;
    return true;
}
static bool overlaps(uintptr_t a, uintptr_t ae, uintptr_t b, uintptr_t be) {
    return a < be && b < ae;
}
static spB2BindingStatus worlds(b2WorldId source, b2WorldId target) {
    if (!b2World_IsValid(source) || !b2World_IsValid(target) ||
        source.index1 == target.index1) return spB2BindingInvalid;
    if (b2GetWorldFromId(source)->locked || b2GetWorldFromId(target)->locked)
        return spB2BindingBusy;
    return spB2BindingOk;
}
static spB2BindingStatus check_one(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* binding, uint64_t* mapped) {
    /* All four selected native ID types have equal store/load bit layouts. */
    b2BodyId base = b2LoadBodyId(binding->source_id);
    if (base.index1 <= 0 || base.world0 != source.index1 - 1)
        return spB2BindingInvalid;
    base.world0 = target.index1 - 1;
    *mapped = b2StoreBodyId(base);
    bool valid_source = false, valid_target = false;
    void* source_data = NULL;
    void* target_data = NULL;
    switch (binding->kind) {
        case spB2BindingBody: {
            b2BodyId a = b2LoadBodyId(binding->source_id), b = b2LoadBodyId(*mapped);
            valid_source = b2Body_IsValid(a); valid_target = b2Body_IsValid(b);
            if (valid_source) source_data = b2Body_GetUserData(a);
            if (valid_target) target_data = b2Body_GetUserData(b);
            break;
        }
        case spB2BindingShape: {
            b2ShapeId a = b2LoadShapeId(binding->source_id), b = b2LoadShapeId(*mapped);
            valid_source = b2Shape_IsValid(a); valid_target = b2Shape_IsValid(b);
            if (valid_source) source_data = b2Shape_GetUserData(a);
            if (valid_target) target_data = b2Shape_GetUserData(b);
            break;
        }
        case spB2BindingJoint: {
            b2JointId a = b2LoadJointId(binding->source_id), b = b2LoadJointId(*mapped);
            valid_source = b2Joint_IsValid(a); valid_target = b2Joint_IsValid(b);
            if (valid_source) source_data = b2Joint_GetUserData(a);
            if (valid_target) target_data = b2Joint_GetUserData(b);
            break;
        }
        case spB2BindingChain: {
            if (binding->retained_user_data) return spB2BindingInvalid;
            valid_source = b2Chain_IsValid(b2LoadChainId(binding->source_id));
            valid_target = b2Chain_IsValid(b2LoadChainId(*mapped));
            break;
        }
        default: return spB2BindingInvalid;
    }
    if (!valid_source || !valid_target || source_data != binding->retained_user_data)
        return spB2BindingStale;
    return target_data ? spB2BindingAlreadyBound : spB2BindingOk;
}
static spB2BindingStatus check_plan(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* bindings, size_t count, const uint64_t* mapped) {
    spB2BindingStatus status = worlds(source, target);
    if (status != spB2BindingOk) return status;
    for (size_t i = 0; i < count; ++i) {
        uint64_t target_id;
        status = check_one(source, target, &bindings[i], &target_id);
        if (status != spB2BindingOk) return status;
        if (mapped && mapped[i] != target_id) return spB2BindingStale;
        for (size_t j = 0; j < i; ++j)
            if (bindings[j].kind == bindings[i].kind &&
                bindings[j].source_id == bindings[i].source_id)
                return spB2BindingInvalid;
    }
    return spB2BindingOk;
}
spB2BindingStatus spB2PrepareLocalBindings(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* bindings, size_t count,
    uint64_t* mapped_ids, size_t capacity, size_t* written) {
    uintptr_t b, be, o, oe, w, we;
    if (count > SP_B2_LOCAL_BINDING_LIMIT) return spB2BindingCapacity;
    if (!extent(written, 1, sizeof(*written), &w, &we)) return spB2BindingInvalid;
    if (capacity < count) return spB2BindingCapacity;
    if (count && (!extent(bindings, count, sizeof(*bindings), &b, &be) ||
        !extent(mapped_ids, count, sizeof(*mapped_ids), &o, &oe) ||
        overlaps(b, be, o, oe) || overlaps(b, be, w, we) || overlaps(o, oe, w, we)))
        return spB2BindingInvalid;
    spB2BindingStatus status = check_plan(source, target, bindings, count, NULL);
    if (status != spB2BindingOk) return status;
    for (size_t i = 0; i < count; ++i) {
        b2BodyId id = b2LoadBodyId(bindings[i].source_id);
        id.world0 = target.index1 - 1;
        mapped_ids[i] = b2StoreBodyId(id);
    }
    *written = count;
    return spB2BindingOk;
}
spB2BindingStatus spB2CommitLocalBindings(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* bindings, const uint64_t* mapped_ids, size_t count) {
    uintptr_t b, be, o, oe;
    if (count > SP_B2_LOCAL_BINDING_LIMIT) return spB2BindingCapacity;
    if (count && (!extent(bindings, count, sizeof(*bindings), &b, &be) ||
        !extent(mapped_ids, count, sizeof(*mapped_ids), &o, &oe) || overlaps(b, be, o, oe)))
        return spB2BindingInvalid;
    spB2BindingStatus status = check_plan(source, target, bindings, count, mapped_ids);
    if (status != spB2BindingOk) return status;
    for (size_t i = 0; i < count; ++i) {
        switch (bindings[i].kind) {
            case spB2BindingBody: b2Body_SetUserData(b2LoadBodyId(mapped_ids[i]), bindings[i].retained_user_data); break;
            case spB2BindingShape: b2Shape_SetUserData(b2LoadShapeId(mapped_ids[i]), bindings[i].retained_user_data); break;
            case spB2BindingJoint: b2Joint_SetUserData(b2LoadJointId(mapped_ids[i]), bindings[i].retained_user_data); break;
            case spB2BindingChain: break;
            default: return spB2BindingInvalid; /* Preflight excludes this. */
        }
    }
    return spB2BindingOk;
}
