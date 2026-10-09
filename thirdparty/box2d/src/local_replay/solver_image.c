#include "solver_image.h"
#include "box2d/box2d.h"
#include "physics_world.h"
#include "world_snapshot.h"
#include <limits.h>
#include <string.h>

static bool sp_ranges_overlap(const void* left, size_t left_size,
                              const void* right, size_t right_size) {
    uintptr_t a = (uintptr_t)left;
    uintptr_t b = (uintptr_t)right;
    if (left_size > UINTPTR_MAX - a || right_size > UINTPTR_MAX - b) return true;
    return a < b + right_size && b < a + left_size;
}

spB2ImageStatus spB2MeasureSolverImage(b2WorldId id, size_t* bytes) {
    if (bytes == NULL || !b2World_IsValid(id)) return spB2ImageInvalidArgument;
    b2World* world = b2GetWorldFromId(id);
    if (world->locked) return spB2ImageBusy;
    b2RecBuffer count = {0};
    count.countOnly = true;
    count.fixedCapacity = true;
    count.capacity = INT_MAX;
    b2SerializeWorld(world, &count);
    if (count.failed) return spB2ImageCapacity;
    *bytes = (size_t)count.size;
    return spB2ImageOk;
}

spB2ImageStatus spB2CaptureSolverImage(b2WorldId id,
    void* staging, size_t staging_capacity, void* output, size_t output_capacity,
    size_t* written) {
    if (staging == NULL || output == NULL || written == NULL ||
        staging_capacity > INT_MAX || output_capacity > INT_MAX ||
        sp_ranges_overlap(staging, staging_capacity, output, output_capacity) ||
        sp_ranges_overlap(staging, staging_capacity, written, sizeof(*written)) ||
        sp_ranges_overlap(output, output_capacity, written, sizeof(*written))) {
        return spB2ImageInvalidArgument;
    }
    size_t required = 0;
    spB2ImageStatus status = spB2MeasureSolverImage(id, &required);
    if (status != spB2ImageOk) return status;
    if (required > staging_capacity || required > output_capacity) return spB2ImageCapacity;
    b2World* world = b2GetWorldFromId(id);
    uint64_t step = world->stepIndex;
    b2RecBuffer buffer = {0};
    buffer.data = staging;
    buffer.capacity = (int)staging_capacity;
    buffer.fixedCapacity = true;
    b2SerializeWorld(world, &buffer);
    if (buffer.failed) return spB2ImageCapacity;
    if (world->locked || world->stepIndex != step || (size_t)buffer.size != required)
        return spB2ImageChanged;
    memcpy(output, staging, required);
    *written = required;
    return spB2ImageOk;
}
