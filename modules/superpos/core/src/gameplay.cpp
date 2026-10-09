#include "superpos/gameplay.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if (a.empty() || b.empty()) return false;
    auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
bool valid_descriptor(SimulationDescriptor d) noexcept {
    return d.state_bytes>0 && d.state_bytes<=65536 && d.input_bytes<=4096;
}
bool external(std::span<const std::byte> value,std::span<const std::byte> arena,std::span<const std::byte> scratch,std::span<const std::byte> records) noexcept {
    return !overlap(value,arena) && !overlap(value,scratch) && !overlap(value,records);
}
}
PredictionHistory::PredictionHistory(PredictionConfig c,SimulationAdapter& s,SimulationDescriptor d,
    std::span<PredictionFrame> r,std::span<std::byte> a,std::span<std::byte> w) noexcept:
    config_(c),simulation_(&s),descriptor_(d),records_(r),arena_(a),scratch_(w) {}
PredictionHistory::PredictionHistory(PredictionHistory&& other) noexcept { *this=std::move(other); }
PredictionHistory& PredictionHistory::operator=(PredictionHistory&& other) noexcept {
    if (this!=&other) {
        config_=other.config_; descriptor_=other.descriptor_; authority_revision_=other.authority_revision_;
        simulation_=std::exchange(other.simulation_,nullptr); records_=std::exchange(other.records_,{});
        arena_=std::exchange(other.arena_,{}); scratch_=std::exchange(other.scratch_,{});
        head_=other.head_; count_=std::exchange(other.count_,0);
    } return *this;
}
std::span<std::byte> PredictionHistory::state(std::size_t i) noexcept { return arena_.subspan(i*(descriptor_.state_bytes+descriptor_.input_bytes),descriptor_.state_bytes); }
std::span<const std::byte> PredictionHistory::state(std::size_t i) const noexcept { return arena_.subspan(i*(descriptor_.state_bytes+descriptor_.input_bytes),descriptor_.state_bytes); }
std::span<const std::byte> PredictionHistory::input(std::size_t i) const noexcept { return arena_.subspan(i*(descriptor_.state_bytes+descriptor_.input_bytes)+descriptor_.state_bytes,descriptor_.input_bytes); }
Result<PredictionHistory> PredictionHistory::create(PredictionConfig config,SimulationAdapter& simulation,
    std::span<PredictionFrame> records,std::span<std::byte> arena,std::span<std::byte> scratch,
    Tick initial_tick,std::uint64_t revision,std::span<const std::byte> initial) noexcept {
    auto d=simulation.descriptor();
    if (!config.epoch || !revision || !valid_descriptor(d) || !config.history_ticks || config.history_ticks>4096) return fail(Error::InvalidArgument);
    if (!supports(d.capabilities,SimulationCapability::DeterministicReplay)) return fail(Error::Unsupported);
    const auto capacity=config.history_ticks+1, stride=d.state_bytes+d.input_bytes;
    if (capacity>SIZE_MAX/stride || capacity>SIZE_MAX/d.state_bytes) return fail(Error::Overflow);
    if (records.size()<capacity || arena.size()<capacity*stride || scratch.size()<capacity*d.state_bytes) return fail(Error::CapacityExceeded);
    records=records.first(capacity); arena=arena.first(capacity*stride); scratch=scratch.first(capacity*d.state_bytes);
    auto record_bytes=std::as_bytes(records);
    if (initial.size()!=d.state_bytes || overlap(arena,scratch) || overlap(record_bytes,arena) || overlap(record_bytes,scratch) || !external(initial,arena,scratch,record_bytes)) return fail(Error::InvalidArgument);
    if (auto valid=simulation.validate_state(initial);!valid) return fail(valid.error());
    PredictionHistory result(config,simulation,d,records,arena,scratch);
    for (auto& record:records) record={}; records[0]={initial_tick,true}; result.authority_revision_=revision;
    std::memcpy(result.state(0).data(),initial.data(),initial.size()); return result;
}
StateView PredictionHistory::current() const noexcept {
    if (!count_) return {}; auto i=index(count_-1); return {records_[i].tick,state(i)};
}
Result<StateView> PredictionHistory::at(Epoch epoch,Tick tick) const noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    for (std::size_t i=0;i<count_;++i) if (records_[index(i)].tick==tick) return StateView{tick,state(index(i))};
    return fail(Error::RecoveryUnavailable);
}
Status PredictionHistory::predict(Epoch epoch,Tick tick,std::span<const std::byte> value) noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    auto now=current(); if (now.tick==UINT64_MAX) return fail(Error::CounterExhausted);
    if (tick!=now.tick+1 || value.size()!=descriptor_.input_bytes) return fail(Error::InvalidArgument);
    if (count_==records_.size()) return fail(Error::CapacityExceeded);
    if (!external(value,arena_,scratch_,std::as_bytes(records_))) return fail(Error::InvalidArgument);
    if (auto valid=simulation_->validate_input(value);!valid) return valid;
    auto staged=scratch_.first(descriptor_.state_bytes);
    if (auto step=simulation_->step(tick,now.canonical,value,staged);!step) return step;
    if (auto valid=simulation_->validate_state(staged);!valid) return valid;
    auto i=index(count_); std::memcpy(state(i).data(),staged.data(),staged.size());
    if (!value.empty()) std::memcpy(arena_.data()+i*(descriptor_.state_bytes+descriptor_.input_bytes)+descriptor_.state_bytes,value.data(),value.size());
    records_[i]={tick,true}; ++count_; return {};
}
Result<Reconciliation> PredictionHistory::reconcile(Epoch epoch,Tick tick,std::uint64_t revision,std::span<const std::byte> authoritative) noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    if (!revision || authoritative.size()!=descriptor_.state_bytes || !external(authoritative,arena_,scratch_,std::as_bytes(records_))) return fail(Error::InvalidArgument);
    if (revision<authority_revision_ || tick<confirmed_tick()) return fail(Error::NotReady);
    if (auto valid=simulation_->validate_state(authoritative);!valid) return fail(valid.error());
    if (revision==authority_revision_) {
        if (tick!=confirmed_tick() || std::memcmp(state(head_).data(),authoritative.data(),authoritative.size())) return fail(Error::ProtocolViolation);
        return Reconciliation{};
    }
    std::size_t offset=0; while (offset<count_ && records_[index(offset)].tick!=tick) ++offset;
    if (offset==count_) return fail(tick>current().tick?Error::NotReady:Error::RecoveryUnavailable);
    const bool changed=std::memcmp(state(index(offset)).data(),authoritative.data(),authoritative.size())!=0;
    std::size_t replayed=0;
    if (changed) {
        std::memcpy(scratch_.data(),authoritative.data(),authoritative.size());
        // No stored post-state changes until the entire replay has succeeded.
        for (std::size_t j=offset+1;j<count_;++j) {
            auto previous=scratch_.subspan((j-offset-1)*descriptor_.state_bytes,descriptor_.state_bytes);
            auto next=scratch_.subspan((j-offset)*descriptor_.state_bytes,descriptor_.state_bytes);
            auto i=index(j); if (auto step=simulation_->step(records_[i].tick,previous,input(i),next);!step) return fail(step.error());
            if (auto valid=simulation_->validate_state(next);!valid) return fail(valid.error()); ++replayed;
        }
        for (std::size_t j=offset;j<count_;++j) std::memcpy(state(index(j)).data(),scratch_.data()+(j-offset)*descriptor_.state_bytes,descriptor_.state_bytes);
    }
    for (std::size_t j=0;j<offset;++j) records_[index(j)]={};
    head_=index(offset); count_-=offset; authority_revision_=revision; return Reconciliation{replayed,changed};
}
Status PredictionHistory::reset_epoch(Epoch epoch,Tick tick,std::uint64_t revision,std::span<const std::byte> canonical) noexcept {
    if (!simulation_) return fail(Error::NotReady);
    if (epoch<=config_.epoch) return fail(Error::StaleEpoch);
    if (!revision || canonical.size()!=descriptor_.state_bytes || !external(canonical,arena_,scratch_,std::as_bytes(records_))) return fail(Error::InvalidArgument);
    if (auto valid=simulation_->validate_state(canonical);!valid) return valid;
    for (auto& r:records_) r={}; head_=0; count_=1; config_.epoch=epoch; authority_revision_=revision;
    records_[0]={tick,true}; std::memcpy(state(0).data(),canonical.data(),canonical.size()); return {};
}

