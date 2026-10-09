// SPDX-License-Identifier: MIT
#include "superpos/service/supervisor.hpp"
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace superpos::service {
namespace {
bool nonzero(const LeaseScope& value) noexcept {
    for(auto byte:value)if(byte!=std::byte{})return true;return false;
}
bool overlaps(const void* first,std::size_t bytes,const void* second,std::size_t other) noexcept {
    if(!bytes||!other)return false;
    if(!first||!second)return true;
    const auto a=reinterpret_cast<std::uintptr_t>(first),b=reinterpret_cast<std::uintptr_t>(second);
    constexpr auto maximum=std::numeric_limits<std::uintptr_t>::max();
    if(bytes>maximum-a||other>maximum-b)return true;
    return a<b+other&&b<a+bytes;
}
bool durable_control(JournalJobKind kind) noexcept {
    return kind==JournalJobKind::AppendControl||kind==JournalJobKind::CommitControlPolicy||
        kind==JournalJobKind::InstallExecutionLease;
}
struct Entry {
    bool& entered;
    explicit Entry(bool& value) noexcept:entered(value){entered=true;}
    ~Entry(){entered=false;}
};
SupervisorPhase mapped(CoordinatorPhase phase) noexcept {
    switch(phase){
    case CoordinatorPhase::Unbound:case CoordinatorPhase::Initializing:return SupervisorPhase::Initializing;
    case CoordinatorPhase::Ready:return SupervisorPhase::Ready;
    case CoordinatorPhase::Pending:return SupervisorPhase::Pending;
    case CoordinatorPhase::Unknown:return SupervisorPhase::GrantUnknown;
    case CoordinatorPhase::Confirmed:return SupervisorPhase::Confirmed;
    case CoordinatorPhase::Suspended:return SupervisorPhase::Suspended;
    case CoordinatorPhase::Retired:return SupervisorPhase::Retired;
    }
    return SupervisorPhase::Failed;
}
}
struct OwnedCoordinatorSupervisor::Impl {
    Allocator* allocator;
    LeaseNonceProvider* nonce;
    ClockSource* source;
    ContinuityGuard* clock;
    AuthorityEligibilityProvider* eligibility;
    SupervisorConfig config;
    const std::thread::id owner{std::this_thread::get_id()};
    mutable bool entering{};
    SupervisorPhase phase{SupervisorPhase::Opening};
    Error failure{Error::None};
    bool initialized{};
    std::optional<NativeStorageWorker> worker;
    JournalExecutor* queue{};
    std::optional<CoordinatorBootstrap> bootstrap;
    std::optional<AuthorityCoordinator> coordinator;
    std::optional<CoordinatorBootIdentity> boot;
    std::optional<JournalJobTicket> inspection;
    std::optional<JournalPrefix> prefix;
    std::optional<Error> prefix_error;
    std::optional<JournalJobTicket> control_ticket;
    std::optional<JournalJobTicket> control_identity;
    std::optional<SupervisorControlCompletion> control_result;
    std::optional<JournalJobKind> control_kind;
    std::optional<control::PolicyChange> expected_policy;
    struct AppendIdentity {std::uint64_t id{},epoch{},tick{};std::uint32_t bytes{};};
    std::optional<AppendIdentity> expected_append;
    bool control_abandoned{};
    bool control_claimed{};

    Impl(Allocator& a, SupervisorConfig c, LeaseNonceProvider& n, ClockSource& s,
        ContinuityGuard& g, AuthorityEligibilityProvider& e) noexcept
        :allocator(&a),nonce(&n),source(&s),clock(&g),eligibility(&e),config(c){}
    Status own() const noexcept {
        if(owner!=std::this_thread::get_id())return fail(Error::PermissionDenied);
        if(entering)return fail(Error::Busy);return {};
    }
    void record(Error error) noexcept {if(failure==Error::None)failure=error;}
    Status terminal(Error error) noexcept {
        record(error);phase=SupervisorPhase::Failed;
        if(worker){auto stopped=worker->request_stop();if(!stopped)record(stopped.error());}
        return fail(error);
    }
    Status retire(Error error) noexcept {
        record(error);phase=SupervisorPhase::Retired;
        if(worker){auto stopped=worker->request_stop();if(!stopped)record(stopped.error());}
        return fail(error);
    }
    Status health() noexcept {
        if(phase==SupervisorPhase::Retired)return fail(failure);
        if(!worker||phase==SupervisorPhase::Stopping||phase==SupervisorPhase::Stopped||phase==SupervisorPhase::Failed)return fail(Error::NotReady);
        auto state=worker->phase();if(!state)return terminal(state.error());
        if(*state==StorageWorkerPhase::Failed){auto error=worker->failure();return terminal(error?*error:error.error());}
        if(*state==StorageWorkerPhase::Closing||*state==StorageWorkerPhase::Stopped)return terminal(Error::RecoveryUnavailable);
        return {};
    }
    Status synchronize(Status result) noexcept {
        auto state=coordinator->phase();if(!state)return terminal(state.error());
        phase=mapped(*state);
        if(phase==SupervisorPhase::Retired)return retire(result?Error::RecoveryUnavailable:result.error());
        if(!result&&(phase==SupervisorPhase::Suspended||phase==SupervisorPhase::Retired))record(result.error());
        return result;
    }
    Status capture_prefix() noexcept {
        if(!inspection)return {};
        auto done=queue->consume(*inspection);
        if(!done){if(done.error()!=Error::Busy){inspection.reset();prefix_error=done.error();}return fail(done.error());}
        inspection.reset();
        if(done->kind!=JournalJobKind::Prefix){prefix_error=Error::ProtocolViolation;return fail(Error::ProtocolViolation);}
        if(done->error!=Error::None){prefix_error=done->error;return fail(done->error);}
        prefix=done->prefix;return {};
    }
    bool aliases(const void* input,std::size_t bytes,const OwnedCoordinatorSupervisor* wrapper) const noexcept {
        return overlaps(input,bytes,this,sizeof(*this))||overlaps(input,bytes,wrapper,sizeof(*wrapper));
    }
    Status control_admission() noexcept {
        if(!initialized||!queue)return fail(Error::NotReady);
        auto healthy=health();if(!healthy)return healthy;
        if(control_ticket||control_result)return fail(Error::Busy);
        return {};
    }
    Status accepted_control(Result<JournalJobTicket> ticket,JournalJobKind kind) noexcept {
        if(!ticket)return ticket.error()==Error::CounterExhausted?retire(ticket.error()):Status(fail(ticket.error()));
        control_ticket=*ticket;control_identity=*ticket;control_kind=kind;control_abandoned=false;control_claimed=false;return {};
    }
    void clear_control() noexcept {
        control_ticket.reset();control_identity.reset();control_result.reset();control_kind.reset();control_claimed=false;
        expected_policy.reset();expected_append.reset();control_abandoned=false;
    }
    Status capture_control() noexcept {
        if(!control_ticket)return {};
        if(!queue)return fail(Error::NotReady);
        auto done=queue->consume(*control_ticket);
        // Failed lookup does not transfer or retire our Accepted ownership.
        // Keep the exact ticket until a successful consume or actual join.
        if(!done){if(done.error()!=Error::Busy)record(done.error());return fail(done.error());}
        const auto expected=control_kind.value_or(JournalJobKind::ReadControl);
        SupervisorControlCompletion projected{};projected.kind=expected;
        bool malformed=!control_kind||done->kind!=expected||done->output_bytes!=0;
        if(!malformed){
            projected.error=done->error;
            if(done->error==Error::None){
                switch(expected){
                case JournalJobKind::ReadControl:projected.prefix=done->prefix;break;
                case JournalJobKind::AppendControl:
                    malformed=!expected_append||done->receipt.append_id!=expected_append->id||
                        done->receipt.epoch!=expected_append->epoch||done->receipt.tick!=expected_append->tick||
                        done->receipt.bytes!=expected_append->bytes;
                    if(!malformed)projected.receipt=done->receipt;break;
                case JournalJobKind::CommitControlPolicy:
                    malformed=!expected_policy||!done->control_policy||done->control_policy->change!=*expected_policy;
                    if(!malformed)projected.policy=done->control_policy;break;
                case JournalJobKind::QueryAuthority:projected.authority=done->authority;break;
                case JournalJobKind::BindControlIssuer:case JournalJobKind::InstallExecutionLease:case JournalJobKind::Checkpoint:break;
                default:malformed=true;break;
                }
            }
        }
        control_ticket.reset();
        if(malformed){
            // A malformed completion cannot prove the accepted durable write
            // failed. Never manufacture a successful or definitive rejection.
            projected={};projected.kind=expected;
            projected.error=durable_control(expected)?Error::UnknownOutcome:Error::ProtocolViolation;
            record(Error::ProtocolViolation);
            if(phase!=SupervisorPhase::Stopping&&phase!=SupervisorPhase::Stopped&&phase!=SupervisorPhase::Retired)
                static_cast<void>(terminal(Error::ProtocolViolation));
        }else if(projected.error==Error::CounterExhausted){
            // Stored exhaustion and queue-counter exhaustion both close admission.
            // Keep this actual typed outcome; retirement never discards its credit.
            record(projected.error);
            if(phase!=SupervisorPhase::Stopping&&phase!=SupervisorPhase::Stopped&&phase!=SupervisorPhase::Retired)
                static_cast<void>(retire(projected.error));
        }
        control_result=projected;
        if(control_abandoned){record(projected.error);clear_control();}
        return {};
    }
    void settle_joined_control() noexcept {
        if(!control_ticket)return;
        auto captured=capture_control();if(captured)return;
        // The worker really joined. A missing accepted mutation completion stays
        // Unknown; a read/binding has no observed successful outcome.
        const auto kind=control_kind.value_or(JournalJobKind::ReadControl);
        SupervisorControlCompletion projected{};projected.kind=kind;
        projected.error=durable_control(kind)?Error::UnknownOutcome:Error::RecoveryUnavailable;
        record(captured.error());record(projected.error);control_ticket.reset();control_result=projected;
        if(control_abandoned)clear_control();
    }
};
OwnedCoordinatorSupervisor::OwnedCoordinatorSupervisor(Impl* value) noexcept:impl_(value){}
OwnedCoordinatorSupervisor::OwnedCoordinatorSupervisor(OwnedCoordinatorSupervisor&& other) noexcept {
    if(other.impl_&&!other.impl_->own())std::abort();impl_=std::exchange(other.impl_,nullptr);
}
OwnedCoordinatorSupervisor& OwnedCoordinatorSupervisor::operator=(OwnedCoordinatorSupervisor&& other) noexcept {
    if(this!=&other){if(other.impl_&&!other.impl_->own())std::abort();destroy();impl_=std::exchange(other.impl_,nullptr);}return *this;
}
OwnedCoordinatorSupervisor::~OwnedCoordinatorSupervisor(){destroy();}
void OwnedCoordinatorSupervisor::destroy() noexcept {
    if(!impl_)return;if(!impl_->own())std::abort();
    // Keep destructor/allocator callbacks out of every still-live field. The
    // guard is not released: this Impl is about to be destroyed, after real join.
    impl_->entering=true;
    if(impl_->worker){
        impl_->phase=SupervisorPhase::Stopping;
        if(!impl_->worker->request_stop())std::abort();
        for(;;){auto joined=impl_->worker->poll_join(1000);if(joined)break;if(joined.error()!=Error::Busy)std::abort();}
    }
    // The native worker has exited before any queue borrower is destroyed.
    impl_->settle_joined_control();
    impl_->coordinator.reset();impl_->bootstrap.reset();impl_->queue=nullptr;
    impl_->worker.reset();auto* allocator=impl_->allocator;
    auto* retired=std::exchange(impl_,nullptr);
    retired->~Impl();allocator->deallocate(retired);
}
Result<OwnedCoordinatorSupervisor> OwnedCoordinatorSupervisor::create(Allocator& allocator,
    std::string_view path,SupervisorConfig config,LeaseNonceProvider& nonce,
    ClockSource& source,ContinuityGuard& clock,AuthorityEligibilityProvider& eligibility) noexcept {
    if(!config.match||config.match!=config.storage.journal.match||!nonzero(config.match_instance)||!nonzero(config.ordering_instance)||
        !nonzero(config.eligibility_instance))return fail(Error::InvalidArgument);
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);
    if(!memory)return fail(Error::OutOfMemory);
    auto* impl=new(memory) Impl(allocator,config,nonce,source,clock,eligibility);
    OwnedCoordinatorSupervisor result(impl);
    auto worker=NativeStorageWorker::create(allocator,path,config.storage);
    if(!worker)return fail(worker.error());impl->worker.emplace(std::move(*worker));return result;
}
Status OwnedCoordinatorSupervisor::poll() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(impl_->control_abandoned&&impl_->control_ticket)static_cast<void>(impl_->capture_control());
    if(impl_->phase==SupervisorPhase::Stopping||impl_->phase==SupervisorPhase::Stopped)return fail(Error::NotReady);
    if(impl_->phase==SupervisorPhase::Failed)return fail(impl_->failure);
    if(impl_->phase==SupervisorPhase::Retired)return fail(impl_->failure);
    auto worker=impl_->worker->phase();if(!worker)return impl_->terminal(worker.error());
    if(*worker==StorageWorkerPhase::Failed){auto error=impl_->worker->failure();return impl_->terminal(error?*error:error.error());}
    if(*worker==StorageWorkerPhase::Stopped||*worker==StorageWorkerPhase::Closing)return impl_->terminal(Error::RecoveryUnavailable);
    if(*worker==StorageWorkerPhase::Opening)return fail(Error::Busy);
    if(impl_->phase==SupervisorPhase::Opening){
        auto queue=impl_->worker->queue();if(!queue)return impl_->terminal(queue.error());
        impl_->queue=*queue;impl_->bootstrap.emplace(**queue,*impl_->nonce);impl_->phase=SupervisorPhase::Bootstrapping;
    }
    if(impl_->phase==SupervisorPhase::BootUnknown)return fail(Error::UnknownOutcome);
    if(impl_->phase==SupervisorPhase::Bootstrapping){
        auto phase=impl_->bootstrap->phase();if(!phase)return impl_->terminal(phase.error());
        auto progressed=*phase==BootstrapPhase::Dormant?impl_->bootstrap->start():impl_->bootstrap->poll();
        phase=impl_->bootstrap->phase();if(!phase)return impl_->terminal(phase.error());
        if(*phase==BootstrapPhase::Unknown){impl_->phase=SupervisorPhase::BootUnknown;return progressed;}
        if(*phase==BootstrapPhase::Retired)return impl_->retire(progressed?Error::CounterExhausted:progressed.error());
        if(*phase==BootstrapPhase::Failed)return impl_->terminal(progressed?Error::RecoveryUnavailable:progressed.error());
        if(*phase!=BootstrapPhase::Ready)return progressed;
        auto identity=impl_->bootstrap->identity();if(!identity)return impl_->terminal(identity.error());
        impl_->boot=*identity;
        MatchAuthorityContext context{impl_->config.match,impl_->config.match_instance,
            impl_->config.ordering_instance,impl_->config.eligibility_instance,*identity};
        impl_->coordinator.emplace(*impl_->queue,*impl_->source,*impl_->clock,*impl_->eligibility,context);
        auto started=impl_->coordinator->start();
        if(!started)return started.error()==Error::CounterExhausted?impl_->retire(started.error()):impl_->terminal(started.error());impl_->phase=SupervisorPhase::Initializing;return {};
    }
    if(impl_->phase==SupervisorPhase::Initializing||impl_->phase==SupervisorPhase::Pending){
        auto progressed=impl_->synchronize(impl_->coordinator->poll());
        if(impl_->phase==SupervisorPhase::Ready)impl_->initialized=true;
        if(impl_->phase==SupervisorPhase::Retired)return progressed;
        if(!impl_->initialized&&(impl_->phase==SupervisorPhase::Suspended||impl_->phase==SupervisorPhase::Retired))
            return impl_->terminal(progressed?Error::RecoveryUnavailable:progressed.error());
        return progressed;
    }
    if(impl_->phase==SupervisorPhase::GrantUnknown)return fail(Error::UnknownOutcome);
    return {};
}
Status OwnedCoordinatorSupervisor::request(AuthorityRequest request) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto healthy=impl_->health();if(!healthy)return healthy;
    if(impl_->phase!=SupervisorPhase::Ready)return fail(Error::NotReady);
    return impl_->synchronize(impl_->coordinator->request(request));
}
Status OwnedCoordinatorSupervisor::retry_unknown() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto healthy=impl_->health();if(!healthy)return healthy;
    if(impl_->phase==SupervisorPhase::BootUnknown){
        auto retry=impl_->bootstrap->retry_unknown();
        auto state=impl_->bootstrap->phase();if(!state)return impl_->terminal(state.error());
        if(*state==BootstrapPhase::Retired)return impl_->retire(retry?Error::CounterExhausted:retry.error());
        if(*state==BootstrapPhase::Failed)return impl_->terminal(retry?Error::RecoveryUnavailable:retry.error());
        if(retry)impl_->phase=SupervisorPhase::Bootstrapping;return retry;
    }
    if(impl_->phase!=SupervisorPhase::GrantUnknown)return fail(Error::NotReady);
    return impl_->synchronize(impl_->coordinator->retry_unknown());
}
Status OwnedCoordinatorSupervisor::resume_after_revalidation() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto healthy=impl_->health();if(!healthy)return healthy;
    if(impl_->phase!=SupervisorPhase::Suspended)return fail(Error::NotReady);
    return impl_->synchronize(impl_->coordinator->resume_after_revalidation());
}
Status OwnedCoordinatorSupervisor::publish(AuthorityGrantPublisher& publisher) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto healthy=impl_->health();if(!healthy)return healthy;
    if(impl_->phase!=SupervisorPhase::Confirmed)return fail(Error::NotReady);
    return impl_->synchronize(impl_->coordinator->publish(publisher));
}
Result<SupervisorPhase> OwnedCoordinatorSupervisor::phase() const noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);return impl_->phase;
}
Result<Error> OwnedCoordinatorSupervisor::failure() const noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);return impl_->failure;
}
Result<CoordinatorBootIdentity> OwnedCoordinatorSupervisor::identity() const noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);
    if(!impl_->boot)return fail(Error::NotReady);return *impl_->boot;
}
Status OwnedCoordinatorSupervisor::request_prefix() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto healthy=impl_->health();if(!healthy)return healthy;
    if(impl_->phase!=SupervisorPhase::Ready||!impl_->queue)return fail(Error::NotReady);
    if(impl_->inspection||impl_->prefix||impl_->prefix_error)return fail(Error::Busy);
    auto ticket=impl_->queue->prefix();if(!ticket)return ticket.error()==Error::CounterExhausted?impl_->retire(ticket.error()):Status(fail(ticket.error()));impl_->inspection=*ticket;return {};
}
Result<JournalPrefix> OwnedCoordinatorSupervisor::consume_prefix() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);
    if(impl_->inspection){auto captured=impl_->capture_prefix();if(!captured&&captured.error()==Error::Busy)return fail(Error::Busy);}
    if(impl_->prefix_error){auto error=*impl_->prefix_error;impl_->prefix_error.reset();return fail(error);}
    if(!impl_->prefix)return fail(Error::NotReady);auto value=*impl_->prefix;impl_->prefix.reset();return value;
}
Status OwnedCoordinatorSupervisor::bind_control_issuer(const control::IssuerIdentity& identity) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    if(impl_->aliases(&identity,sizeof(identity),this))return fail(Error::InvalidArgument);
    return impl_->accepted_control(impl_->queue->bind_control_issuer(identity),JournalJobKind::BindControlIssuer);
}
Status OwnedCoordinatorSupervisor::commit_control_policy(const control::PolicyChange& change) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    if(impl_->aliases(&change,sizeof(change),this))return fail(Error::InvalidArgument);
    auto accepted=impl_->accepted_control(impl_->queue->commit_control_policy(change),JournalJobKind::CommitControlPolicy);
    if(accepted)impl_->expected_policy=change;return accepted;
}
Status OwnedCoordinatorSupervisor::install_execution_lease(const control::ExecutionLease& lease) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    if(impl_->aliases(&lease,sizeof(lease),this))return fail(Error::InvalidArgument);
    return impl_->accepted_control(impl_->queue->install_execution_lease(lease),JournalJobKind::InstallExecutionLease);
}
Status OwnedCoordinatorSupervisor::request_control_authority() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    return impl_->accepted_control(impl_->queue->query_authority(),JournalJobKind::QueryAuthority);
}
Status OwnedCoordinatorSupervisor::checkpoint_control() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(auto admitted=impl_->control_admission();!admitted)return admitted;
    return impl_->accepted_control(impl_->queue->checkpoint(),JournalJobKind::Checkpoint);
}
Status OwnedCoordinatorSupervisor::read_control(const control::ExecutionPermit& permit) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    if(impl_->aliases(&permit,sizeof(permit),this))return fail(Error::InvalidArgument);
    return impl_->accepted_control(impl_->queue->read_control(permit),JournalJobKind::ReadControl);
}
Status OwnedCoordinatorSupervisor::append_control(const control::ExecutionPermit& permit,std::uint64_t id,
    Tick tick,std::span<const std::byte> body) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    auto admitted=impl_->control_admission();if(!admitted)return admitted;
    if(body.size()>4000)return fail(Error::CapacityExceeded);
    if(impl_->aliases(&permit,sizeof(permit),this)||impl_->aliases(body.data(),body.size(),this))return fail(Error::InvalidArgument);
    auto accepted=impl_->accepted_control(impl_->queue->append_control(permit,id,tick,body),JournalJobKind::AppendControl);
    if(accepted)impl_->expected_append=Impl::AppendIdentity{id,permit.fields().policy.authority_epoch,tick,static_cast<std::uint32_t>(body.size())};
    return accepted;
}
Result<SupervisorControlCompletion> OwnedCoordinatorSupervisor::poll_control() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);
    if(impl_->control_claimed)return fail(Error::Busy);
    if(impl_->control_ticket){auto captured=impl_->capture_control();if(!captured)return fail(captured.error());}
    if(!impl_->control_result)return fail(Error::NotReady);return *impl_->control_result;
}
Status OwnedCoordinatorSupervisor::consume_control() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(impl_->control_claimed)return fail(Error::Busy);
    if(impl_->control_ticket){auto captured=impl_->capture_control();if(!captured)return captured;}
    if(!impl_->control_result)return fail(Error::NotReady);impl_->clear_control();return {};
}
Status OwnedCoordinatorSupervisor::abandon_control() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(impl_->control_claimed)return fail(Error::Busy);
    if(impl_->control_ticket){impl_->control_abandoned=true;return {};}
    if(impl_->control_result){impl_->record(impl_->control_result->error);impl_->clear_control();}return {};
}

