#include "superpos/lease.hpp"
#include <limits>
namespace superpos {
Result<AuthorityLease> AuthorityLease::create(ClockSource& source,ContinuityGuard& continuity,LeaseNonceProvider& provider,LeaseConfig config) noexcept {
    if(!config.maximum_ttl_us||config.maximum_ttl_us>10000000||config.relative_rate_ppm>1000000)return fail(Error::InvalidArgument);
    LeaseScope scope{}; auto generated=provider.fresh(scope); if(!generated)return fail(generated.error());
    bool nonzero=false; for(auto byte:scope)nonzero=nonzero||byte!=std::byte{}; if(!nonzero)return fail(Error::InvalidArgument);
    return Result<AuthorityLease>(std::in_place,source,continuity,config,ValidatedScope(scope));
}
AuthorityLease::AuthorityLease(ClockSource& source,ContinuityGuard& continuity,LeaseConfig config,ValidatedScope scope) noexcept
    :source_(&source),continuity_(&continuity),config_(config),scope_(scope.scope_),owner_(std::this_thread::get_id()) {}
Status AuthorityLease::owner() const noexcept {return owner_==std::this_thread::get_id()?Status{}:Status(fail(Error::PermissionDenied));}
Status AuthorityLease::invalidate(bool terminal) noexcept {
    if(have_grant_&&(!have_expired_epoch_||grant_.epoch>expired_epoch_)){expired_epoch_=grant_.epoch;have_expired_epoch_=true;}
    request_.reset();have_deadline_=false;phase_=terminal||phase_==LeasePhase::Failed?LeasePhase::Failed:LeasePhase::Unavailable;return {};
}
Result<ClockObservation> AuthorityLease::observe() noexcept {
    auto owned=owner();if(!owned)return fail(owned.error());if(phase_==LeasePhase::Failed)return fail(Error::NotReady);
    auto observation=continuity_->observe(*source_);
    if(!observation){static_cast<void>(invalidate(observation.error()==Error::CounterExhausted));return fail(observation.error());}
    if((have_deadline_||request_)&&observation->proof_generation!=requested_.proof_generation){static_cast<void>(invalidate());return fail(Error::StaleGeneration);}
    return observation;
}
Result<std::uint64_t> AuthorityLease::remaining(const ClockObservation& observation) noexcept {
    if(!have_deadline_)return fail(Error::NotReady);
    if(observation.now_us>=deadline_||observation.uncertainty_us>=deadline_-observation.now_us){static_cast<void>(invalidate());return fail(Error::Timeout);}
    return deadline_-observation.now_us-observation.uncertainty_us;
}
Result<LeaseRequest> AuthorityLease::begin_request() noexcept {
    auto observation=observe();if(!observation)return fail(observation.error());
    if(have_deadline_)static_cast<void>(remaining(*observation));
    if(request_)return fail(Error::Busy);
    if(request_exhausted_){static_cast<void>(invalidate(true));return fail(Error::CounterExhausted);}
    requested_=*observation;request_=next_request_;if(next_request_==UINT64_MAX)request_exhausted_=true;else ++next_request_;return LeaseRequest{*request_,observation->proof_generation,scope_};
}
Status AuthorityLease::authenticated_grant(const AuthorityGrant& grant) noexcept {
    auto observation=observe();if(!observation)return fail(observation.error());
    if(grant.scope!=scope_)return fail(Error::AuthenticationFailed);
    if(grant.proof_generation!=requested_.proof_generation)return fail(Error::StaleGeneration);
    if(!request_||grant.request!=*request_)return fail(Error::StaleGeneration);
    if(observation->proof_generation!=requested_.proof_generation){static_cast<void>(invalidate());return fail(Error::StaleGeneration);}
    if(!grant.epoch||static_cast<unsigned>(grant.kind)>1||!grant.ttl_us||grant.ttl_us>config_.maximum_ttl_us)return fail(Error::InvalidArgument);
    if((have_grant_&&(grant.coordinator_term<last_term_||grant.epoch<last_epoch_))||(have_expired_epoch_&&grant.epoch<=expired_epoch_))return fail(Error::StaleEpoch);
    if(have_grant_&&grant.membership_generation<grant_.membership_generation)return fail(Error::StaleGeneration);
    if(have_grant_&&grant.coordinator_term==last_term_&&grant.grant_sequence<=last_grant_)return fail(Error::StaleGeneration);
    if(have_grant_&&grant.coordinator_term>last_term_&&grant.epoch<=last_epoch_)return fail(Error::StaleEpoch);
    if(have_grant_&&grant.epoch==last_epoch_&&(grant.owner!=grant_.owner||grant.kind!=grant_.kind||grant.membership_generation!=grant_.membership_generation))return fail(Error::ProtocolViolation);
    // Conservative local duration under the qualified relative rate envelope.
    const auto duration=grant.ttl_us*1000000/(1000000+config_.relative_rate_ppm);
    if(duration>UINT64_MAX-requested_.now_us){static_cast<void>(invalidate(true));return fail(Error::CounterExhausted);}
    const auto deadline=requested_.now_us+duration;
    if(observation->now_us>=deadline||observation->uncertainty_us>=deadline-observation->now_us){request_.reset();return fail(Error::Timeout);}
    // A reply cannot revive an authority whose previous lease has expired,
    // even if the caller did not make an authorize() call at the boundary.
    if(have_deadline_){auto prior=remaining(*observation);if(!prior&&have_expired_epoch_&&grant.epoch<=expired_epoch_)return fail(Error::StaleEpoch);}
    const bool renewed=phase_==LeasePhase::Active&&grant.epoch==last_epoch_&&grant.coordinator_term==last_term_;
    grant_=grant;last_term_=grant.coordinator_term;last_epoch_=grant.epoch;last_grant_=grant.grant_sequence;
    deadline_=deadline;have_grant_=true;have_deadline_=true;request_.reset();phase_=renewed?LeasePhase::Active:LeasePhase::AwaitingRestore;return {};
}
Status AuthorityLease::restored(std::uint64_t term,Epoch epoch,std::uint64_t membership,PeerId peer,AuthorityKind kind) noexcept {
    auto observation=observe();if(!observation)return fail(observation.error());
    if(phase_!=LeasePhase::AwaitingRestore)return fail(Error::NotReady);
    auto duration=remaining(*observation);if(!duration)return fail(duration.error());
    if(term!=grant_.coordinator_term||epoch!=grant_.epoch||membership!=grant_.membership_generation||peer!=grant_.owner||kind!=grant_.kind)return fail(Error::StaleEpoch);
    phase_=LeasePhase::Active;return {};
}
Result<LeasePosition> AuthorityLease::authorize(PeerId peer,AuthorityKind kind,Epoch epoch,std::uint64_t membership) noexcept {
    auto observation=observe();if(!observation)return fail(observation.error());
    auto duration=remaining(*observation);if(!duration)return fail(duration.error());
    if(phase_!=LeasePhase::Active)return fail(Error::NotReady);
    if(peer!=grant_.owner||kind!=grant_.kind)return fail(Error::PermissionDenied);
    if(epoch!=grant_.epoch||membership!=grant_.membership_generation)return fail(Error::StaleEpoch);
    return LeasePosition{grant_.coordinator_term,grant_.membership_generation,grant_.epoch,grant_.owner,grant_.kind,*duration};
}
Status AuthorityLease::revoke() noexcept {auto owned=owner();if(!owned)return owned;return invalidate();}
Result<LeasePhase> AuthorityLease::phase() const noexcept {auto owned=owner();if(!owned)return fail(owned.error());return phase_;}
Result<LeaseScope> AuthorityLease::scope_identity() const noexcept {auto owned=owner();if(!owned)return fail(owned.error());return scope_;}
}
