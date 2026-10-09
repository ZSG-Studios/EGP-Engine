// Appended after solver_preflight.inc in the private world_snapshot.c copy.
#include "reservation.h"
#include <stdint.h>

bool spB2CheckedCharge(size_t count, size_t element_size, size_t* bytes, size_t* allocations) {
    if (!bytes || !allocations || (element_size && count > SIZE_MAX / element_size)) return false;
    size_t raw = count * element_size;
    if (!raw) return true;
    if (raw > SIZE_MAX - 31u) return false;
    size_t rounded = (raw + 31u) & ~(size_t)31u;
    if (rounded > SIZE_MAX - *bytes || *allocations == SIZE_MAX) return false;
    *bytes += rounded;
    ++*allocations;
    return true;
}

bool spB2DefaultSerialShellRequirements(spB2CandidateRequirements* requirements) {
    if (!requirements) return false;
    spB2CandidateRequirements result = {0};
    size_t bytes = 0, allocations = 0;
#define CHARGE(n, type) do { if (!spB2CheckedCharge((n), sizeof(type), &bytes, &allocations)) return false; } while (0)
    // physics_world.c:183; arena_allocator.c:11-20.
    CHARGE(2048, char);
    CHARGE(32, b2StackEntry);
    // broad_phase.c:37-54; dynamic_tree.c:55-58; default capacities are all zero.
    for (int i = 0; i < b2_bodyTypeCount; ++i) CHARGE(1, uint64_t);
    CHARGE(16, int);
    CHARGE(32, b2SetItem);
    for (int i = 0; i < b2_bodyTypeCount; ++i) CHARGE(31, b2TreeNode);
    // constraint_graph.c:36-45: overflow has no body bitset/contact reserve.
    for (int i = 0; i < B2_OVERFLOW_INDEX; ++i) {
        CHARGE(1, uint64_t);
        CHARGE(16, b2ContactSim);
    }
    // physics_world.c:187-234; id_pool.c:6-9.
    for (int i = 0; i < 7; ++i) CHARGE(32, int);
    CHARGE(16, b2Body);
    CHARGE(8, b2SolverSet);
    CHARGE(16, b2Shape);
    CHARGE(4, b2ChainShape);
    CHARGE(16, b2Contact);
    CHARGE(16, b2Joint);
    CHARGE(16, b2Island);
    CHARGE(4, b2Sensor);
    CHARGE(16, b2BodySim);
    CHARGE(16, b2BodySim);
    CHARGE(16, b2BodyState);
    CHARGE(16, b2ContactSim);
    // physics_world.c:236-244.
    CHARGE(4, b2BodyMoveEvent);
    CHARGE(4, b2SensorBeginTouchEvent);
    CHARGE(4, b2SensorEndTouchEvent);
    CHARGE(4, b2SensorEndTouchEvent);
    CHARGE(4, b2ContactBeginTouchEvent);
    CHARGE(4, b2ContactEndTouchEvent);
    CHARGE(4, b2ContactEndTouchEvent);
    CHARGE(4, b2ContactHitEvent);
    CHARGE(4, b2JointEvent);
    // physics_world.c:111-130: precisely one serial worker; no native scheduler.
    CHARGE(1, b2TaskContext);
    CHARGE(1, b2SensorTaskContext);
    CHARGE(8, b2SensorHit);
    for (int i = 0; i < 3; ++i) CHARGE(16, uint64_t);
    CHARGE(4, uint64_t);
    CHARGE(4, uint64_t);
    CHARGE(2, uint64_t);
    // physics_world.c:317-320.
    for (int i = 0; i < 4; ++i) CHARGE(4, uint64_t);
#undef CHARGE
    result.shell_bytes = bytes;
    result.shell_allocations = allocations;
    result.total_bytes = bytes;
    result.total_allocations = allocations;
    *requirements = result;
    return true;
}

typedef struct spB2CandidateScan {
    spB2Preflight stream;
    size_t allocations;
} spB2CandidateScan;

