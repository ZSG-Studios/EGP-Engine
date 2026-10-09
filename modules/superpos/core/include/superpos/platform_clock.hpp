#pragma once
#include "superpos/result.hpp"
#include <cstdint>
#include <thread>

namespace superpos {
// All values are monotonic, in-process observations. They prove no continuity
// across reboot and establish no remote clock offset or network uncertainty.
struct ClockSample {
    std::uint64_t continuous_us{};
    std::uint64_t active_us{};
    std::uint64_t sampling_uncertainty_us{};
};
class ClockSource {
public:
    virtual ~ClockSource() = default;
    virtual Result<ClockSample> sample() noexcept = 0;
};
enum class PlatformClockKind { WindowsInterrupt, LinuxBootTime, AppleContinuous };
class PlatformClock final : public ClockSource {
public:
    PlatformClock(const PlatformClock&) = delete;
    PlatformClock& operator=(const PlatformClock&) = delete;
    PlatformClock(PlatformClock&&) noexcept;
    PlatformClock& operator=(PlatformClock&&) noexcept;
    ~PlatformClock() override;
    static Result<PlatformClock> create() noexcept;
    Result<ClockSample> sample() noexcept override;
    PlatformClockKind kind() const noexcept { return kind_; }
private:
    PlatformClock() noexcept = default;
    PlatformClockKind kind_{PlatformClockKind::WindowsInterrupt};
    std::uint32_t numerator_{1}, denominator_{1};
    void* continuous_function_{};
    void* active_function_{};
    void* module_{};
};

struct ContinuityConfig {
    std::uint32_t relative_rate_ppm{1000};
    std::uint64_t maximum_uncertainty_us{100000};
    std::uint64_t maximum_sampling_uncertainty_us{1000};
    std::uint64_t maximum_observation_gap_us{500000};
    std::uint64_t suspend_discrepancy_us{1000};
};
enum class ContinuityState { Unvalidated, Continuous, NeedsRevalidation, Failed };
enum class ContinuityReason {
    None, Lifecycle, Suspend, ObservationGap, BackwardClock, Uncertainty,
    SourceFailure, CounterExhausted
};
struct ClockObservation {
    std::uint64_t now_us{};
    std::uint64_t uncertainty_us{};
    std::uint64_t proof_generation{};
};
// One owner thread; allocation free. revalidate is a trusted-host boundary:
// callers must first re-establish routing/admission/authority and supply its
// calibrated REMOTE uncertainty. Sampling precision alone is not that proof.
class ContinuityGuard {
public:
    explicit ContinuityGuard(ContinuityConfig config = {}) noexcept;
    ContinuityGuard(const ContinuityGuard&) = delete;
    ContinuityGuard& operator=(const ContinuityGuard&) = delete;
    ContinuityGuard(ContinuityGuard&&) = delete;
    ContinuityGuard& operator=(ContinuityGuard&&) = delete;
    Status revalidate(ClockSample, std::uint64_t fresh_proof_generation,
                      std::uint64_t trusted_remote_uncertainty_us) noexcept;
    Result<ClockObservation> observe(ClockSample) noexcept;
    Result<ClockObservation> observe(ClockSource&) noexcept;
    Status lifecycle_discontinuity() noexcept;
    Result<ContinuityState> state() const noexcept {
        if (owner_ != std::this_thread::get_id()) return fail(Error::PermissionDenied);
        return state_;
    }
    Result<ContinuityReason> reason() const noexcept {
        if (owner_ != std::this_thread::get_id()) return fail(Error::PermissionDenied);
        return reason_;
    }
    bool ready() const noexcept { return owner_ == std::this_thread::get_id() && state_ == ContinuityState::Continuous; }
private:
    Status check_owner() const noexcept;
    Status revoke(ContinuityReason, Error, bool terminal = false) noexcept;
    ContinuityConfig config_{};
    std::thread::id owner_{};
    ContinuityState state_{ContinuityState::Unvalidated};
    ContinuityReason reason_{ContinuityReason::None};
    ClockSample origin_{}, previous_{};
    std::uint64_t generation_{}, trusted_uncertainty_us_{};
};
}
