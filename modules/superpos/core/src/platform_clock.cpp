#include "superpos/platform_clock.hpp"
#include "clock_sampling.hpp"
#include <limits>
#include <utility>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <time.h>
#endif

namespace superpos {
namespace {
constexpr std::uint64_t maximum = UINT64_MAX;
Result<std::uint64_t> sum(std::uint64_t a, std::uint64_t b) noexcept {
    if (b > maximum - a) return fail(Error::CounterExhausted);
    return a + b;
}
Result<std::uint64_t> rate_uncertainty(std::uint64_t elapsed, std::uint32_t ppm) noexcept {
    constexpr std::uint64_t denominator = 1000000;
    const auto quotient = elapsed / denominator;
    const auto remainder = elapsed % denominator;
    if (ppm && quotient > maximum / ppm) return fail(Error::CounterExhausted);
    const auto residual = (remainder * ppm + denominator - 1) / denominator;
    return sum(quotient * ppm, residual);
}
[[maybe_unused]] Result<ClockSample> bracket(std::uint64_t before, std::uint64_t active, std::uint64_t after) noexcept {
    if (after < before) return fail(Error::InvalidArgument);
    const auto width = after - before;
    auto uncertainty = sum(width / 2 + width % 2, 1); // rounded microsecond conversions
    if (!uncertainty) return fail(uncertainty.error());
    return ClockSample{before + width / 2, active, *uncertainty};
}
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
Result<std::uint64_t> linux_time(clockid_t kind) noexcept {
    timespec value{};
    if (clock_gettime(kind, &value) != 0) return fail(Error::Io);
    if (value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000) return fail(Error::InvalidArgument);
    auto seconds = static_cast<std::uint64_t>(value.tv_sec);
    if (seconds > maximum / 1000000) return fail(Error::CounterExhausted);
    return sum(seconds * 1000000, static_cast<std::uint64_t>(value.tv_nsec) / 1000);
}
#elif defined(__APPLE__)
Result<std::uint64_t> apple_time(std::uint64_t ticks, std::uint32_t numerator, std::uint32_t denominator) noexcept {
    if (!denominator || !numerator) return fail(Error::Unsupported);
    const auto quotient = ticks / denominator;
    const auto remainder = ticks % denominator;
    if (quotient > maximum / numerator) return fail(Error::CounterExhausted);
    auto nanos = sum(quotient * numerator, remainder * numerator / denominator);
    if (!nanos) return fail(nanos.error());
    return *nanos / 1000;
}
#endif
}

Result<PlatformClock> PlatformClock::create() noexcept {
    PlatformClock result;
#if defined(_WIN32)
    // Resolve the documented API-set contract rather than its current backing
    // DLL; the public Kernel32 documentation is not a GetProcAddress export map.
    auto module = LoadLibraryExW(L"api-ms-win-core-realtime-l1-1-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return fail(Error::Unsupported);
    result.module_ = module;
    result.continuous_function_ = reinterpret_cast<void*>(GetProcAddress(module, "QueryInterruptTimePrecise"));
    result.active_function_ = reinterpret_cast<void*>(GetProcAddress(module, "QueryUnbiasedInterruptTimePrecise"));
    if (!result.continuous_function_ || !result.active_function_) return fail(Error::Unsupported);
    result.kind_ = PlatformClockKind::WindowsInterrupt;
#elif defined(__APPLE__)
    if (__builtin_available(macOS 10.12, iOS 10.0, watchOS 3.0, tvOS 10.0, *)) {
        mach_timebase_info_data_t info{};
        if (mach_timebase_info(&info) != KERN_SUCCESS || !info.denom || !info.numer) return fail(Error::Unsupported);
        result.numerator_ = info.numer;
        result.denominator_ = info.denom;
        result.kind_ = PlatformClockKind::AppleContinuous;
    } else { return fail(Error::Unsupported); }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && defined(CLOCK_BOOTTIME)
    if (!linux_time(CLOCK_BOOTTIME) || !linux_time(CLOCK_MONOTONIC)) return fail(Error::Unsupported);
    result.kind_ = PlatformClockKind::LinuxBootTime;
#else
    // Browser and other native platforms require a qualified trusted-host clock.
    return fail(Error::Unsupported);
#endif
    return result;
}

PlatformClock::PlatformClock(PlatformClock&& other) noexcept {
    *this = std::move(other);
}
PlatformClock& PlatformClock::operator=(PlatformClock&& other) noexcept {
    if (this == &other) return *this;
#if defined(_WIN32)
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
#endif
    kind_ = other.kind_; numerator_ = other.numerator_; denominator_ = other.denominator_;
    continuous_function_ = std::exchange(other.continuous_function_, nullptr);
    active_function_ = std::exchange(other.active_function_, nullptr);
    module_ = std::exchange(other.module_, nullptr);
    return *this;
}
PlatformClock::~PlatformClock() {
#if defined(_WIN32)
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
#endif
}

Result<ClockSample> PlatformClock::sample() noexcept {
    return detail::bounded_clock_sample([&]() noexcept -> Result<ClockSample> {
#if defined(_WIN32)
    if (!continuous_function_ || !active_function_) return fail(Error::NotReady);
    using Function = void (WINAPI*)(PULONGLONG);
    auto continuous = reinterpret_cast<Function>(continuous_function_);
    auto active = reinterpret_cast<Function>(active_function_);
    ULONGLONG before{}, active_time{}, after{};
    continuous(&before); active(&active_time); continuous(&after);
    return bracket(before / 10, active_time / 10, after / 10);
#elif defined(__APPLE__)
    if (__builtin_available(macOS 10.12, iOS 10.0, watchOS 3.0, tvOS 10.0, *)) {
        auto before = apple_time(mach_continuous_time(), numerator_, denominator_);
        auto active = apple_time(mach_absolute_time(), numerator_, denominator_);
        auto after = apple_time(mach_continuous_time(), numerator_, denominator_);
        if (!before || !active || !after) return fail(Error::CounterExhausted);
        return bracket(*before, *active, *after);
    } else { return fail(Error::Unsupported); }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__) && defined(CLOCK_BOOTTIME)
    auto before = linux_time(CLOCK_BOOTTIME);
    auto active = linux_time(CLOCK_MONOTONIC);
    auto after = linux_time(CLOCK_BOOTTIME);
    if (!before) return fail(before.error());
    if (!active) return fail(active.error());
    if (!after) return fail(after.error());
    return bracket(*before, *active, *after);
#else
    return fail(Error::Unsupported);
#endif
    });
}

ContinuityGuard::ContinuityGuard(ContinuityConfig config) noexcept : config_(config), owner_(std::this_thread::get_id()) {
    if (config.relative_rate_ppm > 1000000 || !config.maximum_uncertainty_us ||
        !config.maximum_sampling_uncertainty_us || !config.maximum_observation_gap_us ||
        config.maximum_sampling_uncertainty_us > config.maximum_uncertainty_us) {
        state_ = ContinuityState::Failed;
        reason_ = ContinuityReason::Uncertainty;
    }
}
Status ContinuityGuard::check_owner() const noexcept {
    if (owner_ != std::this_thread::get_id()) return fail(Error::PermissionDenied);
    return {};
}
Status ContinuityGuard::revoke(ContinuityReason reason, Error error, bool terminal) noexcept {
    reason_ = reason;
    state_ = terminal || state_ == ContinuityState::Failed ? ContinuityState::Failed : ContinuityState::NeedsRevalidation;
    return fail(error);
}
Status ContinuityGuard::revalidate(ClockSample sample, std::uint64_t fresh_generation, std::uint64_t trusted_uncertainty) noexcept {
    auto owner = check_owner(); if (!owner) return owner;
    if (state_ == ContinuityState::Failed) return fail(Error::NotReady);
    if (!fresh_generation || fresh_generation <= generation_) return fail(Error::StaleGeneration);
    if (sample.sampling_uncertainty_us > config_.maximum_sampling_uncertainty_us ||
        trusted_uncertainty > config_.maximum_uncertainty_us) return revoke(ContinuityReason::Uncertainty, Error::NotReady);
    auto total = sum(trusted_uncertainty, sample.sampling_uncertainty_us);
    if (!total) return revoke(ContinuityReason::CounterExhausted, total.error(), true);
    if (*total > config_.maximum_uncertainty_us) return revoke(ContinuityReason::Uncertainty, Error::NotReady);
    origin_ = previous_ = sample;
    trusted_uncertainty_us_ = trusted_uncertainty;
    generation_ = fresh_generation;
    state_ = ContinuityState::Continuous; reason_ = ContinuityReason::None;
    return {};
}
Result<ClockObservation> ContinuityGuard::observe(ClockSample sample) noexcept {
    auto owner = check_owner(); if (!owner) return fail(owner.error());
    if (state_ != ContinuityState::Continuous) return fail(Error::NotReady);
    if (sample.continuous_us < previous_.continuous_us || sample.active_us < previous_.active_us) {
        static_cast<void>(revoke(ContinuityReason::BackwardClock, Error::NotReady)); return fail(Error::NotReady);
    }
    const auto elapsed = sample.continuous_us - previous_.continuous_us;
    const auto active_elapsed = sample.active_us - previous_.active_us;
    if (elapsed > config_.maximum_observation_gap_us) {
        static_cast<void>(revoke(ContinuityReason::ObservationGap, Error::NotReady)); return fail(Error::NotReady);
    }
    if (sample.sampling_uncertainty_us > config_.maximum_sampling_uncertainty_us) {
        static_cast<void>(revoke(ContinuityReason::Uncertainty, Error::NotReady)); return fail(Error::NotReady);
    }
    auto slack = sum(previous_.sampling_uncertainty_us, sample.sampling_uncertainty_us);
    if (slack) slack = sum(*slack, config_.suspend_discrepancy_us);
    const auto discrepancy = elapsed >= active_elapsed ? elapsed - active_elapsed : active_elapsed - elapsed;
    if (!slack) { static_cast<void>(revoke(ContinuityReason::CounterExhausted, slack.error(), true)); return fail(slack.error()); }
    if (discrepancy > *slack) { static_cast<void>(revoke(ContinuityReason::Suspend, Error::NotReady)); return fail(Error::NotReady); }
    // Repeated short discontinuities must not hide below a per-sample tolerance.
    const auto total_elapsed = sample.continuous_us - origin_.continuous_us;
    const auto total_active = sample.active_us - origin_.active_us;
    const auto total_discrepancy = total_elapsed >= total_active ? total_elapsed - total_active : total_active - total_elapsed;
    auto total_slack = sum(origin_.sampling_uncertainty_us, sample.sampling_uncertainty_us);
    if (total_slack) total_slack = sum(*total_slack, config_.suspend_discrepancy_us);
    if (!total_slack) { static_cast<void>(revoke(ContinuityReason::CounterExhausted, total_slack.error(), true)); return fail(total_slack.error()); }
    if (total_discrepancy > *total_slack) { static_cast<void>(revoke(ContinuityReason::Suspend, Error::NotReady)); return fail(Error::NotReady); }
    auto growth = rate_uncertainty(total_elapsed, config_.relative_rate_ppm);
    auto total = growth ? sum(trusted_uncertainty_us_, *growth) : Result<std::uint64_t>(fail(growth.error()));
    if (total) total = sum(*total, origin_.sampling_uncertainty_us);
    if (total) total = sum(*total, sample.sampling_uncertainty_us);
    if (!total) { static_cast<void>(revoke(ContinuityReason::CounterExhausted, total.error(), true)); return fail(total.error()); }
    if (*total > config_.maximum_uncertainty_us) { static_cast<void>(revoke(ContinuityReason::Uncertainty, Error::NotReady)); return fail(Error::NotReady); }
    previous_ = sample;
    return ClockObservation{sample.continuous_us, *total, generation_};
}
Result<ClockObservation> ContinuityGuard::observe(ClockSource& source) noexcept {
    auto owner = check_owner(); if (!owner) return fail(owner.error());
    if (state_ == ContinuityState::Failed) return fail(Error::NotReady);
    auto sample = source.sample();
    if (!sample) {
        static_cast<void>(revoke(sample.error() == Error::CounterExhausted ? ContinuityReason::CounterExhausted : ContinuityReason::SourceFailure,
               sample.error(), sample.error() == Error::CounterExhausted));
        return fail(sample.error());
    }
    return observe(*sample);
}
Status ContinuityGuard::lifecycle_discontinuity() noexcept {
    auto owner = check_owner(); if (!owner) return owner;
    if (state_ == ContinuityState::Failed) return fail(Error::NotReady);
    state_ = ContinuityState::NeedsRevalidation; reason_ = ContinuityReason::Lifecycle;
    return {};
}
}