SnapshotHistory::SnapshotHistory(HistoryConfig c,SimulationAdapter& s,SimulationDescriptor d,
    std::span<HistoryFrame> r,std::span<std::byte> a,std::span<std::byte> w) noexcept:
    config_(c),simulation_(&s),descriptor_(d),records_(r),arena_(a),scratch_(w) {}
SnapshotHistory::SnapshotHistory(SnapshotHistory&& other) noexcept { *this=std::move(other); }
SnapshotHistory& SnapshotHistory::operator=(SnapshotHistory&& other) noexcept {
    if (this!=&other) {
        config_=other.config_; descriptor_=other.descriptor_; simulation_=std::exchange(other.simulation_,nullptr);
        records_=std::exchange(other.records_,{}); arena_=std::exchange(other.arena_,{}); scratch_=std::exchange(other.scratch_,{});
        head_=other.head_; count_=std::exchange(other.count_,0);
    } return *this;
}
std::span<std::byte> SnapshotHistory::state(std::size_t i) noexcept { return arena_.subspan(i*descriptor_.state_bytes,descriptor_.state_bytes); }
std::span<const std::byte> SnapshotHistory::state(std::size_t i) const noexcept { return arena_.subspan(i*descriptor_.state_bytes,descriptor_.state_bytes); }
Result<SnapshotHistory> SnapshotHistory::create(HistoryConfig c,SimulationAdapter& s,std::span<HistoryFrame> r,std::span<std::byte> a,std::span<std::byte> w) noexcept {
    auto d=s.descriptor(); if (!c.epoch || !valid_descriptor(d) || c.capacity<2 || c.capacity>4096 || !c.maximum_bracket_ticks || c.maximum_bracket_ticks>(std::uint64_t{1}<<53)) return fail(Error::InvalidArgument);
    if (c.capacity>SIZE_MAX/d.state_bytes) return fail(Error::Overflow);
    if (r.size()<c.capacity || a.size()<c.capacity*d.state_bytes || w.size()<d.state_bytes) return fail(Error::CapacityExceeded);
    r=r.first(c.capacity); a=a.first(c.capacity*d.state_bytes); w=w.first(d.state_bytes);
    if (overlap(a,w) || overlap(std::as_bytes(r),a) || overlap(std::as_bytes(r),w)) return fail(Error::InvalidArgument);
    for (auto& v:r) v={}; return SnapshotHistory(c,s,d,r,a,w);
}
Status SnapshotHistory::push(Epoch epoch,Tick tick,std::uint64_t revision,std::span<const std::byte> value) noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    if (!revision || value.size()!=descriptor_.state_bytes || !external(value,arena_,scratch_,std::as_bytes(records_))) return fail(Error::InvalidArgument);
    bool replace=false;
    if (count_) {
        auto i=index(count_-1); const auto& last=records_[i];
        if (tick<last.tick || revision<last.revision) return fail(Error::NotReady);
        if (revision==last.revision) {
            if (tick!=last.tick || std::memcmp(state(i).data(),value.data(),value.size())) return fail(Error::ProtocolViolation);
            return {};
        } replace=tick==last.tick;
    }
    if (auto valid=simulation_->validate_state(value);!valid) return valid;
    if (!replace && count_==records_.size()) { records_[head_]={}; head_=index(1); --count_; }
    auto i=replace?index(count_-1):index(count_);
    std::memcpy(state(i).data(),value.data(),value.size()); records_[i]={tick,revision,true}; if (!replace) ++count_; return {};
}
Result<StateView> SnapshotHistory::exact(Epoch epoch,Tick tick) const noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    for (std::size_t j=0;j<count_;++j) if (records_[index(j)].tick==tick) return StateView{tick,state(index(j))};
    return fail(Error::RecoveryUnavailable);
}
Status SnapshotHistory::sample(Epoch epoch,Tick tick,double fraction,std::span<std::byte> output) noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch!=config_.epoch) return fail(Error::StaleEpoch);
    if (!std::isfinite(fraction) || fraction<0 || fraction>=1 || output.size()!=descriptor_.state_bytes || !external(output,arena_,scratch_,std::as_bytes(records_))) return fail(Error::InvalidArgument);
    if (!count_) return fail(Error::NotReady);
    std::size_t before=count_;
    for (std::size_t j=0;j<count_;++j) { if (records_[index(j)].tick<=tick) before=j; else break; }
    if (before==count_) return fail(Error::RecoveryUnavailable);
    auto a=index(before); const auto first=records_[a].tick;
    if (first==tick && fraction==0) { std::memcpy(output.data(),state(a).data(),output.size()); return {}; }
    if (before+1==count_) return fail(Error::NotReady);
    if (!supports(descriptor_.capabilities,SimulationCapability::Interpolation)) return fail(Error::Unsupported);
    auto b=index(before+1); const auto width=records_[b].tick-first;
    if (!width || width>config_.maximum_bracket_ticks) return fail(Error::RecoveryUnavailable);
    const auto alpha=(static_cast<double>(tick-first)+fraction)/static_cast<double>(width);
    auto staged=scratch_.first(descriptor_.state_bytes);
    if (auto result=simulation_->interpolate(state(a),state(b),alpha,staged);!result) return result;
    if (auto valid=simulation_->validate_state(staged);!valid) return valid;
    std::memcpy(output.data(),staged.data(),output.size()); return {};
}
Status SnapshotHistory::reset_epoch(Epoch epoch) noexcept {
    if (!simulation_) return fail(Error::NotReady); if (epoch<=config_.epoch) return fail(Error::StaleEpoch);
    config_.epoch=epoch; head_=count_=0; for (auto& r:records_) r={}; return {};
}