Result<SupervisorControlTicket> OwnedCoordinatorSupervisor::claim_control() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);
    if(impl_->control_claimed)return fail(Error::Busy);
    if(!impl_->control_identity||impl_->control_abandoned)return fail(Error::NotReady);
    impl_->control_claimed=true;return SupervisorControlTicket(*impl_->control_identity);
}
Result<SupervisorControlCompletion> OwnedCoordinatorSupervisor::poll_control(SupervisorControlTicket ticket) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return fail(owned.error());Entry entry(impl_->entering);
    if(!impl_->control_claimed||impl_->control_abandoned||!impl_->control_identity||
        ticket!=SupervisorControlTicket(*impl_->control_identity))return fail(Error::StaleGeneration);
    if(impl_->control_ticket){auto captured=impl_->capture_control();if(!captured)return fail(captured.error());}
    if(!impl_->control_result)return fail(Error::NotReady);return *impl_->control_result;
}
Status OwnedCoordinatorSupervisor::consume_control(SupervisorControlTicket ticket) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(!impl_->control_claimed||impl_->control_abandoned||!impl_->control_identity||
        ticket!=SupervisorControlTicket(*impl_->control_identity))return fail(Error::StaleGeneration);
    if(impl_->control_ticket){auto captured=impl_->capture_control();if(!captured)return captured;}
    if(!impl_->control_result)return fail(Error::NotReady);impl_->clear_control();return {};
}
Status OwnedCoordinatorSupervisor::abandon_control(SupervisorControlTicket ticket) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(!impl_->control_claimed||impl_->control_abandoned||!impl_->control_identity||
        ticket!=SupervisorControlTicket(*impl_->control_identity))return fail(Error::StaleGeneration);
    if(impl_->control_ticket){impl_->control_abandoned=true;return {};}
    if(!impl_->control_result)return fail(Error::NotReady);
    impl_->record(impl_->control_result->error);impl_->clear_control();return {};
}
Status OwnedCoordinatorSupervisor::shutdown() noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(impl_->phase==SupervisorPhase::Stopped||impl_->phase==SupervisorPhase::Stopping)return {};
    impl_->phase=SupervisorPhase::Stopping;
    auto stopped=impl_->worker->request_stop();if(!stopped)impl_->record(stopped.error());return stopped;
}
Status OwnedCoordinatorSupervisor::poll_shutdown(std::uint32_t wait_ms) noexcept {
    if(!impl_)return fail(Error::NotReady);auto owned=impl_->own();if(!owned)return owned;Entry entry(impl_->entering);
    if(wait_ms>1000)return fail(Error::InvalidArgument);
    if(impl_->phase==SupervisorPhase::Stopped)return {};
    if(impl_->phase!=SupervisorPhase::Stopping)return fail(Error::NotReady);
    auto joined=impl_->worker->poll_join(wait_ms);if(!joined)return joined;
    auto error=impl_->worker->failure();if(!error)impl_->record(error.error());else if(*error!=Error::None)impl_->record(*error);
    if(impl_->inspection){auto captured=impl_->capture_prefix();if(!captured){
        auto unavailable=captured.error()==Error::Busy?Error::RecoveryUnavailable:captured.error();
        impl_->record(unavailable);impl_->prefix_error=unavailable;impl_->inspection.reset();}}
    impl_->settle_joined_control();
    impl_->coordinator.reset();impl_->bootstrap.reset();impl_->queue=nullptr;impl_->worker.reset();
    impl_->phase=SupervisorPhase::Stopped;return {};
}
}
