// SPDX-License-Identifier: MIT
#include "superpos/host_restore.hpp"
#include "superpos/journal_envelope.hpp"
#include "executor_identity.hpp"
#include <algorithm>
#include <cstdlib>
#include <thread>

namespace superpos {
namespace {
std::atomic<std::uint64_t> next_restore_identity{1};
enum class Phase { Idle,Active,Sealed };
class Restore final:public HostRestoreStaging {
    Allocator& allocator_;HostRestoreParticipant& participant_;HostRestoreAuthority& authority_;CryptographicDigest& digest_;
    HostRestoreConfig config_;Buffer input_;std::thread::id owner_{std::this_thread::get_id()};
    std::uint64_t incarnation_{},generation_{},sequence_{};Epoch epoch_{};Tick tick_{};
    HostRestorePlan plan_{};std::optional<CheckpointServiceVerifier> service_{};
    std::array<Fingerprint,checkpoint_maximum_chunks> chunks_{};std::uint32_t chunk_{};
    Phase phase_{Phase::Idle};bool entering_{},participant_active_{};
    struct Guard {bool& b;explicit Guard(bool& value):b(value){b=true;}~Guard(){b=false;}};
    Status access() const noexcept {if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(entering_)return fail(Error::Busy);return {};}
    Status ticket(HostRestoreStagingTicket t) const noexcept {if(!t.generation||t.incarnation!=incarnation_||t.generation!=generation_||phase_==Phase::Idle)return fail(Error::StaleGeneration);return {};}
    void discard() noexcept {phase_=Phase::Idle;service_.reset();if(participant_active_){participant_active_=false;participant_.discard();}}
    Status stop(Error e) noexcept {discard();return fail(e);}
    Result<HostRestoreStagingTicket> reject(Error e) noexcept {discard();return fail(e);}
    bool checkpoint_complete() const noexcept {return chunk_==(plan_.checkpoint.content.manifest.bytes+checkpoint_chunk_bytes-1)/checkpoint_chunk_bytes;}
public:
    Restore(Allocator& a,HostRestoreParticipant& p,HostRestoreAuthority& authority,CryptographicDigest& d,HostRestoreConfig c,std::uint64_t id) noexcept
        :allocator_(a),participant_(p),authority_(authority),digest_(d),config_(c),input_(a,MemoryDomain::Recovery),incarnation_(id){}
    ~Restore() override {if(owner_!=std::this_thread::get_id()||entering_)std::abort();entering_=true;discard();}
    Status initialize() noexcept {return input_.resize(std::max<std::size_t>(config_.maximum_record_bytes,checkpoint_chunk_bytes));}
    Result<HostRestoreStagingTicket> prepare(const HostRestorePlan& supplied) noexcept override {
        if(auto r=access();!r)return fail(r.error());Guard guard(entering_);if(phase_!=Phase::Idle)return fail(Error::Busy);
        const HostRestorePlan p=supplied; // Before any provider callback.
        if(generation_==UINT64_MAX)return fail(Error::CounterExhausted);++generation_;
        const auto& m=p.checkpoint.content.manifest;
        if(!m.epoch||!m.bytes||m.bytes>config_.maximum_checkpoint_bytes||p.final_epoch<m.epoch||p.successor_epoch<=p.final_epoch||p.final_sequence<m.sequence||p.final_tick<m.tick||
           p.final_sequence-m.sequence>config_.maximum_journal_records||!known_fingerprint(p.final_state))return fail(Error::InvalidArgument);
        const auto caps=participant_.capabilities();
        if(caps.schemas!=m.schemas||caps.simulation!=m.simulation||caps.participants!=m.participants)return fail(Error::IncompatibleSchema);
        if(caps.grade<m.grade||m.grade==RecoveryGrade::None||unsigned(caps.grade)>3||m.bytes>caps.maximum_checkpoint_bytes||
           !caps.maximum_record_bytes||caps.maximum_record_bytes<config_.maximum_record_bytes||p.final_sequence-m.sequence>caps.maximum_journal_records||
           (p.final_epoch!=m.epoch&&!caps.cross_epoch))return fail(Error::Unsupported);
        auto inspected=CheckpointServiceVerifier::create(p.checkpoint,digest_);if(!inspected)return fail(inspected.error());
        if(auto r=authority_.validate_plan(p);!r)return fail(r.error());
        plan_=p;service_=std::move(*inspected);chunk_=0;sequence_=m.sequence;epoch_=m.epoch;tick_=m.tick;phase_=Phase::Active;participant_active_=true;
        if(auto r=participant_.prepare(plan_,allocator_);!r)return reject(r.error());
        return HostRestoreStagingTicket{generation_,incarnation_};
    }
    Status checkpoint_chunk(HostRestoreStagingTicket t,std::uint32_t ordinal,std::span<const std::byte> bytes) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);if(auto r=ticket(t);!r)return r;
        if(phase_!=Phase::Active||ordinal!=chunk_||checkpoint_complete())return stop(Error::ProtocolViolation);
        const auto total=plan_.checkpoint.content.manifest.bytes,offset=std::uint64_t(chunk_)*checkpoint_chunk_bytes;
        if(bytes.size()!=std::min<std::uint64_t>(checkpoint_chunk_bytes,total-offset))return stop(Error::InvalidArgument);
        std::copy(bytes.begin(),bytes.end(),input_.bytes().begin());const auto copy=input_.bytes().first(bytes.size());Fingerprint hash{};
        if(digest_.algorithm()!=DigestAlgorithm::Sha256)return stop(Error::Unsupported);
        if(auto r=digest_.hash(copy,hash);!r)return stop(r.error());
        if(digest_.algorithm()!=DigestAlgorithm::Sha256)return stop(Error::Unsupported);
        if(auto r=participant_.checkpoint_chunk(ordinal,copy);!r)return stop(r.error());chunks_[chunk_++]=hash;
        if(checkpoint_complete()){auto root=checkpoint_content_commitment(plan_.checkpoint.content.manifest,std::span(chunks_).first(chunk_),digest_);if(!root)return stop(root.error());if(digest_.algorithm()!=DigestAlgorithm::Sha256)return stop(Error::Unsupported);if(*root!=plan_.checkpoint.content.manifest.content)return stop(Error::RecoveryUnavailable);}
        return {};
    }
    Status service_page(HostRestoreStagingTicket t,std::uint32_t ordinal,std::span<const std::byte> bytes) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);if(auto r=ticket(t);!r)return r;
        if(phase_!=Phase::Active||bytes.size()>checkpoint_chunk_bytes)return stop(Error::ProtocolViolation);
        std::copy(bytes.begin(),bytes.end(),input_.bytes().begin());auto r=service_->accept(ordinal,input_.bytes().first(bytes.size()));return r?r:stop(r.error());
    }
    Status poststate(HostRestoreStagingTicket t,Epoch epoch,std::uint64_t sequence,Tick tick,std::span<const std::byte> state,std::span<const std::byte> envelope) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);if(auto r=ticket(t);!r)return r;
        if(phase_!=Phase::Active||!checkpoint_complete()||sequence_==UINT64_MAX||sequence!=sequence_+1||sequence>plan_.final_sequence||epoch<epoch_||epoch>plan_.final_epoch||tick<tick_||tick>plan_.final_tick||
           state.size()>config_.maximum_record_bytes||envelope.size()>config_.maximum_record_bytes-state.size())return stop(Error::ProtocolViolation);
        std::copy(state.begin(),state.end(),input_.bytes().begin());std::copy(envelope.begin(),envelope.end(),input_.bytes().begin()+state.size());
        auto copied_state=input_.bytes().first(state.size()),copied_envelope=input_.bytes().subspan(state.size(),envelope.size());
        if(auto r=decode_journal_envelope(plan_.checkpoint.content.ticket.match,copied_envelope);!r)return stop(r.error());
        if(auto r=authority_.validate_record(plan_,epoch,sequence,tick,copied_state,copied_envelope);!r)return stop(r.error());
        if(auto r=participant_.poststate(epoch,sequence,tick,copied_state,copied_envelope);!r)return stop(r.error());
        epoch_=epoch;sequence_=sequence;tick_=tick;return {};
    }
    Status seal(HostRestoreStagingTicket t) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);if(auto r=ticket(t);!r)return r;
        if(phase_!=Phase::Active||!checkpoint_complete()||sequence_!=plan_.final_sequence||epoch_!=plan_.final_epoch||tick_!=plan_.final_tick)return stop(Error::RecoveryUnavailable);
        if(auto r=service_->finish();!r)return stop(r.error());if(auto r=authority_.validate_plan(plan_);!r)return stop(r.error());
        auto final=participant_.seal();if(!final)return stop(final.error());if(*final!=plan_.final_state)return stop(Error::RecoveryUnavailable);phase_=Phase::Sealed;return {};
    }
    Status abort(HostRestoreStagingTicket t) noexcept override {
        if(auto r=access();!r)return r;Guard guard(entering_);if(auto r=ticket(t);!r)return r;discard();return {};
    }
};
}
Result<AllocatedOwner<HostRestoreStaging>> create_host_restore_staging(Allocator& a,HostRestoreParticipant& p,HostRestoreAuthority& authority,CryptographicDigest& digest,HostRestoreConfig c) noexcept {
    if(!c.maximum_checkpoint_bytes||c.maximum_checkpoint_bytes>checkpoint_maximum_bytes||!c.maximum_record_bytes||c.maximum_record_bytes>1024*1024||!c.maximum_journal_records||c.maximum_journal_records>1000000)return fail(Error::InvalidArgument);
    auto id=detail::reserve_executor_identity(next_restore_identity);if(!id)return fail(id.error());
    auto owner=AllocatedOwner<HostRestoreStaging>::create<Restore>(a,MemoryDomain::Recovery,a,p,authority,digest,c,*id);if(!owner)return fail(owner.error());
    if(auto r=static_cast<Restore*>(owner->get())->initialize();!r)return fail(r.error());return owner;
}
}
