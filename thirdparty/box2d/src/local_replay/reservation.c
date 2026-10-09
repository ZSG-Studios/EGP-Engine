#include "reservation.h"
#include "box2d/box2d.h"
#include "physics_world.h"
#include "world_snapshot.h"
#include "atomic.h"
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_B2_OWNER_CAPACITY 2
typedef struct spB2Arena {
    unsigned char* data;
    size_t capacity, used, allocations, freed_bytes;
    uint32_t generation;
} spB2Arena;
static spB2Arena owners[SP_B2_OWNER_CAPACITY];
static spB2ReservationStats stats;
#if defined(SP_B2_LOCAL_REPLAY_TESTS)
static bool fail_next;
#endif
static b2AtomicInt registry_lock;
static void lock_registry(void) { while (!b2AtomicCompareExchangeInt(&registry_lock, 0, 1)) {} }
static void unlock_registry(void) { b2AtomicStoreInt(&registry_lock, 0); }
static void *arena_alloc(size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, 32);
#else
    return aligned_alloc(32, size);
#endif
}
static void arena_free(void *p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}
#if defined(_MSC_VER)
__declspec(thread) static spB2Arena* current_arena;
#else
static _Thread_local spB2Arena* current_arena;
#endif

static void invariant(bool condition, const char* message) {
    if (!condition) { fprintf(stderr, "RESERVATION_INVARIANT: %s\n", message); abort(); }
}

// Native core calls these before its unchanged ordinary allocator/free path.
// No global allocator callbacks are installed or replaced. Registry locking is
// bounded to metadata; upstream allocation/free callbacks never run under it.
void* spB2TryAllocateReserved(size_t size) {
    if (!current_arena) return NULL;
    invariant(size && !(size & 31u), "native allocator alignment contract drift");
    spB2Arena* arena = current_arena;
    lock_registry();
    invariant(size <= arena->capacity - arena->used, "admitted candidate exceeded reservation");
    void* memory = arena->data + arena->used;
    arena->used += size;
    ++arena->allocations;
    ++stats.arena_allocations;
    unlock_registry();
    return memory;
}

bool spB2TryFreeReserved(void* memory, size_t size) {
    uintptr_t pointer = (uintptr_t)memory;
    size_t rounded = (size + 31u) & ~(size_t)31u;
    lock_registry();
    for (size_t i = 0; i < SP_B2_OWNER_CAPACITY; ++i) {
        spB2Arena* arena = owners + i;
        uintptr_t begin = (uintptr_t)arena->data;
        if (arena->data && pointer >= begin && pointer - begin < arena->capacity) {
            invariant(!(pointer & 31u) && pointer - begin <= arena->used &&
                      rounded <= arena->used - (pointer - begin), "invalid arena free extent");
            invariant(rounded <= arena->used - arena->freed_bytes, "arena free accounting exceeded allocation");
            arena->freed_bytes += rounded;
            ++stats.arena_free_calls;
            unlock_registry();
            return true;
        }
    }
    unlock_registry();
    return false;
}

#if defined(SP_B2_LOCAL_REPLAY_TESTS)
void spB2FixtureFailNextReservation(void) { fail_next = true; }
spB2ReservationStats spB2FixtureStats(void) {
    lock_registry();spB2ReservationStats result=stats;unlock_registry();return result;
}

#endif

bool spB2CheckedNextGeneration(uint32_t current, uint32_t* next) {
    if (!next || current == UINT32_MAX) return false;
    *next = current + 1u;
    return true;
}

static spB2Arena* reserve(size_t bytes, spB2ReserveStatus* status) {
    invariant(current_arena == NULL, "candidate scope must be non-nested");
    spB2Arena* arena = NULL;
    for (size_t i = 0; i < SP_B2_OWNER_CAPACITY; ++i) {
        // An exhausted token slot is permanently retired; stale handles never alias.
        if (!owners[i].data && owners[i].generation != UINT32_MAX) { arena = owners + i; break; }
    }
    if (!arena) { *status = spB2ReserveOwnerLimit; return NULL; }
    lock_registry();++stats.reservation_attempts;unlock_registry();
    void* memory = NULL;
#if defined(SP_B2_LOCAL_REPLAY_TESTS)
    if (fail_next) fail_next = false;
    else
#endif
    memory = arena_alloc(bytes);
    if (!memory) { *status = spB2ReserveOutOfMemory; return NULL; }
    uint32_t generation = 0;
    invariant(spB2CheckedNextGeneration(arena->generation, &generation), "owner generation exhausted");
    lock_registry();
    *arena = (spB2Arena){memory, bytes, 0, 0, 0, generation};
    stats.reservation_live_bytes += bytes;
    ++stats.active_owners;
    unlock_registry();
    return arena;
}

