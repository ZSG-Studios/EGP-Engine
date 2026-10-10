// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/coordinator.hpp"
#include "superpos/coordinator_bootstrap.hpp"
#include "superpos/storage_worker.hpp"
#include "superpos/service/control_ordered.hpp"
#include <string_view>

namespace superpos::service {
enum class SupervisorPhase : std::uint8_t {
    Opening, Bootstrapping, BootUnknown, Initializing, Ready, Pending,
    GrantUnknown, Confirmed, Suspended, Retired, Failed, Stopping, Stopped
};
struct SupervisorConfig {
    StorageWorkerConfig storage{};
    std::uint64_t match{};
    LeaseScope match_instance{}, ordering_instance{}, eligibility_instance{};
};
// Fixed copied projection of one exact internally owned completion. QueryAuthority
// is an observation, not a new committed/authenticated grant or delivery receipt.
struct SupervisorControlCompletion {
    JournalJobKind kind{};
    Error error{Error::None};
    JournalPrefix prefix{};
    JournalReceipt receipt{};
    std::optional<control::PolicyReceipt> policy{};
    std::optional<DurableAuthorityReceipt> authority{};
};
// Opaque ownership of one exact control credit. Its queue incarnation and full
// sequence survive a safe Supervisor move and never identify a replacement job.
class SupervisorControlTicket {
    std::uint64_t incarnation_{},sequence_{};
    std::uint32_t slot_{};
    explicit SupervisorControlTicket(JournalJobTicket value) noexcept
        :incarnation_(value.incarnation),sequence_(value.sequence),slot_(value.slot){}
    friend class OwnedCoordinatorSupervisor;
public:
    SupervisorControlTicket()=default;
    friend bool operator==(const SupervisorControlTicket&,const SupervisorControlTicket&)=default;
};

// Single selected-match service ownership. The allocator/fault hook must be
// thread-safe and all borrowed providers must outlive shutdown plus destruction.
// Providers are trusted, not an implementation of authenticated admission.
// All calls, moves and destruction belong to the creating thread. Reentry is Busy.
// Ready means initialized; the timing gate still enforces restart TTL + guard.
// Terminal queue counter exhaustion is Retired at startup, grant and diagnostic
// and private control admission/completion. It seals the worker and preserves Accepted/completed tickets until
// real join; shutdown is the only later phase transition and never reopens it.
class OwnedCoordinatorSupervisor {
    struct Impl;
    Impl* impl_{};
    explicit OwnedCoordinatorSupervisor(Impl*) noexcept;
    void destroy() noexcept;
public:
    static Result<OwnedCoordinatorSupervisor> create(Allocator&,
        std::string_view absolute_utf8_path, SupervisorConfig, LeaseNonceProvider&,
        ClockSource&, ContinuityGuard&, AuthorityEligibilityProvider&) noexcept;
    OwnedCoordinatorSupervisor(const OwnedCoordinatorSupervisor&)=delete;
    OwnedCoordinatorSupervisor& operator=(const OwnedCoordinatorSupervisor&)=delete;
    OwnedCoordinatorSupervisor(OwnedCoordinatorSupervisor&&) noexcept;
    OwnedCoordinatorSupervisor& operator=(OwnedCoordinatorSupervisor&&) noexcept;
    ~OwnedCoordinatorSupervisor();
    Status poll() noexcept;
    Status request(AuthorityRequest) noexcept;
    Status retry_unknown() noexcept;
    Status resume_after_revalidation() noexcept;
    Status publish(AuthorityGrantPublisher&) noexcept;
    Result<SupervisorPhase> phase() const noexcept;
    Result<Error> failure() const noexcept;
    Result<CoordinatorBootIdentity> identity() const noexcept;

    // One fixed internally ticketed diagnostic read, including completed credit.
    // No queue pointer, ticket or caller completion crosses this interface.
    Status request_prefix() noexcept;
    Result<JournalPrefix> consume_prefix() noexcept;

    // Trusted native management only; these methods are not wire Route commands.
    // Accepted submission is not storage success. Issuer/lease/policy metadata is
    // copied into the owned queue; the store independently validates it at execution.
    Status bind_control_issuer(const control::IssuerIdentity&) noexcept;
    Status commit_control_policy(const control::PolicyChange&) noexcept;
    Status install_execution_lease(const control::ExecutionLease&) noexcept;
    Status request_control_authority() noexcept;
    // Trusted owner maintenance through the same bounded, token-owned credit.
    // Completion only reports WAL checkpoint success; it resolves no operation.
    Status checkpoint_control() noexcept;
    // Network dispatch requires an opaque native permit. The body is copied by
    // the queue before Accepted and is bounded to 4000 bytes, including empty.
    Status read_control(const control::ExecutionPermit&) noexcept;
    Status append_control(const control::ExecutionPermit&,std::uint64_t append_id,
        Tick,std::span<const std::byte>) noexcept;
    // One shared fixed control credit, including completed-but-unconsumed work.
    // Poll copies the projection without releasing caller-visible credit.
    Result<SupervisorControlCompletion> poll_control() noexcept;
    Status consume_control() noexcept;
    // Detach without canceling Accepted work. A pending abandoned ticket remains
    // owned until poll/poll_control or real joined shutdown drains it. New control
    // submissions remain Busy until then; no callback/reference to Route is kept.
    Status abandon_control() noexcept;

    // Claim immediately after one's successful submission. No callback or
    // allocation occurs. Claiming seals the bare projection APIs; another owner
    // cannot consume, abandon, or claim this credit. The token APIs reject stale
    // tickets before capturing/releasing any replacement completion.
    Result<SupervisorControlTicket> claim_control() noexcept;
    Result<SupervisorControlCompletion> poll_control(SupervisorControlTicket) noexcept;
    Status consume_control(SupervisorControlTicket) noexcept;
    Status abandon_control(SupervisorControlTicket) noexcept;

    // Irreversible issuance/admission closure. Busy join keeps every resource.
    // Stopped proves join; Failed does not. Destruction has a blocking owner-only
    // join fallback; native OS/storage stalls cannot be force-canceled safely.
    Status shutdown() noexcept;
    Status poll_shutdown(std::uint32_t wait_ms=0) noexcept;
};
}
