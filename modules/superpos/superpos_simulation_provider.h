// SPDX-License-Identifier: MIT
// Native engine-module interface. This deliberately exposes no C++23 core type.
#pragma once
#include "core/object/ref_counted.h"
#include <cstdint>

struct SuperposSimulationDescriptor {
    enum : uint32_t { FORMAT_V1 = 1, DETERMINISTIC_REPLAY = 1, INTERPOLATION = 2 };
    uint32_t format = FORMAT_V1;
    uint32_t state_bytes = 0;
    uint32_t input_bytes = 0;
    uint32_t capabilities = 0;
    // Integer ordinal ticks: each accepted input advances by exactly one tick.
    uint32_t tick_contract = 1;
    uint8_t codec_sha256[32] = {};
    uint8_t rules_sha256[32] = {};
    uint8_t qualification_sha256[32] = {};
};

// Trusted native providers implement pure bounded canonical byte transforms.
// They never retain supplied buffers, mutate live scene/solver state, emit
// external effects, execute a script callback or allocate during replay.
// These declarations identify the application-qualified exact native codec;
// merely supplying a digest does not qualify an implementation.
class SuperposSimulationProvider : public RefCounted {
    GDCLASS(SuperposSimulationProvider, RefCounted);
protected:
    static void _bind_methods() {}
public:
    virtual SuperposSimulationDescriptor descriptor() const noexcept = 0;
    virtual Error validate_state(const uint8_t *p_bytes, uint32_t p_size) const noexcept = 0;
    virtual Error validate_input(const uint8_t *p_bytes, uint32_t p_size) const noexcept = 0;
    virtual Error step(uint64_t p_tick, const uint8_t *p_state, uint32_t p_state_size,
        const uint8_t *p_input, uint32_t p_input_size, uint8_t *r_output, uint32_t p_output_size) noexcept = 0;
    virtual Error interpolate(const uint8_t *, uint32_t, const uint8_t *, uint32_t,
        double, uint8_t *, uint32_t) noexcept { return ERR_UNAVAILABLE; }
};
