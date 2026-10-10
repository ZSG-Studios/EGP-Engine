#pragma once
#include "superpos/journal_executor.hpp"
#include <thread>
namespace superpos::service {
enum class BootstrapPhase : std::uint8_t { Dormant,Reading,Committing,Unknown,Binding,Ready,Failed,Retired };
// One startup per ordering domain, not per match. The supervisor owns a fresh
// serial-worker connection and joins it before destroying this borrowed queue.
// That queue must not move or be recreated during this object's lifetime.
// Only a successful FULL/WAL boot commit followed by exact connection binding
// exposes identity. No observation or caller-injected receipt can do so. Ready
// records a completed exact bind, not perpetual currency: protected SQL writes
// independently fence a later competing boot. Nonce provider is trusted.
class CoordinatorBootstrap {
public:
    CoordinatorBootstrap(JournalExecutor&,LeaseNonceProvider&) noexcept;
    CoordinatorBootstrap(const CoordinatorBootstrap&)=delete;
    CoordinatorBootstrap& operator=(const CoordinatorBootstrap&)=delete;
    Status start() noexcept;
    Status poll() noexcept;
    Status retry_unknown() noexcept;
    Result<CoordinatorBootIdentity> identity() const noexcept;
    Result<BootstrapPhase> phase() const noexcept;
private:
    Status own() const noexcept;
    Status stop(Error) noexcept;
    Status submit_commit() noexcept;
    Status submit_bind() noexcept;
    JournalExecutor* queue_;
    LeaseNonceProvider* nonce_;
    const std::thread::id owner_;
    BootstrapPhase phase_{BootstrapPhase::Dormant};
    std::optional<JournalJobTicket> ticket_{};
    std::optional<std::uint64_t> expected_{};
    std::array<std::byte,32> fresh_{};
    std::optional<CoordinatorBootIdentity> identity_{};
    bool entering_{};
    bool uncertain_{};
};
}
