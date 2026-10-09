// SPDX-License-Identifier: MIT
#include "superpos/service/control_ordered.hpp"
#include "superpos/service/admission_types.hpp"
#include <algorithm>
#include <limits>

namespace superpos::service::control {
namespace {
struct Busy {bool& b;explicit Busy(bool& value) noexcept:b(value){b=true;}~Busy(){b=false;}};
bool nonzero(const LeaseScope& scope) noexcept {
    return std::any_of(scope.begin(),scope.end(),[](std::byte b){return b!=std::byte{};});
}
}
bool same_grant(const AuthorityGrant& a,const AuthorityGrant& b) noexcept {
    return a.request==b.request&&a.coordinator_term==b.coordinator_term&&a.grant_sequence==b.grant_sequence&&
        a.membership_generation==b.membership_generation&&a.epoch==b.epoch&&a.owner==b.owner&&a.kind==b.kind&&
        a.ttl_us==b.ttl_us&&a.scope==b.scope&&a.proof_generation==b.proof_generation;
}
bool same_execution_lease(const ExecutionLeaseFields& a,const ExecutionLeaseFields& b) noexcept {
    return a.issuer==b.issuer&&same_grant(a.grant,b.grant)&&a.anchor.continuous_us==b.anchor.continuous_us&&
        a.anchor.active_us==b.anchor.active_us&&a.anchor.sampling_uncertainty_us==b.anchor.sampling_uncertainty_us&&
        a.deadline_us==b.deadline_us;
}
PermitIssuer::PermitIssuer(AuthorityLease& lease,PlatformClock& clock) noexcept:lease_(lease),clock_(clock){}
Status PermitIssuer::check() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(busy_)return fail(Error::Busy);
    return {};
}
Status PermitIssuer::initialize(CoordinatorBootIdentity boot,LeaseNonceProvider& random) noexcept {
    if(auto r=check();!r)return r;Busy guard(busy_);
    if(identity_)return fail(Error::InvalidArgument);
    if(!nonzero(boot.nonce))return fail(Error::InvalidArgument);
    if(lease_.source_!=&clock_)return fail(Error::InvalidArgument);
    if(auto r=lease_.owner();!r)return r;
    LeaseScope scope{};if(auto r=random.fresh(scope);!r)return r;
    if(!nonzero(scope))return fail(Error::AuthenticationFailed);
    identity_.emplace(IssuerIdentity(IssuerIdentityFields{boot,scope}));return {};
}
Result<IssuerIdentity> PermitIssuer::identity() const noexcept {
    if(auto r=check();!r)return fail(r.error());if(!identity_)return fail(Error::NotReady);return *identity_;
}
Status PermitIssuer::matches_clock(const ClockSource& source) const noexcept {
    if(auto r=check();!r)return r;if(!identity_)return fail(Error::NotReady);
    return &source==&clock_&&lease_.source_==&clock_?Status{}:Status(fail(Error::InvalidArgument));
}
Result<LeasePosition> PermitIssuer::current(const AuthorityGrant& grant) noexcept {
    if(lease_.source_!=&clock_)return fail(Error::InvalidArgument);
    auto p=lease_.authorize(grant.owner,grant.kind,grant.epoch,grant.membership_generation);
    if(!p)return fail(p.error());
    if(!same_grant(lease_.grant_,grant)||p->coordinator_term!=grant.coordinator_term)return fail(Error::StaleGeneration);
    return p;
}
Result<ExecutionLease> PermitIssuer::execution_lease(const DurableAuthorityReceipt& receipt) noexcept {
    if(auto r=check();!r)return fail(r.error());Busy guard(busy_);if(!identity_)return fail(Error::NotReady);
    const auto metadata=receipt;
    if(metadata.boot!=identity_->fields().boot)return fail(Error::StaleEpoch);
    auto anchor=clock_.sample();if(!anchor)return fail(anchor.error());
    if(anchor->sampling_uncertainty_us>1000)return fail(Error::RecoveryUnavailable);
    auto position=current(metadata.proposal.grant);if(!position)return fail(position.error());
    // Anchor precedes authorize; its remaining time is a conservative upper
    // admission boundary even if either call is delayed. Charge native sampling
    // uncertainty again; remote/rate uncertainty was already charged by lease.
    if(position->remaining_us<=anchor->sampling_uncertainty_us)return fail(Error::Timeout);
    const auto remaining=position->remaining_us-anchor->sampling_uncertainty_us;
    if(remaining>UINT64_MAX-anchor->continuous_us)return fail(Error::CounterExhausted);
    return ExecutionLease({identity_->fields(),metadata.proposal.grant,*anchor,anchor->continuous_us+remaining});
}
Result<ExecutionPermit> PermitIssuer::issue(const admission::Grant& input,const PolicyReceipt& input_policy,
    const ExecutionLease& execution,Operation operation,std::uint64_t admission_deadline) noexcept {
    if(auto r=check();!r)return fail(r.error());Busy guard(busy_);if(!identity_)return fail(Error::NotReady);
    const auto admission=input;const auto policy=input_policy;const auto fields=execution.fields();
    if(fields.issuer!=identity_->fields()||policy.change.boot!=fields.issuer.boot)return fail(Error::StaleEpoch);
    const auto& p=policy.change.policy;const auto& g=fields.grant;
    const auto permission=operation==Operation::ReadAuthority?read_authority_permission:
        operation==Operation::AppendState?append_state_permission:0;
    if(!permission)return fail(Error::InvalidArgument);
    if(!policy.current||p.revoked||!p.match||!p.session||!p.principal||p.match!=admission.match||p.session!=admission.session||
        p.principal!=admission.actor||p.principal_epoch!=admission.actor_epoch||p.authority_epoch!=admission.authority_epoch||
        p.authority_epoch!=g.epoch||p.authority_owner!=g.owner||p.authority_kind!=g.kind||
        p.membership_generation!=g.membership_generation||(p.permissions&~admission::known_permissions)||
        !(p.permissions&permission)||!(admission.permissions&permission)||!admission.connection_incarnation)return fail(Error::PermissionDenied);
    auto anchor=clock_.sample();if(!anchor)return fail(anchor.error());
    auto position=current(g);if(!position)return fail(position.error());
    if(anchor->sampling_uncertainty_us>1000||position->remaining_us<=anchor->sampling_uncertainty_us)return fail(Error::Timeout);
    auto remaining=position->remaining_us-anchor->sampling_uncertainty_us;
    if(remaining>UINT64_MAX-anchor->continuous_us)return fail(Error::CounterExhausted);
    const auto deadline=std::min({fields.deadline_us,admission_deadline,anchor->continuous_us+remaining});
    if(anchor->continuous_us>=deadline||anchor->sampling_uncertainty_us>=deadline-anchor->continuous_us)return fail(Error::Timeout);
    return ExecutionPermit({fields,p,policy.revision,admission.connection_incarnation,permission,deadline});
}
}
