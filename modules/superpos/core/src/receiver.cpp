#include "superpos/receiver.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <atomic>

namespace superpos {
namespace {
Result<std::uint64_t> new_receiver_instance() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto current=next.load(std::memory_order_relaxed);
    for(;;) {
        if(current==UINT64_MAX)return fail(Error::CounterExhausted);
        if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current;
    }
}
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
std::array<std::uint64_t,6> key_words(const ReplicaKey& key) noexcept {
    return {key.slot,key.incarnation,key.authority_epoch,key.connection_epoch,key.replica_epoch,key.encoding_epoch};
}
}
PeerReplicaReceiver::PeerReplicaReceiver(ReceiverConfig c,FrozenRegistry registry,std::span<ReceiverRecord> records,
    std::span<std::byte> images,std::span<std::byte> scratch) noexcept:
    config_(c),registry_(std::move(registry)),records_(records),images_(images),scratch_(scratch),control_applied_(c.first_control_sequence-1) {
    for(auto& r:records_)r={}; std::fill(images_.begin(),images_.end(),std::byte{});
}
Result<PeerReplicaReceiver> PeerReplicaReceiver::create(ReceiverConfig c,FrozenRegistry registry,std::span<ReceiverRecord> records,
    std::span<std::byte> images,std::span<std::byte> scratch) noexcept {
    if(!c.authority_epoch || !c.connection_epoch || !c.replica_epoch || !c.first_control_sequence || !c.maximum_active || c.maximum_active>1000 ||
        !c.maximum_transitions || c.maximum_transitions>64 || !c.state_stride || c.state_stride>Schema::maximum_state_bytes ||
        records.empty() || records.size()>UINT16_MAX || c.maximum_active>records.size() || !registry.size() || !known_fingerprint(registry.fingerprint()))return fail(Error::InvalidArgument);
    if(records.size()>images.size()/c.state_stride/3 || scratch.size()<c.state_stride)return fail(Error::CapacityExceeded);
    const auto record_bytes=std::as_bytes(records);
    if(overlap(record_bytes,images) || overlap(record_bytes,scratch) || overlap(images,scratch))return fail(Error::InvalidArgument);
    std::array<Schema,SchemaRegistry::maximum_schemas> views{};
    if(auto materialized=registry.materialize(views);!materialized)return fail(materialized.error());
    for(std::size_t i=0;i<registry.size();++i) {
        auto found=registry.find(views[i].id()); if(!found)return fail(found.error()); const auto descriptor=std::as_bytes(std::span(*found,1));
        if(overlap(descriptor,record_bytes) || overlap(descriptor,images) || overlap(descriptor,scratch) ||
            overlap(std::as_bytes(views[i].fields()),record_bytes) || overlap(std::as_bytes(views[i].fields()),images) || overlap(std::as_bytes(views[i].fields()),scratch) ||
            overlap(std::as_bytes(views[i].rpcs()),record_bytes) || overlap(std::as_bytes(views[i].rpcs()),images) || overlap(std::as_bytes(views[i].rpcs()),scratch))return fail(Error::InvalidArgument);
    }
    auto instance=new_receiver_instance(); if(!instance)return fail(instance.error());
    PeerReplicaReceiver result(c,std::move(registry),records,images.first(records.size()*c.state_stride*3),scratch.first(std::min(scratch.size(),maximum_group_bytes)));
    result.instance_=*instance; return result;
}
PeerReplicaReceiver::PeerReplicaReceiver(PeerReplicaReceiver&& other) noexcept:
    config_(other.config_),registry_(std::move(other.registry_)),records_(std::exchange(other.records_,{})),images_(std::exchange(other.images_,{})),scratch_(std::exchange(other.scratch_,{})),
    active_(std::exchange(other.active_,0)),transitions_(std::exchange(other.transitions_,0)),control_applied_(other.control_applied_),instance_(std::exchange(other.instance_,0)),controls_(other.controls_),owner_(other.owner_) {}
