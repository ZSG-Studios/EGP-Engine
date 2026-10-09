#pragma once
#include "superpos/service/detail/coordinator_timing.hpp"
#include "superpos/capability.hpp"
namespace superpos::service {
enum class GrantIntent : std::uint8_t { Takeover,Renew };
struct AuthorityRequest {
    PeerId peer{};
    AuthorityKind kind{};
    GrantIntent intent{};
    LeaseScope admission_instance{};
    std::uint64_t wire_session{},connection_epoch{},membership_generation{};
    LeaseRequest lease{};
};
struct MatchAuthorityContext {
    std::uint64_t match{};
    LeaseScope match_instance{},ordering_instance{},eligibility_instance{};
    CoordinatorBootIdentity boot{};
};
struct AuthorityEligibility {
    MatchAuthorityContext context{};
    AuthorityRequest request{};
    std::uint64_t provider_generation{},local_continuity_generation{};
    Fingerprint membership_set{},schemas{},simulation{};
    std::uint64_t complete_state_bytes{},reserved_capacity_bytes{};
    bool hidden_state_authorized{},all_members_routable{},complete_codec{},ready{};
};
// Trusted authenticated admission/policy domain. Implementations must verify all
// current members, not just the requesting/connected subset. Raw network fields,
// Session::ready and capability declarations do not themselves prove eligibility.
// Authenticated admission and WSS are supplied by the service control owner.
class AuthorityEligibilityProvider {
public:
    virtual ~AuthorityEligibilityProvider()=default;
    virtual Result<AuthorityEligibility> admit(const MatchAuthorityContext&,
        const AuthorityRequest&,std::uint64_t local_continuity_generation) noexcept=0;
    virtual Status revalidate(const AuthorityEligibility&) noexcept=0;
};
class AuthorityGrantPublisher {
public:
    virtual ~AuthorityGrantPublisher()=default;
    // Nonblocking, copies fixed native metadata if accepting it. Failure is not
    // proof that no bytes escaped. The one-use timing permit is always consumed.
    virtual Status send(const AuthorityGrant&,const AuthorityEligibility&) noexcept=0;
};
// Opaque private hidden-state read scope with no public constructor or decoder.
// Only AuthorityCoordinator mints it, from its exact committed current grant
// after fresh eligibility revalidation under one continuity generation. It
// grants no execution permit, lease restoration, credential or publication.
// Copies carry the same scope. The storage worker compares the exact stored
// boot, grant and fenced prefix on every use, so a coordinator restart, newer
// grant or advanced prefix invalidates it. It expires with the grant lease:
// expires_us is the coordinator's lease bound (grant preparation plus TTL) in
// the coordinator ClockSource domain, valid only under proof_generation. A
// renewal mints a successor capability for the renewed grant.
class RestoreReadCapability {
    friend class AuthorityCoordinator;
    CanonicalRestoreScope scope_{};
    std::uint64_t match_{},expires_us_{},proof_generation_{};
    RestoreReadCapability(const CanonicalRestoreScope& scope,std::uint64_t match,std::uint64_t expires,std::uint64_t generation) noexcept
        :scope_(scope),match_(match),expires_us_(expires),proof_generation_(generation){}
public:
    const CanonicalRestoreScope& scope() const noexcept {return scope_;}
    std::uint64_t match() const noexcept {return match_;}
    std::uint64_t expires_us() const noexcept {return expires_us_;}
    std::uint64_t proof_generation() const noexcept {return proof_generation_;}
    // Conservative: valid only strictly before expiry including uncertainty.
    bool current(const ClockObservation& now) const noexcept {
        return now.proof_generation==proof_generation_&&now.now_us<expires_us_&&now.uncertainty_us<expires_us_-now.now_us;
    }
};
enum class CoordinatorPhase : std::uint8_t { Unbound,Initializing,Ready,Pending,
    Unknown,Confirmed,Suspended,Retired };
// Immovable owner-thread coordinator. Borrowed queue is bound to one exact
// journal/match by trusted startup and cannot move or be recreated during this
// lifetime. Only this object consumes its retained ticket from that queue;
// callers cannot inject a receipt, completion, ticket or another executor.
// Worker lifetime remains owned by the service supervisor and must be joined.
class AuthorityCoordinator {
public:
    AuthorityCoordinator(JournalExecutor&,ClockSource&,ContinuityGuard&,
        AuthorityEligibilityProvider&,MatchAuthorityContext) noexcept;
    AuthorityCoordinator(const AuthorityCoordinator&)=delete;
    AuthorityCoordinator& operator=(const AuthorityCoordinator&)=delete;
    Status start() noexcept;
    Status poll() noexcept;
    Status request(AuthorityRequest) noexcept;
    Status retry_unknown() noexcept;
    Status publish(AuthorityGrantPublisher&) noexcept;
    Status resume_after_revalidation() noexcept;
    // Confirmed/Ready with no outstanding job and the eligibility binding of
    // the exact current grant, before its lease bound. Side-effect free:
    // refusal never suspends. Timeout once the grant lease has lapsed.
    Result<RestoreReadCapability> restore_read(CheckpointTicket,
        std::uint32_t maximum_records) noexcept;
    Result<CoordinatorPhase> phase() const noexcept;
private:
    enum class Work : std::uint8_t { Boot,Match,Prefix,Authority,Commit };
    Status own() const noexcept;
    Status submit_query(Work) noexcept;
    Status submit_commit(DurableAuthorityProposal) noexcept;
    Status validate(const AuthorityEligibility&,const AuthorityRequest&,
        std::uint64_t local_generation) const noexcept;
    Status suspend(Error) noexcept;
    JournalExecutor* queue_;
    ClockSource* source_;
    ContinuityGuard* clock_;
    AuthorityEligibilityProvider* eligibility_;
    const MatchAuthorityContext context_;
    detail::CoordinatorTiming timing_;
    const std::thread::id owner_;
    std::optional<JournalJobTicket> ticket_{};
    std::optional<DurableAuthorityReceipt> current_{};
    std::optional<AuthorityEligibility> binding_{};
    std::optional<DurableAuthorityProposal> proposal_{};
    JournalPrefix prefix_{};
    Work work_{Work::Boot};
    CoordinatorPhase phase_{CoordinatorPhase::Unbound};
    bool entering_{};
    bool boot_bound_{};
};
}