static void retire(spB2Arena* arena) {
    invariant(arena->data && current_arena == NULL && arena->used == arena->freed_bytes,
              "native teardown must free every arena allocation before retiring owner");
    lock_registry();
    void* memory = arena->data;
    stats.reservation_live_bytes -= arena->capacity;
    --stats.active_owners;
    arena->data = NULL;
    arena->capacity = arena->used = arena->allocations = arena->freed_bytes = 0;
    unlock_registry();
    arena_free(memory);
}

#if defined(SP_B2_LOCAL_REPLAY_TESTS)
bool spB2FixtureCheckShell(const spB2CandidateRequirements* requirements) {
    if (!requirements) return false;
    spB2ReserveStatus status;
    spB2Arena* arena = reserve(requirements->shell_bytes, &status);
    if (!arena) return false;
    current_arena = arena;
    b2WorldDef def = b2DefaultWorldDef();
    def.workerCount = 1;
    b2WorldId world = b2CreateWorld(&def);
    current_arena = NULL;
    bool matched = b2World_IsValid(world) && arena->used == requirements->shell_bytes &&
                   arena->allocations == requirements->shell_allocations;
    if (b2World_IsValid(world)) b2DestroyWorld(world);
    retire(arena);
    return matched;
}

#endif

spB2ReserveStatus spB2CreateReservedCandidate(b2WorldId original_world,
    const uint8_t* image, size_t size, size_t budget, spB2ReservedCandidate* candidate) {
    if (!candidate || !b2World_IsValid(original_world)) return spB2ReserveInvalid;
    if (b2GetWorldFromId(original_world)->locked) return spB2ReserveBusy;
    uintptr_t begin = (uintptr_t)image, output = (uintptr_t)candidate;
    if (!image || size > UINTPTR_MAX - begin || sizeof(*candidate) > UINTPTR_MAX - output ||
        (begin < output + sizeof(*candidate) && output < begin + size)) return spB2ReserveInvalid;
    spB2CandidateRequirements requirements;
    if (!spB2MeasureCandidateRequirements(image, size, &requirements)) return spB2ReserveInvalid;
    if (budget < requirements.total_bytes) return spB2ReserveBudget;
    spB2ReserveStatus status;
    spB2Arena* arena = reserve(requirements.total_bytes, &status);
    if (!arena) return status;
    current_arena = arena;
    b2WorldId world = b2CreateWorldFromSnapshot(image, (int)size, 1);
    current_arena = NULL;
    if (!b2World_IsValid(world)) { retire(arena); return spB2ReserveNativeFailure; }
    invariant(arena->used == requirements.total_bytes && arena->allocations == requirements.total_allocations,
              "candidate allocation plan differs from native requests");
    spB2ReservedCandidate result = {world, (uint32_t)(arena - owners), arena->generation};
    *candidate = result;
    return spB2ReserveOk;
}

bool spB2DestroyReservedCandidate(spB2ReservedCandidate* candidate) {
    if (!candidate || candidate->owner_slot >= SP_B2_OWNER_CAPACITY) return false;
    spB2Arena* arena = owners + candidate->owner_slot;
    if (!arena->data || arena->generation != candidate->owner_generation || !b2World_IsValid(candidate->world)) return false;
    b2DestroyWorld(candidate->world);
    retire(arena);
    *candidate = (spB2ReservedCandidate){0};
    return true;
}

bool spB2ReleaseDestroyedCandidate(spB2ReservedCandidate* candidate) {
    if (!candidate || candidate->owner_slot >= SP_B2_OWNER_CAPACITY) return false;
    spB2Arena* arena = owners + candidate->owner_slot;
    if (!arena->data || arena->generation != candidate->owner_generation ||
        b2World_IsValid(candidate->world) || arena->used != arena->freed_bytes) return false;
    retire(arena);
    *candidate = (spB2ReservedCandidate){0};
    return true;
}
