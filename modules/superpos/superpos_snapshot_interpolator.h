// SPDX-License-Identifier: MIT
#pragma once
// Public C++17 engine/SDK declaration. The bounded per-entity rings are core
// SnapshotHistory instances kept in the private C++23 implementation.
#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

// Presentation-only interpolation of authoritative poses. It never changes
// canonical, predicted or physics state. Ticks and entity identities keep all
// 64 bits; the render clock is an integer tick plus a fraction in [0, 1).
class SuperposSnapshotInterpolator : public RefCounted {
    GDCLASS(SuperposSnapshotInterpolator, RefCounted);
    struct State;
    State *state = nullptr;
    Thread::ID owner_thread = Thread::get_caller_id();
    Error _preflight() const;
protected:
    static void _bind_methods();
public:
    // Replaces any previous configuration and clears every track.
    Error configure(const Dictionary &p_configuration = Dictionary());
    Error submit(uint64_t p_entity, uint64_t p_tick, const Transform3D &p_pose, const Vector3 &p_velocity = Vector3(),
        const Vector3 &p_angular_velocity = Vector3(), uint64_t p_discontinuity_epoch = 0);
    Error advance(double p_delta);
    Dictionary read_clock() const;
    // Samples at the render clock and decays the presentation correction.
    Dictionary sample(uint64_t p_entity, double p_delta = 0.0);
    // Pure query at an explicit time: no clock, counter or correction change.
    Dictionary sample_at(uint64_t p_entity, uint64_t p_tick, double p_fraction = 0.0) const;
    Dictionary read_entity(uint64_t p_entity) const;
    Dictionary read_statistics() const;
    Error remove(uint64_t p_entity);
    Error clear();
    ~SuperposSnapshotInterpolator();
};
