// SPDX-License-Identifier: MIT
#include "superpos/canonical_restore.hpp"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <cstring>
#include <thread>

namespace superpos::service {
namespace {
bool same_service(const CheckpointServiceProgress& a,const CheckpointServiceProgress& b) noexcept {
    return a.ticket==b.ticket&&a.revision==b.revision&&a.bytes==b.bytes&&a.cache_order==b.cache_order&&
        a.pages==b.pages&&a.next_table==b.next_table&&a.complete==b.complete&&a.commitment==b.commitment;
}
bool same_plan(const HostRestorePlan& a,const HostRestorePlan& b) noexcept {
    const auto& x=a.checkpoint;const auto& y=b.checkpoint;
    return x.content.ticket==y.content.ticket&&x.content.manifest==y.content.manifest&&x.content.state==y.content.state&&
        same_service(x.service,y.service)&&x.origin==y.origin&&a.final_epoch==b.final_epoch&&a.successor_epoch==b.successor_epoch&&
        a.final_sequence==b.final_sequence&&a.final_tick==b.final_tick&&a.final_state==b.final_state;
}
bool equal_bytes(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    return a.size()==b.size()&&(a.empty()||!std::memcmp(a.data(),b.data(),a.size()));
}
class Driver final:public CanonicalRestore {
    struct Bridge final:HostRestoreAuthority {
        Driver& self;
        explicit Bridge(Driver& d) noexcept:self(d){}
        Status validate_plan(const HostRestorePlan& p) noexcept override {return self.plan_callback(p);}
        Status validate_record(const HostRestorePlan& p,Epoch e,std::uint64_t s,Tick t,std::span<const std::byte> state,std::span<const std::byte> envelope) noexcept override {
            return self.record_callback(p,e,s,t,state,envelope);
        }
    };
    struct Staged { Epoch epoch{};std::uint64_t sequence{};Tick tick{};std::size_t state{},envelope{}; };
    struct Guard {bool& b;explicit Guard(bool& value) noexcept:b(value){b=true;}~Guard(){b=false;}};
    Allocator& allocator_;JournalExecutor& queue_;RestoreReadCapability capability_;
    ClockSource& source_;ContinuityGuard& clock_;
    HostRestoreParticipant& participant_;CryptographicDigest& digest_;const HostRestoreConfig config_;
    Bridge bridge_{*this};AllocatedOwner<HostRestoreStaging> staging_{};Buffer output_;
    std::thread::id owner_{std::this_thread::get_id()};
    CanonicalRestorePhase phase_{CanonicalRestorePhase::Idle};
    std::optional<JournalJobTicket> ticket_{};
    std::optional<CanonicalRestoreEvidence> evidence_{},final_{};
    std::optional<CanonicalStateChain> chain_{};
    std::optional<Staged> record_{};
    HostRestoreStagingTicket stage_{};
    std::uint32_t chunk_{},page_{};std::uint64_t next_{},records_{};
    bool submit_pending_{},entering_{},staged_{},plan_open_{},final_open_{};

