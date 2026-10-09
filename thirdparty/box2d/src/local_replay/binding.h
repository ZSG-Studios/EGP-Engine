#pragma once

/* PRIVATE native foundation, not a recovery capability or wire format.
 * Sources and targets must be serial, distinct, locally captured worlds with
 * unchanged topology. The caller retains all pointer owners throughout. */
#include "box2d/id.h"
#include <stddef.h>
#include <stdint.h>

typedef enum spB2BindingKind {
    spB2BindingBody = 1,
    spB2BindingShape = 2,
    spB2BindingJoint = 3,
    spB2BindingChain = 4
} spB2BindingKind;

#define SP_B2_LOCAL_BINDING_LIMIT 4096u

typedef struct spB2LocalBinding {
    spB2BindingKind kind;
    uint64_t source_id;
    void* retained_user_data;
} spB2LocalBinding;

typedef enum spB2BindingStatus {
    spB2BindingOk,
    spB2BindingInvalid,
    spB2BindingBusy,
    spB2BindingCapacity,
    spB2BindingStale,
    spB2BindingAlreadyBound
} spB2BindingStatus;

/* Stage every target ID before changing any native pointer. Output and written
 * are unchanged on failure. At most SP_B2_LOCAL_BINDING_LIMIT entries are
 * admitted. Bindings/output/written are valid, aligned, mutually disjoint and
 * disjoint from native world storage; no allocation or callback is performed.
 * Native ID layout is pinned to this local source profile, never portable. */
spB2BindingStatus spB2PrepareLocalBindings(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* bindings, size_t count,
    uint64_t* mapped_ids, size_t capacity, size_t* written);

/* Revalidates the full plan before a callback-free commit. Target body/shape/
 * joint pointers must still be null. Chains have no native user-data setter:
 * their retained_user_data must be null, and only their wrapper ID is mapped.
 * No old world or wrapper object is modified. Full wrapper/queue restoration
 * and presolve callback reinstallation remain separate participant work. */
spB2BindingStatus spB2CommitLocalBindings(b2WorldId source, b2WorldId target,
    const spB2LocalBinding* bindings, const uint64_t* mapped_ids, size_t count);
