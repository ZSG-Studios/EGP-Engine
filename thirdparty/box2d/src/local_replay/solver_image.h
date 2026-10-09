#pragma once

#include "box2d/id.h"
#include <stddef.h>
#include <stdint.h>

// PRIVATE PROTOTYPE. These are native-layout solver images, not engine recovery
// snapshots. The engine wrapper, queued commands, effects and callbacks need
// separate participants before any recovery capability may be advertised.
typedef enum spB2ImageStatus {
    spB2ImageOk = 0,
    spB2ImageInvalidArgument,
    spB2ImageBusy,
    spB2ImageCapacity,
    spB2ImageChanged
} spB2ImageStatus;

// Caller owns the world thread, excludes concurrent world mutation, and has
// drained effects before this boundary. All caller buffers and written storage
// must be disjoint from every live world allocation as well as each other.
// Outputs are changed only on success. Neither function allocates.
spB2ImageStatus spB2MeasureSolverImage(b2WorldId world, size_t* bytes);
spB2ImageStatus spB2CaptureSolverImage(b2WorldId world,
    void* staging, size_t staging_capacity, void* output, size_t output_capacity,
    size_t* written);
