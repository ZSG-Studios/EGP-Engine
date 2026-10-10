#include "superpos/journal_executor.hpp"
#include "superpos/codec.hpp"
#include "executor_identity.hpp"
#include <atomic>
#include <cstring>
#include <new>
#include <thread>
#include <utility>

namespace superpos {
namespace {
std::atomic<std::uint64_t> next_executor_identity{1};
enum class Phase : std::uint8_t { Free,Queued,Complete,Abandoned };
struct Job {
    std::atomic<Phase> phase{Phase::Free};
    std::uint64_t sequence{},epoch{},id{},tick{};
    std::uint32_t bytes{};
    JournalJobKind kind{};
    DurableOperationId operation{};
    PeerId peer{};
    std::optional<std::uint64_t> boot_expected{};
    std::array<std::byte,32> boot_nonce{};
    std::optional<DurableAuthorityProposal> authority{};
    std::optional<service::control::IssuerIdentity> control_identity{};
    std::optional<service::control::PolicyChange> control_policy{};
    std::optional<service::control::ExecutionLease> execution_lease{};
    std::optional<service::control::ExecutionPermit> execution_permit{};
    CheckpointTicket checkpoint_ticket{};
    std::optional<CheckpointManifest> checkpoint_manifest{};
    std::optional<CanonicalRestoreScope> restore_scope{};
    JournalJobCompletion completion{};
};
}
struct JournalExecutor::Impl {
    Allocator* allocator{};
    Job* jobs{};
    std::byte* payload{};
    JournalQueueConfig config{};
    std::thread::id owner{std::this_thread::get_id()},worker{};
    SqliteJournal* journal{};
    std::atomic_flag stepping=ATOMIC_FLAG_INIT;
    std::uint64_t next_sequence{1};
    std::uint64_t incarnation{};
    std::uint32_t producer{},consumer{};
    bool admission_closed{};
};
JournalExecutor::JournalExecutor(Impl* p) noexcept:impl_(p){}
JournalExecutor::JournalExecutor(JournalExecutor&& other) noexcept:impl_(std::exchange(other.impl_,nullptr)){}
JournalExecutor& JournalExecutor::operator=(JournalExecutor&& other) noexcept {
    if(this!=&other){this->~JournalExecutor();new(this) JournalExecutor(std::move(other));}return *this;
}
JournalExecutor::~JournalExecutor(){
    if(!impl_)return;
    auto* allocator=impl_->allocator;
    for(std::uint32_t i=0;i<impl_->config.slots;++i)impl_->jobs[i].~Job();
    allocator->deallocate(impl_->jobs);allocator->deallocate(impl_->payload);
    impl_->~Impl();allocator->deallocate(impl_);
}
Result<JournalExecutor> JournalExecutor::create(Allocator& allocator,JournalQueueConfig config) noexcept {
    if(!config.slots||config.slots>64||!config.payload_bytes||config.payload_bytes>2*1024*1024)return fail(Error::InvalidArgument);
    auto identity=detail::reserve_executor_identity(next_executor_identity);if(!identity)return fail(identity.error());
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);
    if(!memory)return fail(Error::OutOfMemory);
    auto* impl=new(memory) Impl;impl->allocator=&allocator;impl->config=config;impl->incarnation=*identity;
    impl->jobs=static_cast<Job*>(allocator.allocate(sizeof(Job)*config.slots,alignof(Job),MemoryDomain::Backend));
    if(!impl->jobs){impl->~Impl();allocator.deallocate(impl);return fail(Error::OutOfMemory);}
    for(std::uint32_t i=0;i<config.slots;++i)new(impl->jobs+i) Job;
    impl->payload=static_cast<std::byte*>(allocator.allocate(static_cast<std::size_t>(config.payload_bytes)*config.slots,alignof(std::max_align_t),MemoryDomain::Backend));
    JournalExecutor result(impl);if(!impl->payload)return fail(Error::OutOfMemory);return result;
}
Result<JournalJobTicket> JournalExecutor::submit(JournalJobKind kind,Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> payload) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(payload.size()>impl_->config.payload_bytes)return fail(Error::CapacityExceeded);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];
    if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    auto* destination=impl_->payload+static_cast<std::size_t>(impl_->producer)*impl_->config.payload_bytes;
    if(!payload.empty()&&payload.data()!=destination)std::memcpy(destination,payload.data(),payload.size());
    job.kind=kind;job.epoch=epoch;job.id=id;job.tick=tick;job.bytes=static_cast<std::uint32_t>(payload.size());job.sequence=impl_->next_sequence++;
    JournalJobTicket ticket{job.sequence,impl_->producer,impl_->incarnation};
    job.phase.store(Phase::Queued,std::memory_order_release);
    impl_->producer=(impl_->producer+1)%impl_->config.slots;return ticket;
}
Result<JournalJobTicket> JournalExecutor::append(Epoch epoch,std::uint64_t id,Tick tick,std::span<const std::byte> payload) noexcept {return submit(JournalJobKind::Append,epoch,id,tick,payload);}
Result<JournalJobTicket> JournalExecutor::append_decisions(Epoch epoch,std::uint64_t id,Tick tick,
    std::span<const std::byte> canonical,std::span<const DurableDecision> decisions,std::span<const DurableEffect> effects) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(decisions.size()>64||effects.size()>64||canonical.size()>1024*1024)return fail(Error::CapacityExceeded);
    std::size_t required=24+canonical.size();
    for(const auto& decision:decisions){
        if(static_cast<unsigned>(decision.id.kind)>2)return fail(Error::InvalidArgument);
        if(decision.request.size()>4096||decision.result.size()>4096)return fail(Error::CapacityExceeded);
        required+=56+decision.request.size()+decision.result.size();
    }
    for(const auto& effect:effects){if(effect.payload.size()>4096)return fail(Error::CapacityExceeded);required+=16+effect.payload.size();}
    if(required>impl_->config.payload_bytes)return fail(Error::CapacityExceeded);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    if(impl_->jobs[impl_->producer].phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    auto bytes=std::span(impl_->payload+static_cast<std::size_t>(impl_->producer)*impl_->config.payload_bytes,required);
    Writer writer(bytes);
    if(!writer.u64(canonical.size())||!writer.u64(decisions.size())||!writer.u64(effects.size())||!writer.raw(canonical))return fail(Error::CapacityExceeded);
    for(const auto& decision:decisions){
        if(!writer.u64(decision.id.match)||!writer.u64(decision.id.actor)||!writer.u64(decision.id.actor_epoch)||!writer.u64(decision.id.sequence)||
           !writer.u64(static_cast<std::uint64_t>(decision.id.kind))||!writer.u64(decision.request.size())||!writer.u64(decision.result.size())||
           !writer.raw(decision.request)||!writer.raw(decision.result))return fail(Error::CapacityExceeded);
    }
    for(const auto& effect:effects)if(!writer.u64(effect.recipient)||!writer.u64(effect.payload.size())||!writer.raw(effect.payload))return fail(Error::CapacityExceeded);
    return submit(JournalJobKind::Decisions,epoch,id,tick,bytes);
}
Result<JournalJobTicket> JournalExecutor::query(std::uint64_t id) noexcept {return submit(JournalJobKind::Query,0,id,0,{});}
Result<JournalJobTicket> JournalExecutor::fence(Epoch expected,Epoch replacement) noexcept {return submit(JournalJobKind::Fence,expected,replacement,0,{});}
Result<JournalJobTicket> JournalExecutor::checkpoint() noexcept {return submit(JournalJobKind::Checkpoint,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::submit_checkpoint(JournalJobKind kind,CheckpointTicket ticket,std::uint32_t index,std::span<const std::byte> payload) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    if(!ticket.match||!ticket.id||!ticket.generation||index>=checkpoint_maximum_chunks||payload.size()>checkpoint_chunk_bytes)return fail(Error::InvalidArgument);
    job.checkpoint_ticket=ticket;return submit(kind,0,index,0,payload);
}
Result<JournalJobTicket> JournalExecutor::begin_checkpoint(CheckpointManifest manifest) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);if(impl_->admission_closed)return fail(Error::NotReady);auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);job.checkpoint_manifest=manifest;return submit(JournalJobKind::CheckpointBegin,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::put_checkpoint_chunk(CheckpointTicket ticket,std::uint32_t index,std::span<const std::byte> bytes) noexcept {return submit_checkpoint(JournalJobKind::CheckpointPut,ticket,index,bytes);}
Result<JournalJobTicket> JournalExecutor::seal_checkpoint(CheckpointTicket ticket) noexcept {return submit_checkpoint(JournalJobKind::CheckpointSeal,ticket,0);}
Result<JournalJobTicket> JournalExecutor::query_checkpoint(std::uint64_t id) noexcept {return submit(JournalJobKind::CheckpointQuery,0,id,0,{});}
Result<JournalJobTicket> JournalExecutor::abandon_checkpoint(CheckpointTicket ticket) noexcept {return submit_checkpoint(JournalJobKind::CheckpointAbandon,ticket,0);}
Result<JournalJobTicket> JournalExecutor::read_checkpoint_chunk(CheckpointTicket ticket,std::uint32_t index) noexcept {
    if(!impl_||impl_->config.payload_bytes<checkpoint_chunk_bytes)return fail(Error::CapacityExceeded);return submit_checkpoint(JournalJobKind::CheckpointRead,ticket,index);
}
Result<JournalJobTicket> JournalExecutor::begin_checkpoint_service(CheckpointTicket ticket,bool restart) noexcept {return submit_checkpoint(JournalJobKind::CheckpointServiceBegin,ticket,restart?1:0);}
Result<JournalJobTicket> JournalExecutor::capture_checkpoint_service_page(CheckpointTicket ticket,std::uint32_t expected_page) noexcept {return submit_checkpoint(JournalJobKind::CheckpointServicePage,ticket,expected_page);}
Result<JournalJobTicket> JournalExecutor::checkpoint_service_progress(CheckpointTicket ticket) noexcept {return submit_checkpoint(JournalJobKind::CheckpointServiceQuery,ticket,0);}
Result<JournalJobTicket> JournalExecutor::read_checkpoint_service_page(CheckpointTicket ticket,std::uint32_t index) noexcept {
    if(!impl_||impl_->config.payload_bytes<checkpoint_chunk_bytes)return fail(Error::CapacityExceeded);return submit_checkpoint(JournalJobKind::CheckpointServiceRead,ticket,index);
}

Result<JournalJobTicket> JournalExecutor::prefix() noexcept {return submit(JournalJobKind::Prefix,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::bind_boot(CoordinatorBootIdentity identity) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    bool nonzero=false;for(auto b:identity.nonce)nonzero|=b!=std::byte{};
    if(!nonzero)return fail(Error::InvalidArgument);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];
    if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.boot_expected=identity.term;job.boot_nonce=identity.nonce;
    return submit(JournalJobKind::BindBoot,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::query_match() noexcept {return submit(JournalJobKind::QueryMatch,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::query_authority() noexcept {return submit(JournalJobKind::QueryAuthority,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::commit_authority(DurableAuthorityProposal proposal) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];
    if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.authority=proposal;
    return submit(JournalJobKind::CommitAuthority,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::query_boot() noexcept {return submit(JournalJobKind::QueryBoot,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::begin_boot(std::optional<std::uint64_t> expected,
    std::array<std::byte,32> nonce) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    bool nonzero=false;for(auto byte:nonce)nonzero|=byte!=std::byte{};
    if(!nonzero)return fail(Error::InvalidArgument);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];
    if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    // Inline metadata, independent of payload capacity. No fallible operation
    // remains after these copies except the already-preflighted submit checks.
    job.boot_expected=expected;job.boot_nonce=nonce;
    return submit(JournalJobKind::BeginBoot,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::read_committed(Epoch epoch,std::uint64_t sequence) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(!epoch||!sequence)return fail(Error::InvalidArgument);
    if(impl_->config.payload_bytes<2*1024*1024)return fail(Error::CapacityExceeded);
    return submit(JournalJobKind::ReadCommitted,epoch,sequence,0,{});
}
Result<JournalJobTicket> JournalExecutor::restore_evidence(const CanonicalRestoreScope& scope) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(!scope.maximum_records||scope.maximum_records>1000000)return fail(Error::InvalidArgument);
    // submit() performs the counter guard; metadata is only read once Queued.
    auto& job=impl_->jobs[impl_->producer];
    if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.restore_scope=scope;
    return submit(JournalJobKind::RestoreEvidence,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::compact_journal(Epoch expected,std::uint32_t maximum) noexcept {
    if(!expected||!maximum||maximum>4096)return fail(Error::InvalidArgument);
    return submit(JournalJobKind::Compact,expected,maximum,0,{});
}
Result<JournalJobTicket> JournalExecutor::query_receipt(std::uint64_t id) noexcept {
    if(!id)return fail(Error::InvalidArgument);
    return submit(JournalJobKind::QueryReceipt,0,id,0,{});
}
Result<JournalJobTicket> JournalExecutor::submit_operation(JournalJobKind kind,Epoch authority,DurableOperationId operation,PeerId peer,std::span<const std::byte> request) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);
    if(request.size()>4096||request.size()>impl_->config.payload_bytes)return fail(Error::CapacityExceeded);
    if(static_cast<unsigned>(operation.kind)>2)return fail(Error::InvalidArgument);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.operation=operation;job.peer=peer;return submit(kind,authority,0,0,request);
}
Result<JournalJobTicket> JournalExecutor::register_actor(Epoch authority,OperationActorKind kind,std::uint64_t actor,std::uint64_t actor_epoch,PeerId peer,std::uint64_t first) noexcept {
    return submit_operation(JournalJobKind::RegisterActor,authority,{0,actor,actor_epoch,first,kind},peer,{});
}
Result<JournalJobTicket> JournalExecutor::begin_operation(Epoch authority,DurableOperationId id,std::span<const std::byte> request) noexcept {
    return submit_operation(JournalJobKind::BeginOperation,authority,id,0,request);
}
Result<JournalJobTicket> JournalExecutor::query_operation(DurableOperationId id,std::span<const std::byte> request) noexcept {
    return submit_operation(JournalJobKind::QueryOperation,0,id,0,request);
}
Result<JournalJobTicket> JournalExecutor::next_effect() noexcept {return submit(JournalJobKind::NextEffect,0,0,0,{});}
Result<JournalJobTicket> JournalExecutor::acknowledge_effect(std::uint64_t id,std::uint32_t ordinal,std::uint64_t recipient) noexcept {
    if(ordinal>=64)return fail(Error::InvalidArgument);
    return submit_operation(JournalJobKind::AcknowledgeEffect,0,{id,recipient,0,ordinal,OperationActorKind::Player},0,{});
}
Result<JournalJobCompletion> JournalExecutor::consume(JournalJobTicket ticket,std::span<std::byte> output) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(!ticket.sequence||ticket.slot>=impl_->config.slots)return fail(Error::InvalidArgument);
    if(!ticket.incarnation)return fail(Error::InvalidArgument);
    if(ticket.incarnation!=impl_->incarnation)return fail(Error::StaleGeneration);
    auto& job=impl_->jobs[ticket.slot];
    if(job.sequence!=ticket.sequence)return fail(Error::StaleGeneration);
    auto phase=job.phase.load(std::memory_order_acquire);
    if(phase==Phase::Free||phase==Phase::Abandoned)return fail(Error::StaleGeneration);
    if(phase!=Phase::Complete)return fail(Error::Busy);
    if(output.size()<job.completion.output_bytes)return fail(Error::CapacityExceeded);
    if(job.completion.output_bytes)std::memcpy(output.data(),impl_->payload+static_cast<std::size_t>(ticket.slot)*impl_->config.payload_bytes,job.completion.output_bytes);
    auto result=job.completion;job.phase.store(Phase::Free,std::memory_order_release);return result;
}
Status JournalExecutor::abandon(JournalJobTicket ticket) noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(!ticket.sequence||ticket.slot>=impl_->config.slots||!ticket.incarnation)return fail(Error::InvalidArgument);
    if(ticket.incarnation!=impl_->incarnation)return fail(Error::StaleGeneration);
    auto& job=impl_->jobs[ticket.slot];
    if(job.sequence!=ticket.sequence)return fail(Error::StaleGeneration);
    for(;;){
        auto phase=job.phase.load(std::memory_order_acquire);
        if(phase==Phase::Free||phase==Phase::Abandoned)return fail(Error::StaleGeneration);
        if(phase==Phase::Complete){job.phase.store(Phase::Free,std::memory_order_release);return {};}
        // Still queued or running: the worker frees the slot when it finishes.
        if(job.phase.compare_exchange_weak(phase,Phase::Abandoned,std::memory_order_acq_rel,std::memory_order_acquire))return {};
    }
}
Result<bool> JournalExecutor::worker_step(SqliteJournal& journal) noexcept {
    if(!impl_)return fail(Error::NotReady);
    auto caller=std::this_thread::get_id();
    if(caller==impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->stepping.test_and_set(std::memory_order_acquire))return fail(Error::Busy);
    struct Guard {std::atomic_flag& flag;~Guard(){flag.clear(std::memory_order_release);}} guard{impl_->stepping};
    if(impl_->worker==std::thread::id{}){impl_->worker=caller;impl_->journal=&journal;}
    if(caller!=impl_->worker||impl_->journal!=&journal)return fail(Error::PermissionDenied);
    // Maintain local suspend/gap detection even on idle worker steps. A paused
    // control clock blocks protected operations; trusted restore jobs still run.
    (void)journal.control_heartbeat();
    auto& job=impl_->jobs[impl_->consumer];
    const auto admitted=job.phase.load(std::memory_order_acquire);
    if(admitted!=Phase::Queued&&admitted!=Phase::Abandoned)return false;
    auto payload=std::span(impl_->payload+static_cast<std::size_t>(impl_->consumer)*impl_->config.payload_bytes,impl_->config.payload_bytes);
    JournalJobCompletion completed{};completed.kind=job.kind;
    switch(job.kind){
    case JournalJobKind::BindControlIssuer:{if(!job.control_identity){completed.error=Error::ProtocolViolation;break;}auto r=journal.bind_control_issuer(*job.control_identity);if(!r)completed.error=r.error();break;}
    case JournalJobKind::CommitControlPolicy:{if(!job.control_policy){completed.error=Error::ProtocolViolation;break;}auto r=journal.commit_control_policy(*job.control_policy);if(r)completed.control_policy=*r;else completed.error=r.error();break;}
    case JournalJobKind::InstallExecutionLease:{if(!job.execution_lease){completed.error=Error::ProtocolViolation;break;}auto r=journal.install_execution_lease(*job.execution_lease);if(!r)completed.error=r.error();break;}
    case JournalJobKind::ReadControl:{if(!job.execution_permit){completed.error=Error::ProtocolViolation;break;}auto r=journal.read_control(*job.execution_permit);if(r)completed.prefix=*r;else completed.error=r.error();break;}
    case JournalJobKind::AppendControl:{if(!job.execution_permit){completed.error=Error::ProtocolViolation;break;}auto r=journal.append_control(*job.execution_permit,job.id,job.tick,payload.first(job.bytes));if(r)completed.receipt=*r;else completed.error=r.error();break;}
    case JournalJobKind::Append:{auto r=journal.append(job.epoch,job.id,job.tick,payload.first(job.bytes));if(r)completed.receipt=*r;else completed.error=r.error();break;}
    case JournalJobKind::Decisions:{
        Reader reader(payload.first(job.bytes));auto canonical_bytes=reader.u64(),decision_count=reader.u64(),effect_count=reader.u64();
        if(!canonical_bytes||!decision_count||!effect_count||*canonical_bytes>1024*1024||*decision_count>64||*effect_count>64){completed.error=Error::ProtocolViolation;break;}
        auto canonical=reader.raw(static_cast<std::size_t>(*canonical_bytes));if(!canonical){completed.error=Error::ProtocolViolation;break;}
        std::array<DurableDecision,64> decisions{};std::array<DurableEffect,64> effects{};bool valid=true;
        for(std::size_t i=0;i<*decision_count;++i){
            auto match=reader.u64(),actor=reader.u64(),actor_epoch=reader.u64(),sequence=reader.u64(),kind=reader.u64(),request_bytes=reader.u64(),result_bytes=reader.u64();
            if(!match||!actor||!actor_epoch||!sequence||!kind||!request_bytes||!result_bytes||*kind>2||*request_bytes>4096||*result_bytes>4096){valid=false;break;}
            auto request=reader.raw(static_cast<std::size_t>(*request_bytes)),result=reader.raw(static_cast<std::size_t>(*result_bytes));if(!request||!result){valid=false;break;}
            decisions[i]={{*match,*actor,*actor_epoch,*sequence,static_cast<OperationActorKind>(*kind)},*request,*result};
        }
        for(std::size_t i=0;valid&&i<*effect_count;++i){auto recipient=reader.u64(),length=reader.u64();if(!recipient||!length||*length>4096){valid=false;break;}auto effect=reader.raw(static_cast<std::size_t>(*length));if(!effect){valid=false;break;}effects[i]={*recipient,*effect};}
        if(!valid||!reader.empty()){completed.error=Error::ProtocolViolation;break;}
        auto r=journal.append_decisions(job.epoch,job.id,job.tick,*canonical,std::span(decisions).first(static_cast<std::size_t>(*decision_count)),std::span(effects).first(static_cast<std::size_t>(*effect_count)));
        if(r)completed.receipt=*r;else completed.error=r.error();break;
    }
    case JournalJobKind::Query:{auto r=journal.query(job.id,payload);if(r){completed.receipt=*r;completed.output_bytes=r->bytes;}else completed.error=r.error();break;}
    case JournalJobKind::ReadCommitted:{
        constexpr std::size_t half=1024*1024;
        if(payload.size()<2*half){completed.error=Error::CapacityExceeded;break;}
        auto r=journal.read_committed(job.epoch,job.id,payload.first(half),payload.subspan(half,half));
        if(r){
            completed.committed=*r;completed.output_bytes=r->receipt.bytes+r->envelope_bytes;
            if(r->envelope_bytes)std::memmove(payload.data()+r->receipt.bytes,payload.data()+half,r->envelope_bytes);
        }else completed.error=r.error();break;
    }
    case JournalJobKind::Fence:{auto r=journal.fence(job.epoch,job.id);if(r)completed.prefix=*r;else completed.error=r.error();break;}
    case JournalJobKind::Checkpoint:{auto r=journal.checkpoint();if(!r)completed.error=r.error();break;}
    case JournalJobKind::CheckpointBegin:{if(!job.checkpoint_manifest){completed.error=Error::InvalidArgument;break;}auto r=journal.begin_checkpoint(*job.checkpoint_manifest);if(r)completed.checkpoint=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointPut:{auto r=journal.put_checkpoint_chunk(job.checkpoint_ticket,static_cast<std::uint32_t>(job.id),payload.first(job.bytes));if(!r)completed.error=r.error();break;}
    case JournalJobKind::CheckpointSeal:{auto r=journal.seal_checkpoint(job.checkpoint_ticket);if(r)completed.checkpoint=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointQuery:{auto r=journal.query_checkpoint(job.id);if(r)completed.checkpoint=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointAbandon:{auto r=journal.abandon_checkpoint(job.checkpoint_ticket);if(!r)completed.error=r.error();break;}
    case JournalJobKind::CheckpointRead:{auto r=journal.read_checkpoint_chunk(job.checkpoint_ticket,static_cast<std::uint32_t>(job.id),payload);if(r)completed.output_bytes=static_cast<std::uint32_t>(*r);else completed.error=r.error();break;}
    case JournalJobKind::CheckpointServiceBegin:{auto r=journal.begin_checkpoint_service(job.checkpoint_ticket,job.id!=0);if(r)completed.checkpoint_service=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointServicePage:{auto r=journal.capture_checkpoint_service_page(job.checkpoint_ticket,static_cast<std::uint32_t>(job.id));if(r)completed.checkpoint_service=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointServiceQuery:{auto r=journal.checkpoint_service_progress(job.checkpoint_ticket);if(r)completed.checkpoint_service=*r;else completed.error=r.error();break;}
    case JournalJobKind::CheckpointServiceRead:{auto r=journal.read_checkpoint_service_page(job.checkpoint_ticket,static_cast<std::uint32_t>(job.id),payload);if(r)completed.output_bytes=static_cast<std::uint32_t>(*r);else completed.error=r.error();break;}

    case JournalJobKind::RestoreEvidence:{if(!job.restore_scope){completed.error=Error::ProtocolViolation;break;}auto r=journal.canonical_restore_evidence(*job.restore_scope);if(r)completed.restore=*r;else completed.error=r.error();break;}
    case JournalJobKind::Compact:{auto r=journal.compact_journal(job.epoch,static_cast<std::uint32_t>(job.id));if(r)completed.compaction=*r;else completed.error=r.error();break;}
    case JournalJobKind::QueryReceipt:{auto r=journal.query_receipt(job.id);if(r)completed.receipt=*r;else completed.error=r.error();break;}
    case JournalJobKind::Prefix:{auto r=journal.prefix();if(r)completed.prefix=*r;else completed.error=r.error();break;}
    case JournalJobKind::QueryMatch:{auto r=journal.match_identity();if(r)completed.match=*r;else completed.error=r.error();break;}
    case JournalJobKind::QueryAuthority:{auto r=journal.authority_grant();if(r)completed.authority=*r;else completed.error=r.error();break;}
    case JournalJobKind::CommitAuthority:{if(!job.authority){completed.error=Error::ProtocolViolation;break;}auto r=journal.commit_authority(*job.authority);if(r)completed.authority=*r;else completed.error=r.error();break;}
    case JournalJobKind::BindBoot:{if(!job.boot_expected){completed.error=Error::ProtocolViolation;break;}auto r=journal.bind_coordinator_boot({*job.boot_expected,job.boot_nonce});if(!r)completed.error=r.error();break;}
    case JournalJobKind::QueryBoot:{auto r=journal.coordinator_boot();if(r)completed.boot=*r;else completed.error=r.error();break;}
    case JournalJobKind::BeginBoot:{auto r=journal.begin_coordinator_boot(job.boot_expected,job.boot_nonce);if(r)completed.boot=*r;else completed.error=r.error();break;}
    case JournalJobKind::RegisterActor:{auto r=journal.register_actor(job.epoch,job.operation.kind,job.operation.actor,job.operation.actor_epoch,job.peer,job.operation.sequence);if(!r)completed.error=r.error();break;}
    case JournalJobKind::BeginOperation:
    case JournalJobKind::QueryOperation:{
        std::array<std::byte,4096> request{};if(job.bytes)std::memcpy(request.data(),payload.data(),job.bytes);
        auto r=job.kind==JournalJobKind::BeginOperation?journal.begin_operation(job.epoch,job.operation,std::span(request).first(job.bytes),payload):journal.query_operation(job.operation,std::span(request).first(job.bytes),payload);
        if(r){completed.operation=*r;completed.output_bytes=r->result_bytes;}else completed.error=r.error();break;
    }
    case JournalJobKind::NextEffect:{auto r=journal.next_effect(payload);if(r){completed.effect=*r;completed.output_bytes=r->bytes;}else completed.error=r.error();break;}
    case JournalJobKind::AcknowledgeEffect:{auto r=journal.acknowledge_effect(job.operation.match,static_cast<std::uint32_t>(job.operation.sequence),job.operation.actor);if(!r)completed.error=r.error();break;}
    }
    job.completion=completed;
    // An abandoned job still ran in order; nobody will consume it, so free it.
    auto queued=Phase::Queued;
    if(!job.phase.compare_exchange_strong(queued,Phase::Complete,std::memory_order_acq_rel,std::memory_order_acquire))job.phase.store(Phase::Free,std::memory_order_release);
    impl_->consumer=(impl_->consumer+1)%impl_->config.slots;return true;
}
}

namespace superpos { Status JournalExecutor::stop_admission() noexcept {
    if(!impl_)return fail(Error::NotReady);
    if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    impl_->admission_closed=true;return {};
} }

namespace superpos {
Result<JournalJobTicket> JournalExecutor::bind_control_issuer(const service::control::IssuerIdentity& identity) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.control_identity=identity;return submit(JournalJobKind::BindControlIssuer,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::commit_control_policy(const service::control::PolicyChange& policy) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.control_policy=policy;return submit(JournalJobKind::CommitControlPolicy,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::install_execution_lease(const service::control::ExecutionLease& lease) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.execution_lease=lease;return submit(JournalJobKind::InstallExecutionLease,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::read_control(const service::control::ExecutionPermit& permit) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);if(permit.fields().permission!=2)return fail(Error::PermissionDenied);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.execution_permit=permit;return submit(JournalJobKind::ReadControl,0,0,0,{});
}
Result<JournalJobTicket> JournalExecutor::append_control(const service::control::ExecutionPermit& permit,std::uint64_t id,Tick tick,std::span<const std::byte> payload) noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    if(impl_->admission_closed)return fail(Error::NotReady);if(permit.fields().permission!=4)return fail(Error::PermissionDenied);
    if(!id)return fail(Error::InvalidArgument);if(payload.size()>4000||payload.size()>impl_->config.payload_bytes)return fail(Error::CapacityExceeded);
    if(impl_->next_sequence==UINT64_MAX)return fail(Error::CounterExhausted);
    auto& job=impl_->jobs[impl_->producer];if(job.phase.load(std::memory_order_acquire)!=Phase::Free)return fail(Error::Busy);
    job.execution_permit=permit;return submit(JournalJobKind::AppendControl,permit.fields().policy.authority_epoch,id,tick,payload);
}
}