static bool spB2CandidateCharge(spB2CandidateScan* scan, int64_t bytes) {
    if (!scan->stream.reader.ok || bytes < 0 || bytes > INT_MAX) return scan->stream.reader.ok = false;
    if (!bytes) return true;
    if (!spB2Charge(&scan->stream, bytes) || scan->allocations == SIZE_MAX) return scan->stream.reader.ok = false;
    ++scan->allocations;
    return true;
}

static int spB2CandidateCount(spB2CandidateScan* scan, int element_bytes, int minimum_bytes, int retained_capacity) {
    int count = b2SnapR_I32(&scan->stream.reader);
    if (!scan->stream.reader.ok || !b2SnapCheckCount(&scan->stream.reader, count, element_bytes, minimum_bytes)) {
        scan->stream.reader.ok = false;
        return 0;
    }
    // b2Array_Reserve grows to exactly count, not a geometric capacity.
    if (count > retained_capacity) spB2CandidateCharge(scan, (int64_t)count * element_bytes);
    return count;
}

static void spB2CandidateArray(spB2CandidateScan* scan, int element_bytes, int retained_capacity) {
    int count = spB2CandidateCount(scan, element_bytes, element_bytes, retained_capacity);
    spB2Skip(&scan->stream, (int64_t)count * element_bytes);
}

static void spB2CandidateBitSet(spB2CandidateScan* scan) {
    uint32_t count = b2SnapR_U32(&scan->stream.reader);
    spB2CandidateCharge(scan, (int64_t)(count ? count : 1) * sizeof(uint64_t));
    spB2Skip(&scan->stream, (int64_t)count * sizeof(uint64_t));
}

