#pragma once

// Internal serial native-layout candidate allocation. No portable recovery API.
#include "box2d/id.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct spB2CandidateRequirements {
    size_t encoded_bytes;
    size_t shell_bytes;
    size_t shell_allocations;
    size_t payload_bytes;
    size_t payload_allocations;
    size_t total_bytes;
    size_t total_allocations;
} spB2CandidateRequirements;

// All additions/multiplications/rounding are checked. Output unchanged on failure.
bool spB2CheckedCharge(size_t count, size_t element_size, size_t* bytes, size_t* allocations);
bool spB2CheckedNextGeneration(uint32_t current, uint32_t* next);
bool spB2DefaultSerialShellRequirements(spB2CandidateRequirements* requirements);
bool spB2MeasureCandidateRequirements(const uint8_t* image, size_t size,
                                     spB2CandidateRequirements* requirements);

typedef struct spB2ReservedCandidate {
    b2WorldId world;
    uint32_t owner_slot;
    uint32_t owner_generation;
} spB2ReservedCandidate;

// Optional borrowed process owner. It must outlive every published Space lease;
// callbacks are non-throwing and never mutate the source/candidate worlds.
// Kept per arena until native world destruction, never installed globally.
typedef struct spB2CandidateAllocator {
    void* context;
    void* (*allocate)(void* context, size_t bytes, size_t alignment);
    void (*deallocate)(void* context, void* memory);
} spB2CandidateAllocator;

typedef enum spB2ReserveStatus {
    spB2ReserveOk,
    spB2ReserveInvalid,
    spB2ReserveBusy,
    spB2ReserveBudget,
    spB2ReserveOutOfMemory,
    spB2ReserveOwnerLimit,
    spB2ReserveNativeFailure
} spB2ReserveStatus;

typedef struct spB2ReservationStats {
    size_t reservation_attempts;
    size_t reservation_live_bytes;
    size_t active_owners;
    size_t arena_allocations;
    size_t arena_free_calls;
} spB2ReservationStats;

// ALL candidate operations are globally serialized across worlds. The EGP
// participant enforces main-thread admission plus the process-wide simulation mutex. Ordinary solver
// allocations retain the original b2SetAllocator/default behavior. The native
// core consults these routes before that unchanged path; no callbacks are held
// under the registry metadata lock. Image/output/live-world storage are disjoint.
void* spB2TryAllocateReserved(size_t aligned_size);
bool spB2TryFreeReserved(void* memory, size_t original_size);
#if defined(SP_B2_LOCAL_REPLAY_TESTS)
void spB2FixtureFailNextReservation(void);
spB2ReservationStats spB2FixtureStats(void);
bool spB2FixtureCheckShell(const spB2CandidateRequirements* requirements);
#endif
spB2ReserveStatus spB2CreateReservedCandidate(b2WorldId original_world,
    const uint8_t* image, size_t size, size_t budget, spB2ReservedCandidate* candidate);
spB2ReserveStatus spB2CreateChargedCandidate(b2WorldId original_world,
    const uint8_t* image, size_t size, size_t budget,
    spB2CandidateAllocator allocator, spB2ReservedCandidate* candidate);
bool spB2DestroyReservedCandidate(spB2ReservedCandidate* candidate);
// Called only after the owning engine Space has destroyed the same world.
// Does not destroy a world; verifies generation and all arena frees first.
bool spB2ReleaseDestroyedCandidate(spB2ReservedCandidate* candidate);