    const AuthorityGrant& grant() const noexcept {return capability_.scope().authority.proposal.grant;}
    const JournalPrefix& fenced() const noexcept {return capability_.scope().authority.fenced_prefix;}
    const CheckpointManifest& manifest() const noexcept {return evidence_->plan.checkpoint.content.manifest;}
    std::uint32_t chunks() const noexcept {return static_cast<std::uint32_t>((manifest().bytes+checkpoint_chunk_bytes-1)/checkpoint_chunk_bytes);}
    // The capability lapses with the grant lease or any continuity change.
    Status fresh() noexcept {
        auto now=clock_.observe(source_);if(!now)return fail(now.error());
        if(now->proof_generation!=capability_.proof_generation())return fail(Error::StaleGeneration);
        if(!capability_.current(*now))return fail(Error::Timeout);
        return {};
    }
    // Discard an outstanding job's completion; the executor frees its slot.
    void drop_job() noexcept {if(ticket_){(void)queue_.abandon(*ticket_);ticket_.reset();}}
    Status access() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(entering_)return fail(Error::Busy);return {};}
    CanonicalRestoreProgress progress() const noexcept {return {phase_,chunk_,page_,records_};}
    void release() noexcept {
        if(staged_){staged_=false;(void)staging_->abort(stage_);}
        record_.reset();plan_open_=final_open_=false;
    }
    Status fail_closed(Error error) noexcept {
        release();evidence_.reset();final_.reset();chain_.reset();submit_pending_=false;
        phase_=CanonicalRestorePhase::Failed;return fail(error);
    }
    // Staging rejected a stream; it has already discarded its private state.
    Status rejected(Error error) noexcept {staged_=false;return fail_closed(error);}
    Status submit() noexcept {
        Result<JournalJobTicket> queued=fail(Error::ProtocolViolation);
        switch(phase_){
        case CanonicalRestorePhase::Planning:case CanonicalRestorePhase::Finalizing:queued=queue_.restore_evidence(capability_.scope());break;
        case CanonicalRestorePhase::Checkpoint:queued=queue_.read_checkpoint_chunk(evidence_->plan.checkpoint.content.ticket,chunk_);break;
        case CanonicalRestorePhase::Service:queued=queue_.read_checkpoint_service_page(evidence_->plan.checkpoint.content.ticket,page_);break;
        case CanonicalRestorePhase::Records:queued=queue_.read_committed(grant().epoch,next_);break;
        default:break;
        }
        if(!queued){if(queued.error()==Error::Busy){submit_pending_=true;return {};}return fail_closed(queued.error());}
        submit_pending_=false;ticket_=*queued;return {};
    }
    Status enter_records() noexcept {
        if(manifest().sequence==evidence_->plan.final_sequence){phase_=CanonicalRestorePhase::Finalizing;return submit();}
        next_=manifest().sequence+1;phase_=CanonicalRestorePhase::Records;return submit();
    }
    Status planned(const JournalJobCompletion& done) noexcept {
        if(!done.restore)return fail_closed(Error::ProtocolViolation);
        const auto& ev=*done.restore;const auto& scope=capability_.scope();
        // Storage derived every value; these comparisons bind it to the scope.
        if(ev.plan.checkpoint.content.ticket!=scope.checkpoint||ev.plan.successor_epoch!=grant().epoch||
            ev.plan.final_sequence!=fenced().sequence||ev.plan.final_tick!=fenced().tick||ev.snapshot.sequence!=fenced().sequence||
            ev.snapshot.epoch!=grant().epoch||ev.anchor.position.sequence!=ev.plan.checkpoint.content.manifest.sequence||
            ev.final_state.position.sequence!=fenced().sequence||ev.final_state.result!=ev.plan.final_state)return fail_closed(Error::RecoveryUnavailable);
        auto chain=CanonicalStateChain::create(ev.anchor);if(!chain)return fail_closed(Error::RecoveryUnavailable);
        evidence_=ev;chain_.emplace(*chain);plan_open_=true;
        auto prepared=staging_->prepare(evidence_->plan);plan_open_=false;
        if(!prepared)return rejected(prepared.error());
        stage_=*prepared;staged_=true;chunk_=page_=0;phase_=CanonicalRestorePhase::Checkpoint;return submit();
    }
    Status chunk(const JournalJobCompletion& done) noexcept {
        auto bytes=output_.bytes().first(done.output_bytes);
        if(auto r=staging_->checkpoint_chunk(stage_,chunk_,bytes);!r)return rejected(r.error());
        if(++chunk_<chunks())return submit();
        if(!evidence_->plan.checkpoint.service.pages)return enter_records();
        phase_=CanonicalRestorePhase::Service;return submit();
    }
    Status page(const JournalJobCompletion& done) noexcept {
        auto bytes=output_.bytes().first(done.output_bytes);
        if(auto r=staging_->service_page(stage_,page_,bytes);!r)return rejected(r.error());
        if(++page_<evidence_->plan.checkpoint.service.pages)return submit();
        return enter_records();
    }
    Status record(const JournalJobCompletion& done) noexcept {
        const auto& c=done.committed;
        if(c.receipt.sequence!=next_||c.snapshot.epoch!=grant().epoch||std::uint64_t(c.receipt.bytes)+c.envelope_bytes!=done.output_bytes)return fail_closed(Error::RecoveryUnavailable);
        auto state=output_.bytes().first(c.receipt.bytes);auto envelope=output_.bytes().subspan(c.receipt.bytes,c.envelope_bytes);
        auto decoded=decode_canonical_state(state);
        if(!decoded||decoded->header.position!=CanonicalStatePosition{capability_.match(),c.receipt.epoch,next_,c.receipt.tick})return fail_closed(Error::RecoveryUnavailable);
        if(auto r=chain_->admit(decoded->header);!r)return fail_closed(Error::RecoveryUnavailable);
        record_=Staged{c.receipt.epoch,next_,c.receipt.tick,state.size(),envelope.size()};
        auto accepted=staging_->poststate(stage_,c.receipt.epoch,next_,c.receipt.tick,state,envelope);
        const bool consulted=!record_;record_.reset();
        if(!accepted)return rejected(accepted.error());
        if(!consulted)return fail_closed(Error::ProtocolViolation);
        ++records_;
        if(next_==evidence_->plan.final_sequence){phase_=CanonicalRestorePhase::Finalizing;return submit();}
        ++next_;return submit();
    }
    Status finalized(const JournalJobCompletion& done) noexcept {
        if(!done.restore)return fail_closed(Error::ProtocolViolation);
        const auto& f=*done.restore;
        // A fresh snapshot must reproduce the original plan and final claim,
        // and the records actually staged must end at that same claim.
        if(!same_plan(f.plan,evidence_->plan)||f.anchor!=evidence_->anchor||f.final_state!=evidence_->final_state)return fail_closed(Error::RecoveryUnavailable);
        if(auto r=chain_->finish(f.final_state.position,f.final_state.result);!r)return fail_closed(Error::RecoveryUnavailable);
        final_=f;final_open_=true;auto sealed=staging_->seal(stage_);final_open_=false;
        if(!sealed)return rejected(sealed.error());
        phase_=CanonicalRestorePhase::Sealed;return {};
    }
    Status handle(const JournalJobCompletion& done) noexcept {
        if(done.error!=Error::None)return fail_closed(done.error);
        switch(phase_){
        case CanonicalRestorePhase::Planning:return done.kind==JournalJobKind::RestoreEvidence?planned(done):fail_closed(Error::ProtocolViolation);
        case CanonicalRestorePhase::Checkpoint:return done.kind==JournalJobKind::CheckpointRead?chunk(done):fail_closed(Error::ProtocolViolation);
        case CanonicalRestorePhase::Service:return done.kind==JournalJobKind::CheckpointServiceRead?page(done):fail_closed(Error::ProtocolViolation);
        case CanonicalRestorePhase::Records:return done.kind==JournalJobKind::ReadCommitted?record(done):fail_closed(Error::ProtocolViolation);
        case CanonicalRestorePhase::Finalizing:return done.kind==JournalJobKind::RestoreEvidence?finalized(done):fail_closed(Error::ProtocolViolation);
        default:return fail_closed(Error::ProtocolViolation);
        }
    }
public:
    Driver(Allocator& a,JournalExecutor& q,const RestoreReadCapability& c,HostRestoreParticipant& p,CryptographicDigest& d,
        ClockSource& source,ContinuityGuard& clock,HostRestoreConfig config) noexcept
        :allocator_(a),queue_(q),capability_(c),source_(source),clock_(clock),participant_(p),digest_(d),config_(config),output_(a,MemoryDomain::Recovery){}
    ~Driver() override {
        if(owner_!=std::this_thread::get_id()||entering_)std::abort();
        entering_=true;drop_job();release();
    }
    Status initialize() noexcept {
        if(auto r=output_.resize(std::max<std::size_t>(config_.maximum_record_bytes,checkpoint_chunk_bytes));!r)return r;
        auto staging=create_host_restore_staging(allocator_,participant_,bridge_,digest_,config_);if(!staging)return fail(staging.error());
        staging_=std::move(*staging);return {};
    }
    // Private authority callbacks: exact comparison with staged evidence only.
    Status plan_callback(const HostRestorePlan& p) noexcept {
        if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
        if(auto r=fresh();!r)return r;
        if(plan_open_&&evidence_&&same_plan(p,evidence_->plan)){plan_open_=false;return {};}
        if(final_open_&&final_&&same_plan(p,final_->plan)){final_open_=false;return {};}
        return fail(Error::PermissionDenied);
    }
    Status record_callback(const HostRestorePlan& p,Epoch e,std::uint64_t s,Tick t,std::span<const std::byte> state,std::span<const std::byte> envelope) noexcept {
        if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
        if(!record_||!evidence_||!same_plan(p,evidence_->plan))return fail(Error::PermissionDenied);
        if(auto r=fresh();!r){record_.reset();return r;}
        const auto staged=*record_;record_.reset();
        if(e!=staged.epoch||s!=staged.sequence||t!=staged.tick||
            !equal_bytes(state,output_.bytes().first(staged.state))||
            !equal_bytes(envelope,output_.bytes().subspan(staged.state,staged.envelope)))return fail(Error::RecoveryUnavailable);
        return {};
    }
    Status start() noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);
        if(phase_!=CanonicalRestorePhase::Idle)return fail(Error::NotReady);
        if(auto r=fresh();!r){phase_=CanonicalRestorePhase::Failed;return r;}
        phase_=CanonicalRestorePhase::Planning;return submit();
    }
    Result<CanonicalRestoreProgress> poll() noexcept override {
        if(auto r=access();!r)return fail(r.error());Guard guard(entering_);
        if(phase_==CanonicalRestorePhase::Idle)return fail(Error::NotReady);
        if(phase_==CanonicalRestorePhase::Sealed||phase_==CanonicalRestorePhase::Failed||phase_==CanonicalRestorePhase::Aborted)return progress();
        if(auto r=fresh();!r){drop_job();return fail(fail_closed(r.error()).error());}
        if(!ticket_){
            if(submit_pending_){if(auto r=submit();!r)return fail(r.error());if(submit_pending_)return fail(Error::Busy);}
            return progress();
        }
        auto done=queue_.consume(*ticket_,output_.bytes());
        if(!done){
            if(done.error()==Error::Busy)return fail(Error::Busy);
            drop_job();return fail(fail_closed(done.error()).error());
        }
        ticket_.reset();
        if(auto r=handle(*done);!r)return fail(r.error());
        return progress();
    }
    // Adopt the successor capability minted after a renewal of the same grant
    // lineage: same match, checkpoint, bound, boot, epoch, owner, kind, scope,
    // membership and exact fenced prefix, with a newer grant sequence.
    Status renew(const RestoreReadCapability& next) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);
        if(phase_==CanonicalRestorePhase::Sealed||phase_==CanonicalRestorePhase::Failed||phase_==CanonicalRestorePhase::Aborted)return fail(Error::NotReady);
        const auto& a=capability_.scope();const auto& b=next.scope();const auto& x=a.authority.proposal.grant;const auto& y=b.authority.proposal.grant;
        const auto& f=a.authority.fenced_prefix;const auto& g=b.authority.fenced_prefix;
        if(next.match()!=capability_.match()||b.checkpoint!=a.checkpoint||b.maximum_records!=a.maximum_records||b.authority.boot!=a.authority.boot||
            y.epoch!=x.epoch||y.owner!=x.owner||y.kind!=x.kind||y.scope!=x.scope||y.membership_generation!=x.membership_generation||
            y.coordinator_term!=x.coordinator_term||y.grant_sequence<=x.grant_sequence||
            g.epoch!=f.epoch||g.sequence!=f.sequence||g.tick!=f.tick||g.retained_bytes!=f.retained_bytes)return fail(Error::InvalidArgument);
        const auto previous=capability_;capability_=next;
        if(auto r=fresh();!r){capability_=previous;return r;}
        return {};
    }
    Result<HostRestorePlan> sealed_plan() const noexcept override {
        if(auto r=access();!r)return fail(r.error());
        if(phase_!=CanonicalRestorePhase::Sealed||!evidence_)return fail(Error::NotReady);return evidence_->plan;
    }
    Status abort() noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);
        drop_job();submit_pending_=false;release();evidence_.reset();final_.reset();chain_.reset();
        phase_=CanonicalRestorePhase::Aborted;return {};
    }
};
}
Result<AllocatedOwner<CanonicalRestore>> create_canonical_restore(Allocator& allocator,JournalExecutor& queue,
    const RestoreReadCapability& capability,HostRestoreParticipant& participant,CryptographicDigest& digest,
    ClockSource& source,ContinuityGuard& clock,HostRestoreConfig config) noexcept {
    const auto& scope=capability.scope();
    if(!capability.match()||scope.checkpoint.match!=capability.match()||!scope.maximum_records||
        scope.maximum_records>config.maximum_journal_records)return fail(Error::InvalidArgument);
    auto owner=AllocatedOwner<CanonicalRestore>::create<Driver>(allocator,MemoryDomain::Recovery,allocator,queue,capability,participant,digest,source,clock,config);
    if(!owner)return fail(owner.error());
    if(auto r=static_cast<Driver*>(owner->get())->initialize();!r)return fail(r.error());
    return owner;
}
}
