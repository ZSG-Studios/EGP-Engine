#pragma once
#include "superpos/journal_executor.hpp"
namespace superpos::service { class AuthorityCoordinator; }
namespace superpos::service::detail {
// Internal coordinator timing implementation. Every boot/receipt/eligibility call is
// a trusted service boundary, not an authenticated network API.
class CoordinatorTiming {
    friend class ::superpos::service::AuthorityCoordinator;
private:
    CoordinatorTiming(ClockSource&,ContinuityGuard&) noexcept;
    CoordinatorTiming(const CoordinatorTiming&)=delete;
    CoordinatorTiming& operator=(const CoordinatorTiming&)=delete;
    Status bind_committed_boot(CoordinatorBootIdentity) noexcept;
    Status restart_wait_after_revalidation() noexcept;
    Status prepare(DurableAuthorityProposal) noexcept;
    Status admitted(JournalJobTicket) noexcept;
    Status admission_rejected() noexcept;
    Status withdraw() noexcept;
    Result<DurableAuthorityProposal> exact_retry() noexcept;
    Status completed(JournalJobTicket,const JournalJobCompletion&) noexcept;
    // Caller must revalidate authenticated eligibility before this call and
    // hand off immediately, on this owner thread. No asynchronous cached permit.
    // Hold is recorded before return, including a subsequent failed send.
    Result<AuthorityGrant> potential_send() noexcept;
private:
    Result<ClockObservation> observe() noexcept;
    Status wait_from(const ClockObservation&) noexcept;
    Status unknown_outcome() noexcept;
    Status committed(const DurableAuthorityReceipt&) noexcept;
    static constexpr std::uint64_t ttl=2000000,guard=200000,ppm=1000;
    ClockSource* source_;
    ContinuityGuard* clock_;
    std::thread::id owner_;
    std::optional<CoordinatorBootIdentity> boot_{};
    std::optional<DurableAuthorityProposal> pending_{};
    std::optional<DurableAuthorityReceipt> committed_{};
    std::optional<JournalJobTicket> accepted_{};
    ClockObservation prepared_{};
    std::uint64_t wait_until_{},hold_until_{},issue_until_{},request_until_{},renew_until_{},generation_{};
    bool suspended_{true},unknown_{},issuable_{},renewable_{},terminal_{};
};
}