PeerReplicaReceiver& PeerReplicaReceiver::operator=(PeerReplicaReceiver&& other) noexcept {
    if(this!=&other) {
        config_=other.config_; registry_=std::move(other.registry_); records_=std::exchange(other.records_,{}); images_=std::exchange(other.images_,{}); scratch_=std::exchange(other.scratch_,{});
        active_=std::exchange(other.active_,0); transitions_=std::exchange(other.transitions_,0); control_applied_=other.control_applied_; instance_=std::exchange(other.instance_,0); controls_=other.controls_; owner_=other.owner_; applying_=false;
    } return *this;
}
Status PeerReplicaReceiver::available() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(records_.empty() || !registry_.size())return fail(Error::NotReady); if(applying_)return fail(Error::Busy); return {};
}
Result<bool> PeerReplicaReceiver::storage_overlaps(std::span<const std::byte> bytes) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(overlap(bytes,std::as_bytes(std::span(this,1)))||overlap(bytes,std::as_bytes(records_))||overlap(bytes,images_)||overlap(bytes,scratch_))return true;
    std::array<Schema,SchemaRegistry::maximum_schemas> views{};if(auto materialized=registry_.materialize(views);!materialized)return fail(materialized.error());
    for(std::size_t n=0;n<registry_.size();++n){auto catalog=registry_.find(views[n].id());if(!catalog)return fail(catalog.error());if(overlap(bytes,std::as_bytes(std::span(*catalog,1)))||overlap(bytes,std::as_bytes(views[n].fields()))||overlap(bytes,std::as_bytes(views[n].rpcs())))return true;}
    return false;
}
Result<std::size_t> PeerReplicaReceiver::index(const ReplicaKey& k) const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(k.authority_epoch!=config_.authority_epoch || k.connection_epoch!=config_.connection_epoch || k.replica_epoch!=config_.replica_epoch)return fail(Error::StaleEpoch);
    if(!k.slot || k.slot>records_.size())return fail(Error::StaleGeneration);
    const auto i=std::size_t(k.slot-1); const auto& r=records_[i];
    if(r.phase==ReceiverPhase::Empty || r.incarnation!=k.incarnation)return fail(Error::StaleGeneration);
    if(r.encoding_epoch!=k.encoding_epoch)return fail(Error::StaleEpoch); return i;
}
ReplicaKey PeerReplicaReceiver::key(std::size_t i) const noexcept {
    const auto& r=records_[i]; return {static_cast<std::uint16_t>(i+1),r.incarnation,config_.authority_epoch,config_.connection_epoch,config_.replica_epoch,r.encoding_epoch};
}
std::span<std::byte> PeerReplicaReceiver::image(std::size_t i,std::size_t plane) noexcept { return images_.subspan((3*i+plane)*config_.state_stride,records_[i].catalog->schema.state_bytes()); }
std::span<const std::byte> PeerReplicaReceiver::image(std::size_t i,std::size_t plane) const noexcept { return images_.subspan((3*i+plane)*config_.state_stride,records_[i].catalog->schema.state_bytes()); }
CanonicalReplica PeerReplicaReceiver::view(std::size_t i) const noexcept {
    const auto& r=records_[i]; return {key(i),r.handle,&r.catalog->schema,r.owner,r.ownership_revision,r.current_revision,r.current_tick,image(i,0)};
}
PeerReplicaReceiver::ControlStamp PeerReplicaReceiver::stamp(std::uint8_t kind,std::uint64_t sequence,const ReplicaKey& key,std::span<const std::uint64_t> extra) noexcept {
    ControlStamp result; result.sequence=sequence; result.kind=kind; result.size=static_cast<std::uint8_t>(6+extra.size());
    const auto words=key_words(key); std::copy(words.begin(),words.end(),result.words.begin()); std::copy(extra.begin(),extra.end(),result.words.begin()+6); return result;
}
Result<bool> PeerReplicaReceiver::control(const ControlStamp& proposed) const noexcept {
    if(!proposed.sequence)return fail(Error::InvalidArgument);
    if(proposed.sequence<=control_applied_) {
        const auto& remembered=controls_[(proposed.sequence-1)%controls_.size()];
        if(remembered.sequence==proposed.sequence && (remembered.kind!=proposed.kind || remembered.size!=proposed.size || remembered.words!=proposed.words))return fail(Error::ProtocolViolation);
        return false;
    }
    if(control_applied_==UINT64_MAX)return fail(Error::CounterExhausted);
    if(proposed.sequence!=control_applied_+1)return fail(Error::Busy); return true;
}
void PeerReplicaReceiver::acknowledge_control(ControlStamp stamp) noexcept { control_applied_=stamp.sequence; controls_[(stamp.sequence-1)%controls_.size()]=stamp; }
Status PeerReplicaReceiver::projected(const ReceiverRecord& r,std::span<const std::byte> state) const noexcept {
    if(auto canonical=r.catalog->schema.validate(state);!canonical)return canonical;
    for(const auto& field:r.catalog->schema.fields())if(field.audience==FieldAudience::Authority || (field.audience==FieldAudience::Owner && config_.peer!=r.owner))
        for(auto b:state.subspan(field.offset,field.size))if(b!=std::byte{})return fail(Error::PermissionDenied);
    return {};
}
Status PeerReplicaReceiver::group(std::span<const std::size_t> members,std::uint64_t id,std::size_t bytes) const noexcept {
    if(members.empty() || members.size()>maximum_group_objects || bytes>maximum_group_bytes)return fail(Error::CapacityExceeded);
    for(auto i:members) {
        const auto& c=records_[i].catalog->contract;
        if(c.publication==PublicationSemantics::PerObject) { if(id || members.size()!=1)return fail(Error::IncompatibleSchema); }
        else if(id!=c.group_id)return fail(Error::IncompatibleSchema);
        if(members.size()>c.maximum_group_objects || bytes>c.maximum_group_bytes)return fail(Error::CapacityExceeded);
    } return {};
}
Status PeerReplicaReceiver::application(ReplicaApplication& application,ReplicaChangeKind kind,std::span<const CanonicalReplica> states) noexcept {
    applying_=true; auto staged=application.stage(kind,states);
    if(!staged) { application.abort(); applying_=false; return staged; }
    application.commit(); applying_=false; return {};
}
Result<LifecycleAppliedReceipt> PeerReplicaReceiver::bind(const ViewBind& bind) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(bind.key.authority_epoch!=config_.authority_epoch || bind.key.connection_epoch!=config_.connection_epoch || bind.key.replica_epoch!=config_.replica_epoch)return fail(Error::StaleEpoch);
    if(!bind.key.slot || bind.key.slot>records_.size() || !bind.key.incarnation || !bind.key.encoding_epoch || !bind.handle || !bind.ownership_revision)return fail(Error::InvalidArgument);
    const std::array extra{bind.handle.value,bind.schema,bind.owner,bind.ownership_revision}; const auto metadata=stamp(1,bind.lifecycle_sequence,bind.key,extra);
    auto fresh=control(metadata); if(!fresh)return fail(fresh.error()); if(!*fresh)return LifecycleAppliedReceipt{control_applied_};
    const auto i=std::size_t(bind.key.slot-1); auto& r=records_[i];
    if(r.phase!=ReceiverPhase::Empty)return fail(Error::Busy); if(bind.key.incarnation<=r.incarnation)return fail(Error::StaleGeneration);
    if(active_>=config_.maximum_active || transitions_>=config_.maximum_transitions)return fail(Error::CapacityExceeded);
    for(const auto& existing:records_)if(existing.phase!=ReceiverPhase::Empty && existing.handle.slot()==bind.handle.slot())return fail(Error::StaleGeneration);
    auto schema=registry_.find(bind.schema); if(!schema)return fail(schema.error()); if((*schema)->schema.state_bytes()>config_.state_stride)return fail(Error::CapacityExceeded);
    r={}; r.phase=ReceiverPhase::SpawnPending; r.handle=bind.handle; r.catalog=*schema; r.owner=bind.owner; r.ownership_revision=bind.ownership_revision;
    r.incarnation=bind.key.incarnation; r.encoding_epoch=bind.key.encoding_epoch; r.bind_sequence=bind.lifecycle_sequence;
    std::fill(images_.begin()+static_cast<std::ptrdiff_t>(3*i*config_.state_stride),images_.begin()+static_cast<std::ptrdiff_t>((3*i+3)*config_.state_stride),std::byte{});
    ++active_; ++transitions_; acknowledge_control(metadata); return LifecycleAppliedReceipt{control_applied_};
}
Result<BaselinePinnedReceipt> PeerReplicaReceiver::offer(const BaselineOffer& offer,Tick tick) noexcept {
    auto found=index(offer.key); if(!found)return fail(found.error()); const auto i=*found; auto& r=records_[i];
    if(!offer.candidate_token || !offer.revision || overlap(offer.canonical,std::as_bytes(records_)))return fail(Error::InvalidArgument);
    if(auto allowed=projected(r,offer.canonical);!allowed)return fail(allowed.error());
    for(std::size_t image=0;image<2;++image)if(r.images[image].pinned && r.images[image].token==offer.candidate_token) {
        const auto& known=r.images[image];
        if(known.revision!=offer.revision || known.tick!=tick || !std::equal(offer.canonical.begin(),offer.canonical.end(),this->image(i,image+1).begin()))return fail(Error::ProtocolViolation);
        return BaselinePinnedReceipt{offer.key,offer.candidate_token,offer.revision};
    }
    if(offer.candidate_token<=r.highest_token)return fail(Error::StaleGeneration);
    if(r.phase==ReceiverPhase::SpawnPending && r.active_image>=0)return fail(Error::Busy);
    const auto base=r.active_image<0?std::uint64_t{0}:r.images[static_cast<std::size_t>(r.active_image)].token;
    if(offer.base_token!=base)return fail(Error::MissingBaseline);
    if(!base && offer.revision<r.current_revision)return fail(Error::InvalidArgument);
    std::size_t target=0; while(target<2 && r.images[target].pinned)++target; if(target==2)return fail(Error::CapacityExceeded);
    std::memmove(image(i,target+1).data(),offer.canonical.data(),offer.canonical.size()); r.images[target]={offer.candidate_token,offer.revision,tick,true};
    r.active_image=static_cast<std::int8_t>(target); r.highest_token=offer.candidate_token; return BaselinePinnedReceipt{offer.key,offer.candidate_token,offer.revision};
}
Result<SpawnAppliedReceipt> PeerReplicaReceiver::spawn_ready(const ReplicaKey& k,ReplicaApplication& app) noexcept {
    auto found=index(k); if(!found)return fail(found.error()); const auto i=*found; auto& r=records_[i];
    if(r.phase==ReceiverPhase::Ready) {
        if(k.encoding_epoch!=r.initial_encoding_epoch)return fail(Error::NotReady);
        return SpawnAppliedReceipt{k,r.initial_applied_revision,r.initial_ownership_revision};
    }
    if(r.active_image<0)return fail(Error::NotReady); const auto image_index=static_cast<std::size_t>(r.active_image); const auto& initial=r.images[image_index];
    auto staged=view(i); staged.revision=initial.revision; staged.tick=initial.tick; staged.canonical=image(i,image_index+1);
    const std::array changes{staged}; if(auto applied=application(app,ReplicaChangeKind::Spawn,changes);!applied)return fail(applied.error());
    std::memmove(image(i,0).data(),staged.canonical.data(),staged.canonical.size()); r.current_revision=initial.revision; r.current_tick=initial.tick;
    r.phase=ReceiverPhase::Ready; r.repair_required=false; r.initial_applied_revision=r.current_revision; r.initial_ownership_revision=r.ownership_revision; r.initial_encoding_epoch=k.encoding_epoch;
    --transitions_; return SpawnAppliedReceipt{k,r.current_revision,r.ownership_revision};
}
Result<PublicationResult> PeerReplicaReceiver::publish(std::span<const StatePatch> patches,std::uint64_t group_id,Tick tick,
    std::span<StateAppliedReceipt> receipts,std::span<RepairRequest> repairs,ReplicaApplication& app) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(patches.empty() || patches.size()>maximum_group_objects || receipts.size()<patches.size() || repairs.size()<patches.size())return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(patches),images_) || overlap(std::as_bytes(patches),scratch_) || overlap(std::as_bytes(patches),std::as_bytes(records_)) ||
        overlap(std::as_bytes(receipts),images_) || overlap(std::as_bytes(receipts),scratch_) || overlap(std::as_bytes(receipts),std::as_bytes(records_)) ||
        overlap(std::as_bytes(repairs),images_) || overlap(std::as_bytes(repairs),scratch_) || overlap(std::as_bytes(repairs),std::as_bytes(records_)) || overlap(std::as_bytes(receipts),std::as_bytes(repairs)) ||
        overlap(std::as_bytes(patches),std::as_bytes(receipts)) || overlap(std::as_bytes(patches),std::as_bytes(repairs)))return fail(Error::InvalidArgument);
    std::array<std::size_t,maximum_group_objects> members{},offsets{}; std::array<std::int8_t,maximum_group_objects> bases{}; std::array<CanonicalReplica,maximum_group_objects> staged{};
    std::size_t bytes=0,encoded_bytes=0,stale=0; bool missing=false,reset_required=false;
    for(std::size_t n=0;n<patches.size();++n) {
        const auto& patch=patches[n]; if(!patch.revision || patch.revision!=patches[0].revision || !patch.baseline_token)return fail(Error::InvalidArgument);
        auto found=index(patch.key); if(!found)return fail(found.error()); const auto i=*found; const auto& r=records_[i]; members[n]=i;
        if(r.phase!=ReceiverPhase::Ready)return fail(Error::NotReady);
        for(std::size_t p=0;p<n;++p)if(members[p]==i)return fail(Error::InvalidArgument);
        if(tick<r.current_tick && patch.revision>r.current_revision)return fail(Error::InvalidArgument);
        if(overlap(patch.delta,images_) || overlap(patch.delta,scratch_) || overlap(patch.delta,std::as_bytes(records_)) ||
            overlap(patch.delta,std::as_bytes(receipts)) || overlap(patch.delta,std::as_bytes(repairs)))return fail(Error::InvalidArgument);
        if(patch.delta.size()>maximum_group_bytes-encoded_bytes)return fail(Error::CapacityExceeded); encoded_bytes+=patch.delta.size();
        // Structural field/mask/type validation does not depend on possession of
        // the named baseline. Invalid stale or missing-base records are rejected
        // before an idempotent receipt or reliable repair request is produced.
        if(auto canonical=validate_delta(r.catalog->schema,image(i,0),patch.delta);!canonical)return fail(canonical.error());
        offsets[n]=bytes; const auto size=r.catalog->schema.state_bytes(); if(size>scratch_.size()-bytes)return fail(Error::CapacityExceeded); bytes+=size;
        if(patch.revision<=r.current_revision)++stale;
        bases[n]=-1; for(std::size_t base=0;base<2;++base)if(r.images[base].pinned && r.images[base].token==patch.baseline_token)bases[n]=static_cast<std::int8_t>(base);
        missing=missing || bases[n]<0 || r.repair_required;
        reset_required=reset_required || r.repair_required;
    }
    if(auto contract=group(std::span(members).first(patches.size()),group_id,bytes);!contract)return fail(contract.error());
    if(stale==patches.size() && !reset_required) {
        for(std::size_t n=0;n<patches.size();++n)receipts[n]={patches[n].key,records_[members[n]].current_revision};
        return PublicationResult{PublicationDisposition::Duplicate,static_cast<std::uint16_t>(patches.size()),0};
    }
    if(missing || stale) {
        for(std::size_t n=0;n<patches.size();++n)repairs[n]={patches[n].key,patches[n].baseline_token,records_[members[n]].current_revision};
        return PublicationResult{PublicationDisposition::NeedRepair,0,static_cast<std::uint16_t>(patches.size())};
    }
    for(std::size_t n=0;n<patches.size();++n) {
        const auto i=members[n]; const auto& r=records_[i]; auto target=scratch_.subspan(offsets[n],r.catalog->schema.state_bytes());
        if(auto decoded=apply_delta(r.catalog->schema,image(i,static_cast<std::size_t>(bases[n])+1),patches[n].delta,target);!decoded)return fail(decoded.error());
        if(auto allowed=projected(r,target);!allowed)return fail(allowed.error()); staged[n]=view(i); staged[n].revision=patches[n].revision; staged[n].tick=tick; staged[n].canonical=target;
    }
    if(auto applied=application(app,ReplicaChangeKind::Publication,std::span(staged).first(patches.size()));!applied)return fail(applied.error());
    for(std::size_t n=0;n<patches.size();++n) { auto& r=records_[members[n]]; std::memmove(image(members[n],0).data(),staged[n].canonical.data(),staged[n].canonical.size()); r.current_revision=staged[n].revision; r.current_tick=tick; receipts[n]={patches[n].key,r.current_revision}; }
    return PublicationResult{PublicationDisposition::Applied,static_cast<std::uint16_t>(patches.size()),0};
}
Result<PublicationResult> PeerReplicaReceiver::repair(std::span<const FullRepair> repairs,std::uint64_t group_id,Tick tick,
    std::span<StateAppliedReceipt> receipts,ReplicaApplication& app) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(repairs.empty() || repairs.size()>maximum_group_objects || receipts.size()<repairs.size())return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(repairs),images_) || overlap(std::as_bytes(repairs),scratch_) || overlap(std::as_bytes(repairs),std::as_bytes(records_)) ||
        overlap(std::as_bytes(receipts),images_) || overlap(std::as_bytes(receipts),scratch_) || overlap(std::as_bytes(receipts),std::as_bytes(records_)) || overlap(std::as_bytes(repairs),std::as_bytes(receipts)))return fail(Error::InvalidArgument);
    std::array<std::size_t,maximum_group_objects> members{},offsets{}; std::array<CanonicalReplica,maximum_group_objects> staged{}; std::size_t bytes=0,duplicates=0; bool superseded=false;
    for(std::size_t n=0;n<repairs.size();++n) {
        const auto& repair=repairs[n]; if(!repair.revision || repair.revision!=repairs[0].revision)return fail(Error::InvalidArgument);
        auto found=index(repair.key); if(!found)return fail(found.error()); const auto i=*found; const auto& r=records_[i]; members[n]=i;
        if(r.phase!=ReceiverPhase::Ready)return fail(Error::NotReady);
        for(std::size_t p=0;p<n;++p)if(members[p]==i)return fail(Error::InvalidArgument);
        if(repair.revision>r.current_revision && tick<r.current_tick)return fail(Error::InvalidArgument);
        superseded=superseded || repair.revision<r.current_revision;
        if(auto allowed=projected(r,repair.canonical);!allowed)return fail(allowed.error());
        if(overlap(repair.canonical,scratch_) || overlap(repair.canonical,std::as_bytes(records_)) || overlap(repair.canonical,std::as_bytes(receipts)))return fail(Error::InvalidArgument);
        offsets[n]=bytes; if(repair.canonical.size()>scratch_.size()-bytes)return fail(Error::CapacityExceeded); bytes+=repair.canonical.size();
        if(repair.revision==r.current_revision && !r.repair_required) { if(!std::equal(repair.canonical.begin(),repair.canonical.end(),image(i,0).begin()))return fail(Error::ProtocolViolation); ++duplicates; }
    }
    if(auto contract=group(std::span(members).first(repairs.size()),group_id,bytes);!contract)return fail(contract.error());
    // An unordered full repair can be overtaken by a newer publication. Finish
    // every structural/visibility/group check before discarding the entire old
    // group. No receipt, canonical state or application preparation is changed.
    if(superseded)return fail(Error::StaleGeneration);
    if(duplicates==repairs.size()) { for(std::size_t n=0;n<repairs.size();++n)receipts[n]={repairs[n].key,repairs[n].revision}; return PublicationResult{PublicationDisposition::Duplicate,static_cast<std::uint16_t>(repairs.size()),0}; }
    for(std::size_t n=0;n<repairs.size();++n) {
        auto target=scratch_.subspan(offsets[n],repairs[n].canonical.size()); std::memmove(target.data(),repairs[n].canonical.data(),target.size());
        staged[n]=view(members[n]); staged[n].revision=repairs[n].revision; staged[n].tick=tick; staged[n].canonical=target;
    }
    if(auto applied=application(app,ReplicaChangeKind::Publication,std::span(staged).first(repairs.size()));!applied)return fail(applied.error());
    for(std::size_t n=0;n<repairs.size();++n) { auto& r=records_[members[n]]; std::memmove(image(members[n],0).data(),staged[n].canonical.data(),staged[n].canonical.size()); r.current_revision=repairs[n].revision; r.current_tick=tick; r.repair_required=false; receipts[n]={repairs[n].key,r.current_revision}; }
    return PublicationResult{PublicationDisposition::Applied,static_cast<std::uint16_t>(repairs.size()),0};
}
Result<LifecycleAppliedReceipt> PeerReplicaReceiver::retire_baseline(const BaselineRetirement& retirement) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    const std::array extra{retirement.old_token,retirement.replacement_token}; const auto metadata=stamp(2,retirement.lifecycle_sequence,retirement.key,extra);
    auto fresh=control(metadata); if(!fresh)return fail(fresh.error()); if(!*fresh)return LifecycleAppliedReceipt{control_applied_};
    auto found=index(retirement.key); if(!found)return fail(found.error()); const auto i=*found; auto& r=records_[i];
    if(!retirement.old_token || !retirement.replacement_token || retirement.old_token==retirement.replacement_token)return fail(Error::InvalidArgument);
    std::size_t old=2,replacement=2;
    for(std::size_t n=0;n<2;++n)if(r.images[n].pinned) { if(r.images[n].token==retirement.old_token)old=n; if(r.images[n].token==retirement.replacement_token)replacement=n; }
    if(old==2 || replacement==2 || r.active_image!=static_cast<std::int8_t>(replacement))return fail(Error::MissingBaseline);
    std::fill(image(i,old+1).begin(),image(i,old+1).end(),std::byte{}); r.images[old]={}; r.retired_token=retirement.old_token; r.retired_replacement=retirement.replacement_token; r.retired_sequence=retirement.lifecycle_sequence;
    acknowledge_control(metadata); return LifecycleAppliedReceipt{control_applied_};
}
Result<LifecycleAppliedReceipt> PeerReplicaReceiver::leave(const ReplicaKey& k,std::uint64_t sequence,ReplicaApplication& app) noexcept {
    if(auto ready=available();!ready)return fail(ready.error()); const auto metadata=stamp(3,sequence,k);
    auto fresh=control(metadata); if(!fresh)return fail(fresh.error()); if(!*fresh)return LifecycleAppliedReceipt{control_applied_};
    auto found=index(k); if(!found)return fail(found.error()); const auto i=*found; auto& r=records_[i];
    const std::array changes{view(i)}; if(auto applied=application(app,ReplicaChangeKind::Destroy,changes);!applied)return fail(applied.error());
    if(r.phase==ReceiverPhase::SpawnPending)--transitions_; --active_;
    const auto incarnation=r.incarnation,encoding=r.encoding_epoch; const auto handle=r.handle;
    std::fill(images_.begin()+static_cast<std::ptrdiff_t>(3*i*config_.state_stride),images_.begin()+static_cast<std::ptrdiff_t>((3*i+3)*config_.state_stride),std::byte{});
    r={}; r.incarnation=incarnation; r.encoding_epoch=encoding; r.handle=handle; r.leave_sequence=sequence; acknowledge_control(metadata); return LifecycleAppliedReceipt{control_applied_};
}
Result<LifecycleAppliedReceipt> PeerReplicaReceiver::reset_ownership(const ReplicaKey& old_key,const ReplicaKey& new_key,PeerId owner,std::uint64_t revision,std::uint64_t sequence,ReplicaApplication& app) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    const auto new_words=key_words(new_key); std::array<std::uint64_t,8> extra{}; std::copy(new_words.begin(),new_words.end(),extra.begin()); extra[6]=owner; extra[7]=revision;
    const auto metadata=stamp(4,sequence,old_key,extra); auto fresh=control(metadata); if(!fresh)return fail(fresh.error()); if(!*fresh)return LifecycleAppliedReceipt{control_applied_};
    auto found=index(old_key); if(!found)return fail(found.error()); const auto i=*found; auto& r=records_[i];
    if(r.phase!=ReceiverPhase::Ready)return fail(Error::NotReady);
    if(old_key.encoding_epoch==UINT64_MAX)return fail(Error::CounterExhausted);
    auto expected=old_key; ++expected.encoding_epoch;
    if(new_key!=expected || revision<r.ownership_revision || (revision==r.ownership_revision && owner!=r.owner))return fail(Error::InvalidArgument);
    auto target=scratch_.first(r.catalog->schema.state_bytes());
    if(auto projected=r.catalog->schema.project(image(i,0),config_.peer==owner,false,target);!projected)return fail(projected.error());
    auto state=view(i); state.key=new_key; state.owner=owner; state.ownership_revision=revision; state.canonical=target;
    const std::array changes{state}; if(auto applied=application(app,ReplicaChangeKind::OwnershipReset,changes);!applied)return fail(applied.error());
    std::memmove(image(i,0).data(),target.data(),target.size()); for(std::size_t n=0;n<2;++n) { std::fill(image(i,n+1).begin(),image(i,n+1).end(),std::byte{}); r.images[n]={}; }
    r.encoding_epoch=new_key.encoding_epoch; r.owner=owner; r.ownership_revision=revision; r.active_image=-1; r.repair_required=true; r.ownership_sequence=sequence;
    acknowledge_control(metadata); return LifecycleAppliedReceipt{control_applied_};
}
Result<CanonicalReplica> PeerReplicaReceiver::inspect(const ReplicaKey& key) const noexcept {
    auto found=index(key); if(!found)return fail(found.error()); if(records_[*found].phase!=ReceiverPhase::Ready)return fail(Error::NotReady); return view(*found);
}
Result<ReceiverRecord> PeerReplicaReceiver::inspect_record(const ReplicaKey& key) const noexcept { auto found=index(key); if(!found)return fail(found.error()); return records_[*found]; }
Result<Fingerprint> PeerReplicaReceiver::schema_fingerprint() const noexcept { if(auto ready=available();!ready)return fail(ready.error()); return registry_.fingerprint(); }
Result<ReceiverConfig> PeerReplicaReceiver::configuration() const noexcept { if(auto ready=available();!ready)return fail(ready.error()); return config_; }
Result<std::uint64_t> PeerReplicaReceiver::instance_identity() const noexcept { if(auto ready=available();!ready)return fail(ready.error()); return instance_; }
std::uint64_t PeerReplicaReceiver::lifecycle_applied() const noexcept { return available()?control_applied_:0; }
}
