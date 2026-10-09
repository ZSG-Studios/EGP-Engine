// Included in the private world_snapshot.c TU to share its exact native layout
// header and scalar decoder. Never copied into the production engine yet.
#include "solver_preflight.h"

typedef struct spB2Preflight {
    b2SnapReader reader;
    size_t allocations;
} spB2Preflight;

static bool spB2Skip(spB2Preflight* p, int64_t bytes) {
    if (!p->reader.ok || bytes < 0 || bytes > INT_MAX) return p->reader.ok = false;
    b2SnapRCheck(&p->reader, (int)bytes);
    if (p->reader.ok) p->reader.cursor += (int)bytes;
    return p->reader.ok;
}

static bool spB2Charge(spB2Preflight* p, int64_t bytes) {
    if (!p->reader.ok || bytes < 0 || bytes > INT_MAX) return p->reader.ok = false;
    size_t aligned = bytes ? ((size_t)bytes + 31u) & ~(size_t)31u : 0;
    if (aligned > SIZE_MAX - p->allocations) return p->reader.ok = false;
    p->allocations += aligned;
    return true;
}

static int spB2Count(spB2Preflight* p, int element_bytes, int minimum_bytes) {
    int count = b2SnapR_I32(&p->reader);
    if (!p->reader.ok || !b2SnapCheckCount(&p->reader, count, element_bytes, minimum_bytes)) {
        p->reader.ok = false;
        return 0;
    }
    spB2Charge(p, (int64_t)count * element_bytes);
    return count;
}

static bool spB2ArrayImage(spB2Preflight* p, int element_bytes) {
    int count = spB2Count(p, element_bytes, element_bytes);
    return spB2Skip(p, (int64_t)count * element_bytes);
}

static bool spB2BitSetImage(spB2Preflight* p) {
    uint32_t count = b2SnapR_U32(&p->reader);
    if (!p->reader.ok || count > INT_MAX / sizeof(uint64_t)) return p->reader.ok = false;
    return spB2Charge(p, (int64_t)(count ? count : 1) * (int64_t)sizeof(uint64_t)) &&
           spB2Skip(p, (int64_t)count * (int64_t)sizeof(uint64_t));
}

static bool spB2HashSetImage(spB2Preflight* p) {
    uint32_t capacity = b2SnapR_U32(&p->reader), count = b2SnapR_U32(&p->reader);
    if (!p->reader.ok || capacity > INT_MAX / sizeof(b2SetItem) ||
        (capacity & (capacity - 1)) != 0 || count > capacity) return p->reader.ok = false;
    return spB2Charge(p, (int64_t)capacity * (int64_t)sizeof(b2SetItem)) &&
           spB2Skip(p, (int64_t)capacity * (int64_t)sizeof(b2SetItem));
}

static bool spB2TreeImage(spB2Preflight* p) {
    int root = b2SnapR_I32(&p->reader);
    int count = b2SnapR_I32(&p->reader);
    int capacity = b2SnapR_I32(&p->reader);
    int free_list = b2SnapR_I32(&p->reader);
    int proxies = b2SnapR_I32(&p->reader);
    if (!p->reader.ok || capacity < 0 || count < 0 || count > capacity ||
        proxies < 0 || proxies > count || root < -1 || root >= capacity ||
        free_list < -1 || free_list >= capacity) return p->reader.ok = false;
    return spB2Charge(p, (int64_t)capacity * (int64_t)sizeof(b2TreeNode)) &&
           spB2Skip(p, (int64_t)capacity * (int64_t)sizeof(b2TreeNode));
}

