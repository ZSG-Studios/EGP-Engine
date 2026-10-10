#include "superpos/service/detail/coordinator_timing.hpp"
#include <limits>
#include <algorithm>
namespace superpos::service::detail {
namespace {
bool same(const DurableAuthorityProposal& a,const DurableAuthorityProposal& b) noexcept {
    const auto& x=a.grant;const auto& y=b.grant;
    return a.expected_grant_sequence==b.expected_grant_sequence&&a.expected_epoch==b.expected_epoch&&
        x.request==y.request&&x.coordinator_term==y.coordinator_term&&x.grant_sequence==y.grant_sequence&&
        x.membership_generation==y.membership_generation&&x.epoch==y.epoch&&x.owner==y.owner&&x.kind==y.kind&&
        x.ttl_us==y.ttl_us&&x.scope==y.scope&&x.proof_generation==y.proof_generation;
}
Result<std::uint64_t> deadline(std::uint64_t now,std::uint64_t duration) noexcept {
    if(duration>UINT64_MAX-now)return fail(Error::CounterExhausted);return now+duration;
}
}
CoordinatorTiming::CoordinatorTiming(ClockSource& source,ContinuityGuard& clock) noexcept
    :source_(&source),clock_(&clock),owner_(std::this_thread::get_id()) {}
Result<ClockObservation> CoordinatorTiming::observe() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(terminal_)return fail(Error::CounterExhausted);
    auto now=clock_->observe(*source_);
    if(!now){suspended_=true;issuable_=false;renewable_=false;return fail(now.error());}
    if(now->uncertainty_us>100000){suspended_=true;issuable_=false;renewable_=false;return fail(Error::NotReady);}
    if(generation_&&now->proof_generation!=generation_){suspended_=true;issuable_=false;renewable_=false;}
    return now;
}
Status CoordinatorTiming::wait_from(const ClockObservation& now) noexcept {
    // Conservative duration in the service's potentially faster clock domain.
    // Integer ceil and current uncertainty are additional to the takeover guard.
    auto end=deadline(now.now_us,(ttl*(1000000+ppm)+999999)/1000000+guard+now.uncertainty_us);
    if(!end){suspended_=true;terminal_=true;return fail(end.error());}
    wait_until_=*end;generation_=now.proof_generation;suspended_=false;issuable_=false;renewable_=false;return {};
}
Status CoordinatorTiming::bind_committed_boot(CoordinatorBootIdentity boot) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(boot_)return fail(Error::Busy);
    bool nonzero=false;for(auto byte:boot.nonce)nonzero|=byte!=std::byte{};
    if(!nonzero)return fail(Error::InvalidArgument);
    auto now=observe();if(!now)return fail(now.error());
    auto waited=wait_from(*now);if(!waited)return waited;boot_=boot;return {};
}
Status CoordinatorTiming::restart_wait_after_revalidation() noexcept {
    auto now=observe();if(!now)return fail(now.error());
    if(!boot_||pending_)return fail(Error::NotReady);
    return wait_from(*now);
}
Status CoordinatorTiming::prepare(DurableAuthorityProposal proposal) noexcept {
    auto now=observe();if(!now)return fail(now.error());
    if(!boot_||suspended_)return fail(Error::NotReady);
    if(pending_||issuable_)return fail(Error::Busy);
    if(now->now_us<=wait_until_||now->uncertainty_us>=now->now_us-wait_until_)return fail(Error::NotReady);
    const auto& grant=proposal.grant;
    bool nonzero=false;for(auto byte:grant.scope)nonzero|=byte!=std::byte{};
    if(grant.coordinator_term!=boot_->term||!grant.epoch||!grant.ttl_us||grant.ttl_us>ttl||
        static_cast<unsigned>(grant.kind)>1||!nonzero)return fail(Error::InvalidArgument);
    const bool renewal=grant.epoch==proposal.expected_epoch;
    if(renewal){
        if(!renewable_||!committed_||now->now_us>=renew_until_||now->uncertainty_us>=renew_until_-now->now_us)return fail(Error::StaleEpoch);
        const auto& old=committed_->proposal.grant;
        if(old.epoch!=grant.epoch||old.owner!=grant.owner||old.kind!=grant.kind||old.scope!=grant.scope||
            old.membership_generation!=grant.membership_generation)return fail(Error::ProtocolViolation);
    }else {
        if(proposal.expected_epoch==UINT64_MAX||grant.epoch!=proposal.expected_epoch+1)return fail(Error::StaleEpoch);
        if(now->now_us<=hold_until_||now->uncertainty_us>=now->now_us-hold_until_)return fail(Error::NotReady);
    }
    auto end=deadline(now->now_us,grant.ttl_us);if(!end){terminal_=true;suspended_=true;return fail(end.error());}
    prepared_=*now;request_until_=*end;issue_until_=renewal?std::min(*end,renew_until_):*end;pending_=proposal;unknown_=false;return {};
}
Status CoordinatorTiming::admitted(JournalJobTicket ticket) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!pending_)return fail(Error::NotReady);if(accepted_)return fail(Error::Busy);
    if(!ticket.sequence||!ticket.incarnation||ticket.slot>=64)return fail(Error::InvalidArgument);
    accepted_=ticket;return {};
}
Status CoordinatorTiming::admission_rejected() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!pending_||accepted_||unknown_)return fail(Error::NotReady);
    pending_.reset();return {};
}
Status CoordinatorTiming::withdraw() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    suspended_=true;issuable_=false;renewable_=false;return {};
}
Status CoordinatorTiming::completed(JournalJobTicket ticket,const JournalJobCompletion& done) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!accepted_||ticket.slot!=accepted_->slot||ticket.sequence!=accepted_->sequence||ticket.incarnation!=accepted_->incarnation||done.kind!=JournalJobKind::CommitAuthority)return fail(Error::StaleGeneration);
    if(done.error==Error::None){
        if(!done.authority||!pending_||!boot_||done.authority->boot!=*boot_||
            !same(done.authority->proposal,*pending_)||done.authority->fenced_prefix.epoch!=pending_->grant.epoch)return fail(Error::ProtocolViolation);
        accepted_.reset();return committed(*done.authority);
    }
    accepted_.reset();
    if(done.error==Error::UnknownOutcome){auto retained=unknown_outcome();return retained?Status(fail(Error::UnknownOutcome)):retained;}
    // A later failed retry cannot prove that the original uncertain attempt did
    // not commit. Retain it until exact durable reconfirmation or a new boot.
    if(unknown_)return fail(done.error);
    pending_.reset();suspended_=true;issuable_=false;renewable_=false;
    return fail(done.error);
}
Status CoordinatorTiming::unknown_outcome() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!pending_)return fail(Error::NotReady);unknown_=true;issuable_=false;return {};
}
Result<DurableAuthorityProposal> CoordinatorTiming::exact_retry() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!unknown_||!pending_||accepted_)return fail(Error::NotReady);return *pending_;
}
Status CoordinatorTiming::committed(const DurableAuthorityReceipt& receipt) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!pending_||!boot_||receipt.boot!=*boot_||!same(receipt.proposal,*pending_)||
        receipt.fenced_prefix.epoch!=pending_->grant.epoch)return fail(Error::ProtocolViolation);
    auto now=observe();
    committed_=receipt;pending_.reset();unknown_=false;issuable_=false;
    if(!now)return fail(now.error());
    if(suspended_||now->proof_generation!=prepared_.proof_generation){suspended_=true;renewable_=false;return fail(Error::StaleGeneration);}
    // Even a completion too late to send may represent a committed grant. Retain
    // conservative fencing headroom; a fresh takeover must wait this interval.
    auto end=deadline(now->now_us,(ttl*(1000000+ppm)+999999)/1000000+guard+now->uncertainty_us);
    if(!end){suspended_=true;terminal_=true;return fail(end.error());}hold_until_=*end;
    if(now->now_us>=issue_until_||now->uncertainty_us>=issue_until_-now->now_us){renewable_=false;return fail(Error::Timeout);}
    issuable_=true;renewable_=false;return {};
}
Result<AuthorityGrant> CoordinatorTiming::potential_send() noexcept {
    auto now=observe();if(!now)return fail(now.error());
    if(suspended_||!issuable_||!committed_)return fail(Error::NotReady);
    if(now->proof_generation!=prepared_.proof_generation){suspended_=true;issuable_=false;renewable_=false;return fail(Error::StaleGeneration);}
    if(now->now_us>=issue_until_||now->uncertainty_us>=issue_until_-now->now_us){issuable_=false;renewable_=false;return fail(Error::Timeout);}
    auto end=deadline(now->now_us,(ttl*(1000000+ppm)+999999)/1000000+guard+now->uncertainty_us);
    if(!end){suspended_=true;issuable_=false;terminal_=true;return fail(end.error());}
    hold_until_=*end;renew_until_=request_until_;issuable_=false;renewable_=true;return committed_->proposal.grant;
}
}
