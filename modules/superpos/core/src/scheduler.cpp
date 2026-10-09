#include "superpos/scheduler.hpp"
#include <array>
#include <algorithm>
#include <utility>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
std::size_t hash_slot(std::uint32_t slot,std::size_t size) noexcept {
    // This is an internal non-cryptographic table index, never a schema digest.
    return static_cast<std::size_t>(std::uint64_t(slot)*0x9e3779b1ULL%size);
}
bool eligible(const SchedulerRecord& r) noexcept {
    return r.occupied && !r.retiring && (r.unit.visibility==Visibility::Hidden ||
        (r.unit.visibility==Visibility::Visible && (r.repair || r.unit.revision>r.accepted_revision)));
}
std::uint32_t price(const SchedulerRecord& r) noexcept {
    return r.unit.visibility==Visibility::Visible && r.repair?r.unit.repair_wire_bytes:r.unit.wire_bytes;
}
bool before(const SchedulerRecord& a,const SchedulerRecord& b,std::uint64_t round,std::uint64_t wait) noexcept {
    const auto age_a=round-a.last_served_round,age_b=round-b.last_served_round;
    const bool urgent_a=age_a>=wait,urgent_b=age_b>=wait;
    if(urgent_a!=urgent_b)return urgent_a;
    if(urgent_a && age_a!=age_b)return age_a>age_b;
    if(!urgent_a && a.unit.weight!=b.unit.weight)return a.unit.weight>b.unit.weight;
    if(age_a!=age_b)return age_a>age_b;
    return a.unit.handle.value<b.unit.handle.value;
}
}
PeerScheduler::PeerScheduler(SchedulerConfig c,std::span<SchedulerRecord> records,std::span<SchedulerIndex> indices) noexcept:
    config_(c),records_(records),indices_(indices),round_(c.initial_round) {
    for(auto& r:records_)r={}; for(auto& i:indices_)i={};
}
Result<PeerScheduler> PeerScheduler::create(SchedulerConfig c,std::span<SchedulerRecord> records,std::span<SchedulerIndex> indices) noexcept {
    if(!c.authority_epoch || !c.connection_epoch || !c.replica_epoch || !c.maximum_objects || !c.maximum_unit_bytes || c.maximum_unit_bytes>131072 ||
        !c.maximum_unit_objects || c.maximum_unit_objects>16 || !c.maximum_weight || c.maximum_weight>256 || !c.maximum_wait_rounds ||
        records.empty() || records.size()>maximum_units || indices.size()<2*records.size() || indices.size()>4*maximum_units ||
        overlap(std::as_bytes(records),std::as_bytes(indices)))return fail(Error::InvalidArgument);
    return PeerScheduler(c,records,indices);
}
PeerScheduler::PeerScheduler(PeerScheduler&& other) noexcept { *this=std::move(other); }
PeerScheduler& PeerScheduler::operator=(PeerScheduler&& other) noexcept {
    if(this!=&other) {
        config_=other.config_; records_=std::exchange(other.records_,{}); indices_=std::exchange(other.indices_,{});
        active_=std::exchange(other.active_,0); objects_=std::exchange(other.objects_,0); round_=other.round_;
        pending_token_=std::exchange(other.pending_token_,0); pending_units_=std::exchange(other.pending_units_,0); owner_=other.owner_;
        replicas_=std::exchange(other.replicas_,nullptr);
    } return *this;
}
Status PeerScheduler::validate(const SchedulingUnit& unit) const noexcept {
    if(records_.empty())return fail(Error::NotReady);
    if(!unit.handle || !unit.key.slot || !unit.key.incarnation || !unit.key.encoding_epoch || !unit.revision || !unit.wire_bytes ||
        unit.wire_bytes>config_.maximum_unit_bytes || unit.repair_wire_bytes>config_.maximum_unit_bytes || !unit.object_count || unit.object_count>config_.maximum_unit_objects || !unit.weight ||
        unit.weight>config_.maximum_weight || static_cast<unsigned>(unit.visibility)>static_cast<unsigned>(Visibility::Dormant))return fail(Error::InvalidArgument);
    if(unit.key.authority_epoch!=config_.authority_epoch || unit.key.connection_epoch!=config_.connection_epoch || unit.key.replica_epoch!=config_.replica_epoch)return fail(Error::StaleEpoch);
    return {};
}
Result<std::size_t> PeerScheduler::lookup(std::uint32_t slot) const noexcept {
    if(indices_.empty())return fail(Error::NotReady);
    auto pos=hash_slot(slot,indices_.size());
    for(std::size_t work=0;work<indices_.size();++work,pos=(pos+1)%indices_.size()) {
        const auto& entry=indices_[pos]; if(entry.state==SchedulerIndexState::Empty)return fail(Error::StaleGeneration);
        if(entry.state==SchedulerIndexState::Occupied && entry.slot==slot) {
            if(entry.record>=records_.size() || !records_[entry.record].occupied || records_[entry.record].unit.handle.slot()!=slot)return fail(Error::ProtocolViolation);
            return pos;
        }
        if(static_cast<unsigned>(entry.state)>static_cast<unsigned>(SchedulerIndexState::Retired))return fail(Error::ProtocolViolation);
    } return fail(Error::StaleGeneration);
}
Result<std::size_t> PeerScheduler::vacant_index(std::uint32_t slot) const noexcept {
    auto pos=hash_slot(slot,indices_.size()); std::size_t retired=indices_.size();
    for(std::size_t work=0;work<indices_.size();++work,pos=(pos+1)%indices_.size()) {
        const auto& entry=indices_[pos];
        if(entry.state==SchedulerIndexState::Empty)return retired<indices_.size()?retired:pos;
        if(entry.state==SchedulerIndexState::Retired && retired==indices_.size())retired=pos;
        if(static_cast<unsigned>(entry.state)>static_cast<unsigned>(SchedulerIndexState::Retired))return fail(Error::ProtocolViolation);
    }
    if(retired<indices_.size())return retired; return fail(Error::CapacityExceeded);
}
Result<std::size_t> PeerScheduler::index(ObjectHandle handle,const ReplicaKey& key) const noexcept {
    if(records_.empty())return fail(Error::NotReady);
    if(key.authority_epoch!=config_.authority_epoch || key.connection_epoch!=config_.connection_epoch || key.replica_epoch!=config_.replica_epoch)return fail(Error::StaleEpoch);
    if(!handle)return fail(Error::StaleGeneration);
    auto found=lookup(handle.slot()); if(!found)return fail(found.error()); const auto i=indices_[*found].record;
    const auto& unit=records_[i].unit;
    if(unit.handle!=handle || unit.key.slot!=key.slot || unit.key.incarnation!=key.incarnation)return fail(Error::StaleGeneration);
    if(unit.key.encoding_epoch!=key.encoding_epoch)return fail(Error::StaleEpoch); return i;
}
Status PeerScheduler::readiness(const SchedulerRecord& record) const noexcept {
    if(!record.replicas)return fail(Error::NotReady);
    for(std::size_t n=0;n<record.unit.object_count;++n) {
        const auto& member=record.members[n]; auto live=record.replicas->inspect(member.key); if(!live)return fail(live.error());
        if(live->phase!=ReplicaPhase::Ready)return fail(Error::NotReady);
        if(live->handle!=member.handle || live->spawn_sequence!=member.lifecycle_sequence)return fail(Error::StaleGeneration);
        if(record.unit.revision<live->state_applied_revision)return fail(Error::ProtocolViolation);
    } return {};
}
Status PeerScheduler::track(SchedulingUnit unit,const PeerReplicas& replicas,std::span<const ReplicaBinding> members,std::uint64_t accepted) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pending_token_)return fail(Error::Busy);
    if(auto valid=validate(unit);!valid)return valid; if(accepted>unit.revision)return fail(Error::InvalidArgument);
    if(replicas_ && replicas_!=&replicas)return fail(Error::InvalidArgument);
    if(overlap(std::as_bytes(members),std::as_bytes(records_)) || overlap(std::as_bytes(members),std::as_bytes(indices_)))return fail(Error::InvalidArgument);
    if(members.empty() && unit.object_count!=1)return fail(Error::InvalidArgument);
    if(!members.empty() && members.size()!=unit.object_count)return fail(Error::InvalidArgument);
    SchedulerRecord record; record.occupied=true; record.unit=unit; record.accepted_revision=accepted; record.last_served_round=round_; record.replicas=&replicas;
    if(members.empty()) {
        auto live=replicas.inspect(unit.key); if(!live)return fail(live.error());
        record.members[0]={unit.key,unit.handle,live->spawn_sequence};
    } else std::copy(members.begin(),members.end(),record.members.begin());
    if(record.members[0].handle!=unit.handle || record.members[0].key!=unit.key)return fail(Error::InvalidArgument);
    if(auto ready=readiness(record);!ready)return ready;
    for(std::size_t n=0;n<unit.object_count;++n) {
        auto live=replicas.inspect(record.members[n].key); if(!live)return fail(live.error());
        if(accepted>live->state_applied_revision)return fail(Error::ProtocolViolation);
    }
    for(std::size_t n=0;n<unit.object_count;++n) {
        for(std::size_t previous=0;previous<n;++previous)
            if(record.members[n].key.slot==record.members[previous].key.slot || record.members[n].handle.slot()==record.members[previous].handle.slot())return fail(Error::InvalidArgument);
        for(const auto& existing:records_)if(existing.occupied)for(std::size_t p=0;p<existing.unit.object_count;++p)
            if(record.members[n].key.slot==existing.members[p].key.slot || record.members[n].handle.slot()==existing.members[p].handle.slot())return fail(Error::StaleGeneration);
    }
    auto found=lookup(unit.handle.slot()); if(found)return fail(records_[indices_[*found].record].unit.handle==unit.handle?Error::Busy:Error::StaleGeneration);
    if(found.error()!=Error::StaleGeneration)return fail(found.error());
    if(unit.object_count>config_.maximum_objects-objects_ || active_==records_.size())return fail(Error::CapacityExceeded);
    std::size_t free=0; while(free<records_.size() && records_[free].occupied)++free;
    if(free==records_.size())return fail(Error::CapacityExceeded);
    auto insertion=vacant_index(unit.handle.slot()); if(!insertion)return fail(insertion.error());
    records_[free]=record; indices_[*insertion]={unit.handle.slot(),static_cast<std::uint32_t>(free),SchedulerIndexState::Occupied};
    ++active_; objects_+=unit.object_count; replicas_=&replicas; return {};
}
Status PeerScheduler::update(std::span<const SchedulingUnit> units) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady); if(pending_token_)return fail(Error::Busy);
    if(units.size()>maximum_batch)return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(units),std::as_bytes(records_)) || overlap(std::as_bytes(units),std::as_bytes(indices_)))return fail(Error::InvalidArgument);
    std::array<std::size_t,maximum_batch> targets{};
    for(std::size_t n=0;n<units.size();++n) {
        if(auto valid=validate(units[n]);!valid)return valid;
        auto i=index(units[n].handle,units[n].key); if(!i)return fail(i.error()); targets[n]=*i;
        const auto& r=records_[*i]; if(r.retiring)return fail(Error::NotReady);
        if(auto ready=readiness(r);!ready)return ready;
        if(units[n].revision<r.unit.revision || units[n].object_count!=r.unit.object_count)return fail(Error::InvalidArgument);
        if(units[n].visibility==Visibility::Visible && (r.repair || r.unit.visibility!=Visibility::Visible) && !units[n].repair_wire_bytes)return fail(Error::InvalidArgument);
        for(std::size_t previous=0;previous<n;++previous)if(targets[previous]==*i)return fail(Error::InvalidArgument);
    }
    for(std::size_t n=0;n<units.size();++n) {
        auto& r=records_[targets[n]];
        if(r.unit.visibility!=Visibility::Visible && units[n].visibility==Visibility::Visible)r.repair=true;
        r.unit=units[n];
    } return {};
}
Status PeerScheduler::rebind(SchedulingUnit unit,const PeerReplicas& replicas,std::span<const ReplicaBinding> members) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pending_token_)return fail(Error::Busy);
    if(auto valid=validate(unit);!valid)return valid;
    if(!unit.repair_wire_bytes || replicas_!=&replicas)return fail(Error::InvalidArgument);
    if(overlap(std::as_bytes(members),std::as_bytes(records_)) || overlap(std::as_bytes(members),std::as_bytes(indices_)))return fail(Error::InvalidArgument);
    auto entry=lookup(unit.handle.slot()); if(!entry)return fail(entry.error()); auto& record=records_[indices_[*entry].record];
    if(record.retiring)return fail(Error::NotReady);
    if(unit.handle!=record.unit.handle || unit.object_count!=record.unit.object_count || unit.visibility!=record.unit.visibility || unit.revision<record.unit.revision)return fail(Error::InvalidArgument);
    if((members.empty() && unit.object_count!=1) || (!members.empty() && members.size()!=unit.object_count))return fail(Error::InvalidArgument);
    auto proposed=record; proposed.unit=unit;
    if(members.empty()) { auto live=replicas.inspect(unit.key); if(!live)return fail(live.error()); proposed.members[0]={unit.key,unit.handle,live->spawn_sequence}; }
    else std::copy(members.begin(),members.end(),proposed.members.begin());
    if(proposed.members[0].handle!=unit.handle || proposed.members[0].key!=unit.key)return fail(Error::InvalidArgument);
    if(auto ready=readiness(proposed);!ready)return ready;
    bool changed=false;
    for(std::size_t n=0;n<unit.object_count;++n) {
        const auto& old=record.members[n]; const auto& fresh=proposed.members[n];
        if(fresh.handle!=old.handle || fresh.key.slot!=old.key.slot || fresh.key.incarnation!=old.key.incarnation || fresh.lifecycle_sequence!=old.lifecycle_sequence)return fail(Error::StaleGeneration);
        if(fresh.key.encoding_epoch<old.key.encoding_epoch)return fail(Error::StaleEpoch);
        if(fresh.key.encoding_epoch>old.key.encoding_epoch) {
            auto live=replicas.inspect(fresh.key); if(!live)return fail(live.error());
            if(!live->reset_sequence || live->reset_sequence>replicas.lifecycle_applied())return fail(Error::NotReady);
            changed=true;
        }
    }
    if(!changed)return fail(Error::InvalidArgument);
    proposed.repair=true; record=proposed; return {};
}
Status PeerScheduler::request_repair(ObjectHandle handle,const ReplicaKey& key,std::uint32_t wire_bytes) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pending_token_)return fail(Error::Busy); auto i=index(handle,key); if(!i)return fail(i.error());
    if(!wire_bytes || wire_bytes>config_.maximum_unit_bytes)return fail(Error::InvalidArgument);
    auto& r=records_[*i]; if(r.retiring || r.unit.visibility!=Visibility::Visible)return fail(Error::NotReady);
    if(auto ready=readiness(r);!ready)return ready;
    r.unit.repair_wire_bytes=wire_bytes; r.repair=true; return {};
}
Result<SchedulePlan> PeerScheduler::plan(ScheduleBudget budget,std::span<ScheduleSelection> output) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady); if(pending_token_)return fail(Error::Busy);
    if(!budget.wire_bytes || !budget.objects || !budget.units || budget.units>maximum_batch)return fail(Error::InvalidArgument);
    if(output.size()<std::min<std::size_t>(budget.units,records_.size()))return fail(Error::CapacityExceeded);
    if(overlap(std::as_bytes(output),std::as_bytes(records_)) || overlap(std::as_bytes(output),std::as_bytes(indices_)))return fail(Error::InvalidArgument);
    if(round_==UINT64_MAX)return fail(Error::CounterExhausted);
    SchedulePlan result; result.token=round_+1;
    for(const auto& record:records_)if(eligible(record)) {
        if(auto ready=readiness(record);!ready)return fail(ready.error());
        if(!price(record))return fail(Error::InvalidArgument);
        if(price(record)>budget.wire_bytes || record.unit.object_count>budget.objects)++result.oversized_units;
    }
    while(result.units<budget.units) {
        std::size_t best=records_.size();
        for(std::size_t i=0;i<records_.size();++i) {
            const auto& r=records_[i]; if(!eligible(r) || r.selected_round==result.token || price(r)>budget.wire_bytes-result.wire_bytes || r.unit.object_count>budget.objects-result.objects)continue;
            if(best==records_.size() || before(r,records_[best],result.token,config_.maximum_wait_rounds))best=i;
        }
        if(best==records_.size())break;
        auto& r=records_[best]; const auto action=r.unit.visibility==Visibility::Hidden?SchedulingAction::Retire:r.repair?SchedulingAction::Repair:SchedulingAction::State;
        output[result.units]={r.unit,action,price(r)}; r.selected_round=result.token; r.selected_ordinal=result.units; r.selected_action=action;
        ++result.units; result.wire_bytes+=price(r); result.objects+=r.unit.object_count;
    }
    if(!result.units)return fail(result.oversized_units?Error::CapacityExceeded:Error::NotReady);
    round_=result.token; pending_token_=result.token; pending_units_=result.units; return result;
}
Status PeerScheduler::finish(std::uint64_t token,std::span<const ScheduleCompletion> completed) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady); if(!pending_token_ || token!=pending_token_)return fail(Error::StaleEpoch);
    if(completed.size()!=pending_units_)return fail(Error::InvalidArgument);
    if(overlap(std::as_bytes(completed),std::as_bytes(records_)) || overlap(std::as_bytes(completed),std::as_bytes(indices_)))return fail(Error::InvalidArgument);
    std::array<bool,maximum_batch> seen{}; std::size_t count=0;
    for(const auto& r:records_)if(r.selected_round==pending_token_) {
        if(!r.occupied || r.retiring || r.selected_ordinal>=completed.size() || seen[r.selected_ordinal])return fail(Error::ProtocolViolation);
        seen[r.selected_ordinal]=true; ++count; const auto& completion=completed[r.selected_ordinal];
        if(r.selected_action==SchedulingAction::Retire) {
            if(completion.carrier_accepted!=bool(completion.retirement_sequence))return fail(Error::InvalidArgument);
        } else if(completion.retirement_sequence)return fail(Error::InvalidArgument);
    }
    if(count!=pending_units_)return fail(Error::ProtocolViolation);
    for(auto& r:records_)if(r.selected_round==pending_token_) {
        const auto& completion=completed[r.selected_ordinal];
        if(completion.carrier_accepted) {
            r.last_served_round=round_;
            if(r.selected_action==SchedulingAction::Retire) { r.retiring=true; r.retirement_sequence=completion.retirement_sequence; }
            else { r.accepted_revision=r.unit.revision; r.repair=false; }
        }
        r.selected_round=0; r.selected_ordinal=0;
    }
    pending_token_=0; pending_units_=0; return {};
}
Status PeerScheduler::retirement_applied(ObjectHandle handle,const ReplicaKey& key,std::uint64_t watermark) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pending_token_)return fail(Error::Busy); auto i=index(handle,key); if(!i)return fail(i.error()); auto& r=records_[*i];
    if(!r.retiring || !r.retirement_sequence)return fail(Error::NotReady);
    if(watermark<r.retirement_sequence)return fail(Error::NotReady);
    auto entry=lookup(handle.slot()); if(!entry)return fail(entry.error());
    --active_; objects_-=r.unit.object_count; r={}; indices_[*entry].state=SchedulerIndexState::Retired; return {};
}
Result<SchedulerRecord> PeerScheduler::inspect(ObjectHandle handle,const ReplicaKey& key) const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    auto i=index(handle,key); if(!i)return fail(i.error()); return records_[*i];
}
}
