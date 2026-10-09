// Copyright (c) 2026 Superpos contributors.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <ratio>
#include <type_traits>

namespace superpos::rtc_timer {
// A single fixed pending timeout. No queued closure or owning capture survives
// merely because the provider has armed a timer. The host must poll this slot.
// This experimental profile explicitly requires a signed, nanosecond native
// steady clock and rejects negative samples. It is not a lease clock contract.
class DeadlineSlot final {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    enum class Error : std::uint8_t { InvalidSample, InvalidDelay, Overflow };
    using Status = std::expected<void, Error>;
    static constexpr auto maximum_delay = std::chrono::seconds(30);
private:
    using Rep = Clock::duration::rep;
    static_assert(std::is_same_v<Rep, std::int64_t>);
    static_assert(std::ratio_equal_v<Clock::period, std::nano>);
    static_assert(std::atomic<Rep>::is_always_lock_free);
    // Private representation marker, outside the accepted clock sample domain.
    // Public protocol counters do not use this marker.
    static constexpr Rep disarmed = std::numeric_limits<Rep>::min();
    std::atomic<Rep> due_{disarmed};
public:
    DeadlineSlot() noexcept = default;
    DeadlineSlot(const DeadlineSlot&) = delete;
    DeadlineSlot& operator=(const DeadlineSlot&) = delete;
    Status arm(Time now, std::chrono::microseconds delay) noexcept {
        const Rep sample = now.time_since_epoch().count();
        if (sample < 0) return std::unexpected(Error::InvalidSample);
        if (delay < std::chrono::microseconds::zero() || delay > maximum_delay)
            return std::unexpected(Error::InvalidDelay);
        // The 30 second cap makes duration conversion itself exact and safe.
        const Rep delta = std::chrono::duration_cast<Clock::duration>(delay).count();
        if (sample > std::numeric_limits<Rep>::max() - delta)
            return std::unexpected(Error::Overflow);
        due_.store(sample + delta, std::memory_order_release);
        return {};
    }
    void cancel() noexcept { due_.store(disarmed, std::memory_order_release); }
    std::optional<Time> deadline() const noexcept {
        const Rep value = due_.load(std::memory_order_acquire);
        if (value == disarmed) return std::nullopt;
        return Time(Clock::duration(value));
    }
    // Exactly one consumer wins for the observed deadline. A concurrent arm
    // with a different value is retained. If it repeats the same expired value,
    // the admitted receive pass observes the current TLS state and rearms it.
    // Caller restores progress (dirty token) when task admission is rejected.
    bool consume_due(Time now) noexcept {
        const Rep sample = now.time_since_epoch().count();
        if (sample < 0) return false;
        Rep expected = due_.load(std::memory_order_acquire);
        if (expected == disarmed || expected > sample) return false;
        return due_.compare_exchange_strong(expected, disarmed, std::memory_order_acq_rel);
    }
};
}