bool spB2PreflightSolverImage(const uint8_t* image, size_t size,
                            spB2ImageRequirements* requirements) {
    uintptr_t begin = (uintptr_t)image, output = (uintptr_t)requirements;
    if (!image || !requirements || size > INT_MAX || size > UINTPTR_MAX - begin ||
        sizeof(*requirements) > UINTPTR_MAX - output ||
        (begin < output + sizeof(*requirements) && output < begin + size)) return false;
    spB2Preflight p = {0};
    if (!b2OpenSnapshotImage(image, (int)size, &p.reader)) return false;
    b2World scalars = {0};
    b2DesWorldConfig(&p.reader, &scalars);
    for (int pool = 0; pool < 7 && p.reader.ok; ++pool) {
        int next = b2SnapR_I32(&p.reader);
        if (next < 0) p.reader.ok = false;
        spB2ArrayImage(&p, sizeof(int));
    }
    int sets = spB2Count(&p, sizeof(b2SolverSet), 6 * (int64_t)sizeof(int));
    for (int i = 0; i < sets && p.reader.ok; ++i) {
        spB2Skip(&p, sizeof(int));
        spB2ArrayImage(&p, sizeof(b2BodySim));
        spB2ArrayImage(&p, sizeof(b2BodyState));
        spB2ArrayImage(&p, sizeof(b2JointSim));
        spB2ArrayImage(&p, sizeof(b2ContactSim));
        spB2ArrayImage(&p, sizeof(b2IslandSim));
    }
    spB2ArrayImage(&p, sizeof(b2Body));
    spB2ArrayImage(&p, sizeof(b2Shape));
    spB2ArrayImage(&p, sizeof(b2Contact));
    spB2ArrayImage(&p, sizeof(b2Joint));
    int chains = spB2Count(&p, sizeof(b2ChainShape), 5 * (int64_t)sizeof(int) + sizeof(uint16_t));
    for (int i = 0; i < chains && p.reader.ok; ++i) {
        int id = b2SnapR_I32(&p.reader);
        spB2Skip(&p, 2 * (int64_t)sizeof(int));
        int shapes = b2SnapR_I32(&p.reader), materials = b2SnapR_I32(&p.reader);
        spB2Skip(&p, sizeof(uint16_t));
        if (id != B2_NULL_INDEX) {
            spB2Charge(&p, (int64_t)shapes * (int64_t)sizeof(int));
            spB2Skip(&p, (int64_t)shapes * (int64_t)sizeof(int));
            spB2Charge(&p, (int64_t)materials * (int64_t)sizeof(b2SurfaceMaterial));
            spB2Skip(&p, (int64_t)materials * (int64_t)sizeof(b2SurfaceMaterial));
        }
    }
    int sensors = spB2Count(&p, sizeof(b2Sensor), 4 * (int64_t)sizeof(int));
    for (int i = 0; i < sensors && p.reader.ok; ++i) {
        spB2Skip(&p, sizeof(int));
        for (int j = 0; j < 3; ++j) spB2ArrayImage(&p, sizeof(b2Visitor));
    }
    int islands = spB2Count(&p, sizeof(b2Island), 7 * (int64_t)sizeof(int));
    for (int i = 0; i < islands && p.reader.ok; ++i) {
        spB2Skip(&p, 4 * (int64_t)sizeof(int));
        spB2ArrayImage(&p, sizeof(int));
        spB2ArrayImage(&p, sizeof(b2ContactLink));
        spB2ArrayImage(&p, sizeof(b2JointLink));
    }
    for (int i = 0; i < b2_bodyTypeCount; ++i) spB2TreeImage(&p);
    for (int i = 0; i < b2_bodyTypeCount; ++i) spB2BitSetImage(&p);
    spB2ArrayImage(&p, sizeof(int));
    spB2HashSetImage(&p);
    for (int i = 0; i < B2_GRAPH_COLOR_COUNT && p.reader.ok; ++i) {
        if (i != B2_OVERFLOW_INDEX) spB2BitSetImage(&p);
        spB2ArrayImage(&p, sizeof(b2ContactSim));
        spB2ArrayImage(&p, sizeof(b2JointSim));
    }
    if (!p.reader.ok || p.reader.cursor != p.reader.size) return false;
    spB2ImageRequirements candidate = {size, p.allocations};
    *requirements = candidate;
    return true;
}
