// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/platform_clock.hpp"

namespace superpos::detail {
// A preemption between native clock reads can make one bracket too wide.
// Retry measurement only: no continuity state, origin, or deadline is changed.
template<class Measure>
Result<ClockSample> bounded_clock_sample(Measure&& measure) noexcept {
    ClockSample previous{};
    for (unsigned attempt = 0; attempt != 3; ++attempt) {
        auto sample = measure();
        if (!sample) return fail(sample.error());
        if (attempt && (sample->continuous_us < previous.continuous_us ||
                        sample->active_us < previous.active_us))
            return fail(Error::InvalidArgument);
        if (sample->sampling_uncertainty_us <= 1000 || attempt == 2) return sample;
        previous = *sample;
    }
    return fail(Error::NotReady); // unreachable, keeps all compilers explicit
}
}