PredictedSpawns::PredictedSpawns(PredictedSpawns&& other) noexcept { *this=std::move(other); }
PredictedSpawns& PredictedSpawns::operator=(PredictedSpawns&& other) noexcept {
    if (this!=&other) { epoch_=other.epoch_; peer_=other.peer_; next_sequence_=other.next_sequence_; records_=std::exchange(other.records_,{}); } return *this;
}
Result<PredictedSpawns> PredictedSpawns::create(Epoch epoch,PeerId peer,std::span<PredictedSpawnRecord> records,std::uint64_t watermark) noexcept {
    if (!epoch || !peer || records.empty() || records.size()>4096) return fail(Error::InvalidArgument);
    for (auto& r:records) r={}; PredictedSpawns result(epoch,peer,records); result.next_sequence_=watermark; return result;
}
Result<PredictedSpawnRecord*> PredictedSpawns::find(PredictedSpawnId id) noexcept {
    if (records_.empty()) return fail(Error::NotReady); if (id.epoch!=epoch_) return fail(Error::StaleEpoch);
    if (id.peer!=peer_ || !id.sequence) return fail(Error::PermissionDenied);
    for (auto& r:records_) if (r.phase!=PredictedSpawnPhase::Empty && r.id==id) return &r;
    return fail(Error::StaleGeneration);
}
Result<PredictedSpawnId> PredictedSpawns::begin() noexcept {
    if (records_.empty()) return fail(Error::NotReady); if (next_sequence_==UINT64_MAX) return fail(Error::CounterExhausted);
    for (auto& r:records_) if (r.phase==PredictedSpawnPhase::Empty) { r={{epoch_,peer_,++next_sequence_},PredictedSpawnPhase::Pending,{},0}; return r.id; }
    return fail(Error::CapacityExceeded);
}
Status PredictedSpawns::confirm(PredictedSpawnId id,ObjectHandle handle,std::uint64_t ownership) noexcept {
    auto found=find(id); if (!found) return fail(found.error()); auto& r=**found;
    if (!handle || !handle.slot() || !ownership) return fail(Error::InvalidArgument);
    if (r.phase==PredictedSpawnPhase::Confirmed) return r.canonical==handle && r.ownership_revision==ownership?Status{}:Status(fail(Error::ProtocolViolation));
    if (r.phase!=PredictedSpawnPhase::Pending) return fail(Error::NotReady);
    for (const auto& other:records_) if (other.phase==PredictedSpawnPhase::Confirmed && other.canonical==handle) return fail(Error::ProtocolViolation);
    r.phase=PredictedSpawnPhase::Confirmed; r.canonical=handle; r.ownership_revision=ownership; return {};
}
Status PredictedSpawns::reject(PredictedSpawnId id) noexcept {
    auto found=find(id); if (!found) return fail(found.error()); auto& r=**found;
    if (r.phase==PredictedSpawnPhase::Rejected) return {};
    if (r.phase!=PredictedSpawnPhase::Pending) return fail(Error::NotReady);
    r.phase=PredictedSpawnPhase::Rejected; return {};
}
Status PredictedSpawns::release(PredictedSpawnId id) noexcept {
    auto found=find(id); if (!found) return fail(found.error());
    if ((**found).phase==PredictedSpawnPhase::Pending) return fail(Error::Busy); **found={}; return {};
}
Status PredictedSpawns::canonical_destroyed(ObjectHandle handle) noexcept {
    if (records_.empty()) return fail(Error::NotReady); if (!handle || !handle.slot()) return fail(Error::InvalidArgument);
    for (auto& r:records_) if (r.phase==PredictedSpawnPhase::Confirmed && r.canonical==handle) { r.phase=PredictedSpawnPhase::Retired; return {}; }
    return fail(Error::StaleGeneration);
}
Result<PredictedSpawnRecord> PredictedSpawns::inspect(PredictedSpawnId id) const noexcept {
    if (records_.empty()) return fail(Error::NotReady); if (id.epoch!=epoch_) return fail(Error::StaleEpoch);
    if (id.peer!=peer_ || !id.sequence) return fail(Error::PermissionDenied);
    for (const auto& r:records_) if (r.phase!=PredictedSpawnPhase::Empty && r.id==id) return r; return fail(Error::StaleGeneration);
}
Status PredictedSpawns::reset_epoch(Epoch epoch) noexcept {
    if (records_.empty()) return fail(Error::NotReady); if (epoch<=epoch_) return fail(Error::StaleEpoch);
    for (auto& r:records_) r={}; epoch_=epoch; next_sequence_=0; return {};
}

