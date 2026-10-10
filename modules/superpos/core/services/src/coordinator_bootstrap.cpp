#include "superpos/coordinator_bootstrap.hpp"
namespace superpos::service {
namespace {
struct Enter {
    bool& entered;
    explicit Enter(bool& value) noexcept:entered(value){entered=true;}
    ~Enter(){entered=false;}
};
bool nonzero(const std::array<std::byte,32>& value) noexcept {
    for(auto byte:value)if(byte!=std::byte{})return true;return false;
}
}
CoordinatorBootstrap::CoordinatorBootstrap(JournalExecutor& queue,LeaseNonceProvider& nonce) noexcept
    :queue_(&queue),nonce_(&nonce),owner_(std::this_thread::get_id()) {}
Status CoordinatorBootstrap::own() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(entering_)return fail(Error::Busy);return {};
}
Status CoordinatorBootstrap::stop(Error error) noexcept {
    identity_.reset();phase_=error==Error::CounterExhausted?BootstrapPhase::Retired:BootstrapPhase::Failed;
    return fail(error);
}
Status CoordinatorBootstrap::start() noexcept {
    auto owned=own();if(!owned)return owned;Enter enter(entering_);
    if(phase_!=BootstrapPhase::Dormant)return fail(Error::NotReady);
    auto queued=queue_->query_boot();if(!queued)return queued.error()==Error::Busy?Status(fail(queued.error())):stop(queued.error());
    ticket_=*queued;phase_=BootstrapPhase::Reading;return {};
}
Status CoordinatorBootstrap::submit_commit() noexcept {
    auto queued=queue_->begin_boot(expected_,fresh_);if(!queued)return queued.error()==Error::Busy?Status(fail(queued.error())):stop(queued.error());
    ticket_=*queued;phase_=BootstrapPhase::Committing;return {};
}
Status CoordinatorBootstrap::submit_bind() noexcept {
    if(!identity_)return stop(Error::ProtocolViolation);
    auto queued=queue_->bind_boot(*identity_);if(!queued)return queued.error()==Error::Busy?Status(fail(queued.error())):stop(queued.error());
    ticket_=*queued;phase_=BootstrapPhase::Binding;return {};
}
Status CoordinatorBootstrap::poll() noexcept {
    auto owned=own();if(!owned)return owned;Enter enter(entering_);
    if(!ticket_){
        if(phase_==BootstrapPhase::Committing)return submit_commit();
        if(phase_==BootstrapPhase::Binding)return submit_bind();
        return fail(Error::NotReady);
    }
    auto done=queue_->consume(*ticket_);
    if(!done){if(done.error()==Error::Busy)return fail(done.error());ticket_.reset();return stop(done.error());}
    ticket_.reset();
    if(phase_==BootstrapPhase::Reading){
        if(done->kind!=JournalJobKind::QueryBoot)return stop(Error::ProtocolViolation);
        if(done->error!=Error::None)return stop(done->error);
        if(done->boot&&done->boot->term==UINT64_MAX)return stop(Error::CounterExhausted);
        expected_=done->boot?std::optional<std::uint64_t>(done->boot->term):std::nullopt;
        auto generated=nonce_->fresh(fresh_);if(!generated)return stop(generated.error());
        if(!nonzero(fresh_)||(done->boot&&done->boot->nonce==fresh_))return stop(Error::AuthenticationFailed);
        phase_=BootstrapPhase::Committing;return submit_commit();
    }
    if(phase_==BootstrapPhase::Committing){
        if(done->kind!=JournalJobKind::BeginBoot)return stop(Error::ProtocolViolation);
        if(done->error==Error::UnknownOutcome){uncertain_=true;phase_=BootstrapPhase::Unknown;return fail(done->error);}
        if(uncertain_&&done->error!=Error::None){phase_=BootstrapPhase::Unknown;return fail(done->error);}
        if(done->error!=Error::None)return stop(done->error);
        const auto term=expected_?*expected_+1:0;
        if(!done->boot||done->boot->term!=term||done->boot->nonce!=fresh_)return stop(Error::ProtocolViolation);
        uncertain_=false;identity_=CoordinatorBootIdentity{term,fresh_};phase_=BootstrapPhase::Binding;return submit_bind();
    }
    if(phase_==BootstrapPhase::Binding){
        if(done->kind!=JournalJobKind::BindBoot)return stop(Error::ProtocolViolation);
        if(done->error!=Error::None)return stop(done->error);
        phase_=BootstrapPhase::Ready;return {};
    }
    return stop(Error::ProtocolViolation);
}
Status CoordinatorBootstrap::retry_unknown() noexcept {
    auto owned=own();if(!owned)return owned;Enter enter(entering_);
    if(phase_!=BootstrapPhase::Unknown||ticket_)return fail(Error::NotReady);
    return submit_commit();
}
Result<CoordinatorBootIdentity> CoordinatorBootstrap::identity() const noexcept {
    auto owned=own();if(!owned)return fail(owned.error());
    if(phase_!=BootstrapPhase::Ready||!identity_)return fail(Error::NotReady);return *identity_;
}
Result<BootstrapPhase> CoordinatorBootstrap::phase() const noexcept {
    auto owned=own();if(!owned)return fail(owned.error());return phase_;
}
}
