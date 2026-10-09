#include "superpos/service/coordinator.hpp"
namespace superpos::service {
namespace {
struct Entry {
    bool& value;
    explicit Entry(bool& v) noexcept:value(v){value=true;}
    ~Entry(){value=false;}
};
bool same_context(const MatchAuthorityContext& a,const MatchAuthorityContext& b) noexcept {
    return a.match==b.match&&a.match_instance==b.match_instance&&a.ordering_instance==b.ordering_instance&&
        a.eligibility_instance==b.eligibility_instance&&a.boot==b.boot;
}
bool same_request(const AuthorityRequest& a,const AuthorityRequest& b) noexcept {
    return a.peer==b.peer&&a.kind==b.kind&&a.intent==b.intent&&a.admission_instance==b.admission_instance&&
        a.wire_session==b.wire_session&&a.connection_epoch==b.connection_epoch&&a.membership_generation==b.membership_generation&&
        a.lease.request==b.lease.request&&a.lease.proof_generation==b.lease.proof_generation&&a.lease.scope==b.lease.scope;
}
bool same_proposal(const DurableAuthorityProposal& a,const DurableAuthorityProposal& b) noexcept {
    const auto& x=a.grant;const auto& y=b.grant;
    return a.expected_grant_sequence==b.expected_grant_sequence&&a.expected_epoch==b.expected_epoch&&
        x.request==y.request&&x.coordinator_term==y.coordinator_term&&x.grant_sequence==y.grant_sequence&&
        x.membership_generation==y.membership_generation&&x.epoch==y.epoch&&x.owner==y.owner&&x.kind==y.kind&&
        x.ttl_us==y.ttl_us&&x.scope==y.scope&&x.proof_generation==y.proof_generation;
}
}
AuthorityCoordinator::AuthorityCoordinator(JournalExecutor& queue,ClockSource& source,ContinuityGuard& clock,
    AuthorityEligibilityProvider& provider,MatchAuthorityContext context) noexcept
    :queue_(&queue),source_(&source),clock_(&clock),eligibility_(&provider),context_(context),
     timing_(source,clock),owner_(std::this_thread::get_id()) {}
Status AuthorityCoordinator::own() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(entering_)return fail(Error::Busy);return {};
}
Status AuthorityCoordinator::suspend(Error error) noexcept {
    static_cast<void>(timing_.withdraw());binding_.reset();phase_=error==Error::CounterExhausted?CoordinatorPhase::Retired:CoordinatorPhase::Suspended;return fail(error);
}
Status AuthorityCoordinator::submit_query(Work work) noexcept {
    work_=work;
    Result<JournalJobTicket> queued=fail(Error::InvalidArgument);
    if(work==Work::Boot)queued=queue_->query_boot();
    else if(work==Work::Match)queued=queue_->query_match();
    else if(work==Work::Prefix)queued=queue_->prefix();
    else if(work==Work::Authority)queued=queue_->query_authority();
    if(!queued)return queued.error()==Error::CounterExhausted?suspend(queued.error()):Status(fail(queued.error()));ticket_=*queued;return {};
}
Status AuthorityCoordinator::start() noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(phase_!=CoordinatorPhase::Unbound)return fail(Error::NotReady);
    if(!context_.match||!known_fingerprint(context_.match_instance)||!known_fingerprint(context_.ordering_instance)||
        !known_fingerprint(context_.eligibility_instance)||!known_fingerprint(context_.boot.nonce))return fail(Error::InvalidArgument);
    auto queued=submit_query(Work::Boot);if(!queued)return queued;
    phase_=CoordinatorPhase::Initializing;return {};
}
Status AuthorityCoordinator::validate(const AuthorityEligibility& binding,const AuthorityRequest& request,std::uint64_t local_generation) const noexcept {
    if(!same_context(binding.context,context_)||!same_request(binding.request,request)||
        binding.local_continuity_generation!=local_generation)return fail(Error::AuthenticationFailed);
    if(!known_fingerprint(request.admission_instance)||!known_fingerprint(request.lease.scope)||
        static_cast<unsigned>(request.kind)>1||static_cast<unsigned>(request.intent)>1||
        !known_fingerprint(binding.membership_set)||!known_fingerprint(binding.schemas)||!known_fingerprint(binding.simulation))return fail(Error::InvalidArgument);
    if(!binding.hidden_state_authorized||!binding.all_members_routable||!binding.complete_codec||!binding.ready)return fail(Error::PermissionDenied);
    if(!binding.complete_state_bytes||binding.reserved_capacity_bytes<binding.complete_state_bytes)return fail(Error::CapacityExceeded);
    return {};
}
Status AuthorityCoordinator::submit_commit(DurableAuthorityProposal proposal) noexcept {
    auto queued=queue_->commit_authority(proposal);if(!queued)return queued.error()==Error::CounterExhausted?suspend(queued.error()):Status(fail(queued.error()));
    auto accepted=timing_.admitted(*queued);
    ticket_=*queued;work_=Work::Commit;
    if(!accepted){phase_=CoordinatorPhase::Retired;return fail(accepted.error());}
    phase_=CoordinatorPhase::Pending;return {};
}
Status AuthorityCoordinator::request(AuthorityRequest request) noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(phase_!=CoordinatorPhase::Ready||ticket_)return fail(Error::NotReady);
    auto now=clock_->observe(*source_);if(!now)return suspend(now.error());
    auto admitted=eligibility_->admit(context_,request,now->proof_generation);if(!admitted)return fail(admitted.error());
    auto after=clock_->observe(*source_);if(!after)return suspend(after.error());
    if(after->proof_generation!=now->proof_generation)return suspend(Error::StaleGeneration);
    auto checked=validate(*admitted,request,now->proof_generation);if(!checked)return checked;
    if(current_&&request.membership_generation<current_->proposal.grant.membership_generation)return fail(Error::StaleGeneration);
    std::uint64_t sequence=0;
    if(current_){if(current_->proposal.grant.grant_sequence==UINT64_MAX)return suspend(Error::CounterExhausted);sequence=current_->proposal.grant.grant_sequence+1;}
    Epoch epoch=prefix_.epoch;
    if(request.intent==GrantIntent::Takeover){if(epoch==UINT64_MAX)return suspend(Error::CounterExhausted);++epoch;}
    DurableAuthorityProposal proposal{current_?std::optional<std::uint64_t>(current_->proposal.grant.grant_sequence):std::nullopt,prefix_.epoch,
        {request.lease.request,context_.boot.term,sequence,request.membership_generation,epoch,request.peer,request.kind,2000000,request.lease.scope,request.lease.proof_generation}};
    auto prepared=timing_.prepare(proposal);if(!prepared)return prepared.error()==Error::CounterExhausted?suspend(prepared.error()):prepared;
    binding_=*admitted;proposal_=proposal;auto submitted=submit_commit(proposal);
    if(!submitted&&!ticket_){static_cast<void>(timing_.admission_rejected());binding_.reset();proposal_.reset();}
    return submitted;
}
Status AuthorityCoordinator::poll() noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(!ticket_){if(phase_==CoordinatorPhase::Initializing)return submit_query(work_);return fail(Error::NotReady);}
    const auto ticket=*ticket_;auto done=queue_->consume(ticket);
    if(!done){
        if(done.error()==Error::Busy)return fail(done.error());
        ticket_.reset();phase_=CoordinatorPhase::Retired;static_cast<void>(timing_.withdraw());return fail(done.error());
    }
    ticket_.reset();
    if(phase_==CoordinatorPhase::Retired)return fail(Error::RecoveryUnavailable);
    if(work_!=Work::Commit){
        if(done->error!=Error::None)return suspend(done->error);
        if(work_==Work::Boot){
            if(done->kind!=JournalJobKind::QueryBoot||!done->boot||done->boot->term!=context_.boot.term||done->boot->nonce!=context_.boot.nonce)return suspend(Error::StaleEpoch);
            if(!boot_bound_){auto bound=timing_.bind_committed_boot(context_.boot);if(!bound)return suspend(bound.error());boot_bound_=true;}
            return submit_query(Work::Match);
        }
        if(work_==Work::Match){
            if(done->kind!=JournalJobKind::QueryMatch||!done->match||*done->match!=context_.match)return suspend(Error::AuthenticationFailed);
            return submit_query(Work::Prefix);
        }
        if(work_==Work::Prefix){
            if(done->kind!=JournalJobKind::Prefix)return suspend(Error::ProtocolViolation);
            prefix_=done->prefix;return submit_query(Work::Authority);
        }
        if(done->kind!=JournalJobKind::QueryAuthority)return suspend(Error::ProtocolViolation);
        current_=done->authority;
        if(current_&&current_->proposal.grant.epoch!=prefix_.epoch)return suspend(Error::RecoveryUnavailable);
        phase_=CoordinatorPhase::Ready;return {};
    }
    if(done->error==Error::None&&(!done->authority||!proposal_||!same_proposal(done->authority->proposal,*proposal_)||
        done->authority->boot!=context_.boot||done->authority->fenced_prefix.epoch!=proposal_->grant.epoch)){
        phase_=CoordinatorPhase::Retired;static_cast<void>(timing_.withdraw());return fail(Error::ProtocolViolation);
    }
    auto resolved=timing_.completed(ticket,*done);
    if(done->error==Error::None&&done->authority){current_=*done->authority;prefix_=done->authority->fenced_prefix;}
    if(!resolved){
        if(timing_.exact_retry()){phase_=CoordinatorPhase::Unknown;return fail(resolved.error());}
        return suspend(resolved.error());
    }
    proposal_.reset();
    if(!binding_)return suspend(Error::ProtocolViolation);
    auto valid=eligibility_->revalidate(*binding_);if(!valid)return suspend(valid.error());
    phase_=CoordinatorPhase::Confirmed;return {};
}
Status AuthorityCoordinator::retry_unknown() noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(phase_!=CoordinatorPhase::Unknown||ticket_)return fail(Error::NotReady);
    auto proposal=timing_.exact_retry();if(!proposal)return fail(proposal.error());return submit_commit(*proposal);
}
Status AuthorityCoordinator::publish(AuthorityGrantPublisher& publisher) noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(phase_!=CoordinatorPhase::Confirmed||!binding_)return fail(Error::NotReady);
    auto valid=eligibility_->revalidate(*binding_);if(!valid)return suspend(valid.error());
    auto grant=timing_.potential_send();if(!grant)return suspend(grant.error());
    // Transition before calling the sink; a failed handoff cannot be retried as
    // another permission or erase the already recorded authority hold.
    phase_=CoordinatorPhase::Ready;return publisher.send(*grant,*binding_);
}
Status AuthorityCoordinator::resume_after_revalidation() noexcept {
    auto owned=own();if(!owned)return owned;Entry entry(entering_);
    if(phase_!=CoordinatorPhase::Suspended||ticket_)return fail(Error::NotReady);
    auto waited=timing_.restart_wait_after_revalidation();if(!waited)return waited.error()==Error::CounterExhausted?suspend(waited.error()):waited;
    phase_=CoordinatorPhase::Initializing;return submit_query(Work::Boot);
}
Result<RestoreReadCapability> AuthorityCoordinator::restore_read(CheckpointTicket checkpoint,std::uint32_t maximum) noexcept {
    auto owned=own();if(!owned)return fail(owned.error());Entry entry(entering_);
    if((phase_!=CoordinatorPhase::Confirmed&&phase_!=CoordinatorPhase::Ready)||ticket_||!current_||!binding_)return fail(Error::NotReady);
    if(checkpoint.match!=context_.match||!checkpoint.id||!checkpoint.generation||!maximum||maximum>1000000)return fail(Error::InvalidArgument);
    const auto& grant=current_->proposal.grant;const auto& granted=binding_->request;
    // Bound to the exact granted request, this coordinator boot and its fence.
    if(current_->boot!=context_.boot||current_->fenced_prefix.epoch!=grant.epoch||grant.coordinator_term!=context_.boot.term||
        grant.owner!=granted.peer||grant.kind!=granted.kind||grant.request!=granted.lease.request||grant.scope!=granted.lease.scope||
        grant.proof_generation!=granted.lease.proof_generation||grant.membership_generation!=granted.membership_generation||
        !binding_->hidden_state_authorized)return fail(Error::PermissionDenied);
    // The lease bound recorded when this exact grant was prepared and committed.
    if(!timing_.committed_||!same_proposal(timing_.committed_->proposal,current_->proposal)||timing_.pending_)return fail(Error::NotReady);
    const auto expires=timing_.request_until_,generation=timing_.prepared_.proof_generation;
    auto now=clock_->observe(*source_);if(!now)return fail(now.error());
    if(now->proof_generation!=binding_->local_continuity_generation||now->proof_generation!=generation)return fail(Error::StaleGeneration);
    RestoreReadCapability capability({*current_,checkpoint,maximum},context_.match,expires,generation);
    if(!capability.current(*now))return fail(Error::Timeout);
    auto valid=eligibility_->revalidate(*binding_);if(!valid)return fail(valid.error());
    auto after=clock_->observe(*source_);if(!after)return fail(after.error());
    if(!capability.current(*after))return fail(after->proof_generation!=generation?Error::StaleGeneration:Error::Timeout);
    return capability;
}
Result<CoordinatorPhase> AuthorityCoordinator::phase() const noexcept {
    auto owned=own();if(!owned)return fail(owned.error());return phase_;
}
}