Status restore_participants(Epoch epoch,Tick tick,std::span<const RecoveryPart> parts) noexcept {
    if (!epoch || parts.empty() || parts.size()>16) return fail(Error::InvalidArgument);
    std::array<std::uint64_t,16> ids{};
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& p=parts[i]; if (!p.participant) return fail(Error::InvalidArgument);
        auto d=p.participant->descriptor(); if (!d.portable) return fail(Error::Unsupported);
        if (!d.id || !d.schema_version || !d.maximum_checkpoint_bytes || p.canonical.empty() || p.canonical.size()>d.maximum_checkpoint_bytes) return fail(Error::InvalidArgument);
        if (p.schema_version!=d.schema_version) return fail(Error::IncompatibleSchema);
        for (std::size_t j=0;j<i;++j) if (ids[j]==d.id || parts[j].participant==p.participant) return fail(Error::InvalidArgument);
        ids[i]=d.id;
    }
    for (std::size_t i=0;i<parts.size();++i) {
        if (auto result=parts[i].participant->stage_restore(epoch,tick,parts[i].canonical);!result) {
            for (std::size_t j=0;j<=i;++j) parts[j].participant->abort_restore(); return result;
        }
    }
    for (const auto& p:parts) p.participant->commit_restore(); return {};
}
} // namespace superpos