bool spB2MeasureCandidateRequirements(const uint8_t* image, size_t size,
                                     spB2CandidateRequirements* requirements) {
    if (!requirements) return false;
    // Reuse the independently qualified complete framing pass and disjoint-output check.
    uintptr_t begin = (uintptr_t)image, output = (uintptr_t)requirements;
    if (!image || size > UINTPTR_MAX - begin || sizeof(*requirements) > UINTPTR_MAX - output ||
        (begin < output + sizeof(*requirements) && output < begin + size)) return false;
    spB2ImageRequirements conservative;
    if (!spB2PreflightSolverImage(image, size, &conservative)) return false;
    spB2CandidateRequirements result;
    if (!spB2DefaultSerialShellRequirements(&result)) return false;
    spB2CandidateScan scan = {0};
    if (!b2OpenSnapshotImage(image, (int)size, &scan.stream.reader)) return false;
    b2World scalars = {0};
    b2DesWorldConfig(&scan.stream.reader, &scalars);
    // Each shell id pool retains capacity 32.
    for (int i = 0; i < 7; ++i) {
        spB2Skip(&scan.stream, sizeof(int));
        spB2CandidateArray(&scan, sizeof(int), 32);
    }
    int sets = spB2CandidateCount(&scan, sizeof(b2SolverSet), 6 * sizeof(int), 8);
    for (int i = 0; i < sets && scan.stream.reader.ok; ++i) {
        spB2Skip(&scan.stream, sizeof(int));
        // Decoder destroys all shell solver inner arrays before this loop.
        spB2CandidateArray(&scan, sizeof(b2BodySim), 0);
        spB2CandidateArray(&scan, sizeof(b2BodyState), 0);
        spB2CandidateArray(&scan, sizeof(b2JointSim), 0);
        spB2CandidateArray(&scan, sizeof(b2ContactSim), 0);
        spB2CandidateArray(&scan, sizeof(b2IslandSim), 0);
    }
    spB2CandidateArray(&scan, sizeof(b2Body), 16);
    spB2CandidateArray(&scan, sizeof(b2Shape), 16);
    spB2CandidateArray(&scan, sizeof(b2Contact), 16);
    spB2CandidateArray(&scan, sizeof(b2Joint), 16);
    int chains = spB2CandidateCount(&scan, sizeof(b2ChainShape), 5 * sizeof(int) + sizeof(uint16_t), 0);
    for (int i = 0; i < chains && scan.stream.reader.ok; ++i) {
        int id = b2SnapR_I32(&scan.stream.reader);
        spB2Skip(&scan.stream, 2 * sizeof(int));
        int shapes = b2SnapR_I32(&scan.stream.reader), materials = b2SnapR_I32(&scan.stream.reader);
        spB2Skip(&scan.stream, sizeof(uint16_t));
        if (id != B2_NULL_INDEX) {
            spB2CandidateCharge(&scan, (int64_t)shapes * sizeof(int));
            spB2Skip(&scan.stream, (int64_t)shapes * sizeof(int));
            spB2CandidateCharge(&scan, (int64_t)materials * sizeof(b2SurfaceMaterial));
            spB2Skip(&scan.stream, (int64_t)materials * sizeof(b2SurfaceMaterial));
        }
    }
    int sensors = spB2CandidateCount(&scan, sizeof(b2Sensor), 4 * sizeof(int), 0);
    for (int i = 0; i < sensors && scan.stream.reader.ok; ++i) {
        spB2Skip(&scan.stream, sizeof(int));
        for (int j = 0; j < 3; ++j) spB2CandidateArray(&scan, sizeof(b2Visitor), 0);
    }
    int islands = spB2CandidateCount(&scan, sizeof(b2Island), 7 * sizeof(int), 0);
    for (int i = 0; i < islands && scan.stream.reader.ok; ++i) {
        spB2Skip(&scan.stream, 4 * sizeof(int));
        spB2CandidateArray(&scan, sizeof(int), 0);
        spB2CandidateArray(&scan, sizeof(b2ContactLink), 0);
        spB2CandidateArray(&scan, sizeof(b2JointLink), 0);
    }
    for (int i = 0; i < b2_bodyTypeCount; ++i) {
        spB2Skip(&scan.stream, 2 * sizeof(int));
        int capacity = b2SnapR_I32(&scan.stream.reader);
        spB2Skip(&scan.stream, 2 * sizeof(int));
        spB2CandidateCharge(&scan, (int64_t)capacity * sizeof(b2TreeNode));
        spB2Skip(&scan.stream, (int64_t)capacity * sizeof(b2TreeNode));
    }
    for (int i = 0; i < b2_bodyTypeCount; ++i) spB2CandidateBitSet(&scan);
    spB2CandidateArray(&scan, sizeof(int), 0);
    uint32_t capacity = b2SnapR_U32(&scan.stream.reader);
    spB2Skip(&scan.stream, sizeof(uint32_t));
    spB2CandidateCharge(&scan, (int64_t)capacity * sizeof(b2SetItem));
    spB2Skip(&scan.stream, (int64_t)capacity * sizeof(b2SetItem));
    for (int i = 0; i < B2_GRAPH_COLOR_COUNT; ++i) {
        if (i != B2_OVERFLOW_INDEX) spB2CandidateBitSet(&scan);
        spB2CandidateArray(&scan, sizeof(b2ContactSim), i != B2_OVERFLOW_INDEX ? 16 : 0);
        spB2CandidateArray(&scan, sizeof(b2JointSim), 0);
    }
    if (!scan.stream.reader.ok || scan.stream.reader.cursor != scan.stream.reader.size ||
        scan.stream.allocations > conservative.payload_allocation_bound ||
        scan.stream.allocations > SIZE_MAX - result.shell_bytes ||
        scan.allocations > SIZE_MAX - result.shell_allocations) return false;
    result.encoded_bytes = size;
    result.payload_bytes = scan.stream.allocations;
    result.payload_allocations = scan.allocations;
    result.total_bytes += result.payload_bytes;
    result.total_allocations += result.payload_allocations;
    *requirements = result;
    return true;
}
