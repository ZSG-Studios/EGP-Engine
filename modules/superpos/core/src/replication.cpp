#include "superpos/replication.hpp"
#include <algorithm>
#include <cstring>
#include <utility>
#include <atomic>

namespace superpos {
namespace {
Result<std::uint64_t> new_sender_instance() noexcept {
    static std::atomic<std::uint64_t> next{1}; auto current=next.load(std::memory_order_relaxed);
    for(;;) { if(current==UINT64_MAX)return fail(Error::CounterExhausted); if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current; }
}
bool overlaps(const void* a,std::size_t an,const void* b,std::size_t bn) noexcept {
    if(!an||!bn)return false;
    auto aa=reinterpret_cast<std::uintptr_t>(a),bb=reinterpret_cast<std::uintptr_t>(b);
    return aa<=bb?bb-aa<an:aa-bb<bn;
}
}

PeerReplicas::PeerReplicas(PeerReplicas&& other) noexcept { *this=std::move(other); }
PeerReplicas& PeerReplicas::operator=(PeerReplicas&& other) noexcept {
    if(this!=&other) {
        owner_thread_=other.owner_thread_; config_=other.config_; records_=std::exchange(other.records_,{}); images_=std::exchange(other.images_,{});
        active_=std::exchange(other.active_,0); ready_=std::exchange(other.ready_,0); transitions_=std::exchange(other.transitions_,0);
        control_issued_=std::exchange(other.control_issued_,0); control_applied_=std::exchange(other.control_applied_,0); token_issued_=std::exchange(other.token_issued_,0);
        instance_=std::exchange(other.instance_,0);
    } return *this;
}
Result<PeerReplicas> PeerReplicas::create(ReplicaConfig config,
    std::span<ReplicaRecord> records, std::span<std::byte> images) noexcept {
    if (!config.authority_epoch || !config.connection_epoch || !config.replica_epoch ||
        !config.maximum_active || !config.maximum_transitions || !config.state_stride ||
        config.state_stride > Schema::maximum_state_bytes || records.empty() || records.size() > UINT16_MAX ||
        config.maximum_active > records.size() || config.maximum_transitions > records.size())
        return fail(Error::InvalidArgument);
    if (records.size() > images.size() / config.state_stride / 2) return fail(Error::CapacityExceeded);
    if(overlaps(records.data(),records.size_bytes(),images.data(),images.size()))return fail(Error::InvalidArgument);
    auto instance=new_sender_instance(); if(!instance)return fail(instance.error());
    for (auto& record : records) record = ReplicaRecord{};
    PeerReplicas replicas; replicas.config_ = config; replicas.records_ = records; replicas.images_ = images;
    replicas.instance_=*instance;
    return replicas;
}
ReplicaKey PeerReplicas::key(std::size_t i) const noexcept {
    const auto& record = records_[i];
    return {static_cast<std::uint16_t>(i + 1), record.incarnation, config_.authority_epoch,
        config_.connection_epoch, config_.replica_epoch, record.encoding_epoch};
}
Result<std::size_t> PeerReplicas::index(const ReplicaKey& k) const noexcept {
    if(!owned())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady);
    if (k.authority_epoch != config_.authority_epoch || k.connection_epoch != config_.connection_epoch ||
        k.replica_epoch != config_.replica_epoch) return fail(Error::StaleEpoch);
    if (!k.slot || k.slot > records_.size()) return fail(Error::StaleGeneration);
    auto i = static_cast<std::size_t>(k.slot - 1); const auto& record = records_[i];
    if (record.phase == ReplicaPhase::Empty || k.incarnation != record.incarnation) return fail(Error::StaleGeneration);
    if (k.encoding_epoch != record.encoding_epoch) return fail(Error::StaleEpoch);
    return i;
}
std::span<std::byte> PeerReplicas::image(std::size_t i, std::size_t n) noexcept {
    return images_.subspan((2 * i + n) * config_.state_stride, records_[i].schema->state_bytes());
}
std::span<const std::byte> PeerReplicas::image(std::size_t i, std::size_t n) const noexcept {
    return images_.subspan((2 * i + n) * config_.state_stride, records_[i].schema->state_bytes());
}
void PeerReplicas::clear_images(std::size_t i) noexcept {
    auto& record = records_[i];
    for (std::size_t n = 0; n < 2; ++n) {
        auto bytes = image(i, n); std::fill(bytes.begin(), bytes.end(), std::byte{}); record.images[n] = {};
    }
    record.active_image = record.offered_image = record.retiring_image = -1; record.retire_sequence = 0;
}
BaselineRetirement PeerReplicas::retirement(std::size_t i) const noexcept {
    const auto& record = records_[i];
    if (record.retiring_image < 0) return {key(i)};
    return {key(i), record.images[record.retiring_image].token,
        record.images[record.active_image].token, record.retire_sequence};
}
Result<ReplicaBinding> PeerReplicas::begin_spawn(const EntityView& entity) noexcept {
    if(!owned())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady);
    if (!entity.handle || !entity.schema || !entity.schema->id() || !entity.ownership_revision ||
        !entity.revision || entity.schema->state_bytes() > config_.state_stride)
        return fail(Error::InvalidArgument);
    if (auto s = entity.schema->validate(entity.canonical); !s) return fail(s.error());
    if (active_ >= config_.maximum_active || transitions_ >= config_.maximum_transitions) return fail(Error::CapacityExceeded);
    if (control_issued_ == UINT64_MAX) return fail(Error::CounterExhausted);
    std::size_t free = records_.size();
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = records_[i];
        if (record.phase != ReplicaPhase::Empty && record.handle.slot() == entity.handle.slot())
            return fail(record.handle==entity.handle?Error::Busy:Error::StaleGeneration);
        if (free == records_.size() && record.phase == ReplicaPhase::Empty && record.incarnation != UINT64_MAX) free = i;
    }
    if (free == records_.size()) return fail(Error::CapacityExceeded);
    auto& record = records_[free]; auto incarnation = record.incarnation + 1;
    record = ReplicaRecord{}; record.incarnation = incarnation; record.phase = ReplicaPhase::SpawnPending;
    record.handle = entity.handle; record.owner = entity.owner; record.schema = entity.schema; record.ownership_revision = entity.ownership_revision;
    record.spawn_sequence = ++control_issued_; ++active_; ++transitions_;
    return ReplicaBinding{key(free), entity.handle, record.spawn_sequence};
}
Status PeerReplicas::spawn_applied(const ReplicaKey& k, std::uint64_t revision, std::uint64_t ownership) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase == ReplicaPhase::Retiring) return fail(Error::NotReady);
    if (record.phase == ReplicaPhase::Ready) {
        if (ownership != record.ownership_revision || revision != record.initial_applied_revision) return fail(Error::ProtocolViolation);
        return {};
    }
    if (ownership != record.ownership_revision || record.active_image < 0 ||
        revision != record.images[record.active_image].revision) return fail(Error::ProtocolViolation);
    record.phase = ReplicaPhase::Ready; ++ready_; --transitions_;
    record.initial_applied_revision = revision;
    record.state_applied_revision = revision;
    return {};
}
Result<std::uint64_t> PeerReplicas::begin_retire(const ReplicaKey& k) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase == ReplicaPhase::Retiring) return record.leave_sequence;
    if (control_issued_ == UINT64_MAX) return fail(Error::CounterExhausted);
    if (record.phase == ReplicaPhase::Ready && transitions_ >= config_.maximum_transitions) return fail(Error::CapacityExceeded);
    if (record.phase == ReplicaPhase::Ready) { --ready_; ++transitions_; }
    record.phase = ReplicaPhase::Retiring; record.leave_sequence = ++control_issued_; --active_;
    return record.leave_sequence;
}
Status PeerReplicas::control_applied(std::uint64_t sequence) noexcept {
    if(!owned())return fail(Error::PermissionDenied);
    if(records_.empty())return fail(Error::NotReady);
    if (sequence > control_issued_) return fail(Error::ProtocolViolation);
    if (sequence <= control_applied_) return {};
    for (std::size_t i = 0; i < records_.size(); ++i) {
        auto& record = records_[i];
        if (record.phase == ReplicaPhase::Empty) continue;
        if (record.phase == ReplicaPhase::Retiring && record.leave_sequence <= sequence) {
            auto incarnation = record.incarnation; clear_images(i); record = ReplicaRecord{}; record.incarnation = incarnation;
            --transitions_;
        } else if (record.retiring_image >= 0 && record.retire_sequence <= sequence) {
            auto n = static_cast<std::size_t>(record.retiring_image); auto bytes = image(i, n);
            std::fill(bytes.begin(), bytes.end(), std::byte{}); record.images[n] = {};
            record.retiring_image = -1; record.retire_sequence = 0;
        }
        // Preserve the last reset fence for scheduler rebinding; pending_reset
        // remains unavailable after acknowledgement through the watermark.
        if(record.reset_sequence && record.reset_sequence<=sequence)record.previous_encoding_epoch=0;
    }
    control_applied_ = sequence;
    return {};
}
Result<BaselineOffer> PeerReplicas::offer_baseline(const ReplicaKey& k,
    std::span<const std::byte> canonical, std::uint64_t revision, std::uint64_t now) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase == ReplicaPhase::Retiring) return fail(Error::NotReady);
    // Readiness acknowledges the immutable initial image. A lost SpawnApplied
    // receipt must not let promotion replace that image before Ready.
    if (record.phase == ReplicaPhase::SpawnPending && record.active_image >= 0) return fail(Error::Busy);
    if (!revision) return fail(Error::InvalidArgument);
    if (record.active_image < 0 && revision < record.last_sent_revision) return fail(Error::InvalidArgument);
    if (auto s = record.schema->validate(canonical); !s) return fail(s.error());
    if (record.offered_image >= 0 || record.retiring_image >= 0) return fail(Error::Busy);
    if (record.active_image >= 0 && (now < record.last_offer_milliseconds ||
        now - record.last_offer_milliseconds < config_.offer_interval_milliseconds)) return fail(Error::Busy);
    if (token_issued_ == UINT64_MAX) return fail(Error::CounterExhausted);
    auto n = static_cast<std::size_t>(record.active_image == 0 ? 1 : 0);
    // Ordinary peer replication never transfers authority-only recovery state.
    // Every full-width peer identity, including zero, is compared explicitly.
    if (auto s = record.schema->project(canonical, config_.peer == record.owner,
        false, image(*i, n)); !s) return fail(s.error());
    record.images[n] = {++token_issued_, revision, false}; record.offered_image = static_cast<std::int8_t>(n);
    record.last_offer_milliseconds = now; record.last_sent_revision = std::max(record.last_sent_revision, revision);
    return pending_offer(k);
}
Result<BaselineOffer> PeerReplicas::pending_offer(const ReplicaKey& k) const noexcept {
    auto i = index(k); if (!i) return fail(i.error()); const auto& record = records_[*i];
    if (record.offered_image < 0) return fail(Error::MissingBaseline);
    auto n = static_cast<std::size_t>(record.offered_image);
    return BaselineOffer{k, record.active_image < 0 ? 0 : record.images[record.active_image].token,
        record.images[n].token, record.images[n].revision, image(*i, n)};
}
Result<BaselineRetirement> PeerReplicas::baseline_pinned(const ReplicaKey& k,
    std::uint64_t token, std::uint64_t revision) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase == ReplicaPhase::Retiring) return fail(Error::NotReady);
    if (record.active_image >= 0 && record.images[record.active_image].token == token) {
        if (record.images[record.active_image].revision != revision) return fail(Error::ProtocolViolation);
        return retirement(*i);
    }
    if (record.offered_image < 0 || record.images[record.offered_image].token != token) return fail(Error::MissingBaseline);
    if (record.images[record.offered_image].revision != revision) return fail(Error::ProtocolViolation);
    if (record.active_image >= 0 && control_issued_ == UINT64_MAX) return fail(Error::CounterExhausted);
    auto n = record.offered_image; record.images[n].pinned = true;
    if (record.active_image >= 0) { record.retiring_image = record.active_image; record.retire_sequence = ++control_issued_; }
    record.active_image = n; record.offered_image = -1;
    return retirement(*i);
}
Result<std::span<const std::byte>> PeerReplicas::active_baseline(const ReplicaKey& k, std::uint64_t token) const noexcept {
    auto i = index(k); if (!i) return fail(i.error()); const auto& record = records_[*i];
    if (record.phase == ReplicaPhase::Retiring) return fail(Error::NotReady);
    if (record.active_image < 0 || record.images[record.active_image].token != token) return fail(Error::MissingBaseline);
    return image(*i, static_cast<std::size_t>(record.active_image));
}
Status PeerReplicas::note_state_sent(const ReplicaKey& k, std::uint64_t revision) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase != ReplicaPhase::Ready || record.active_image < 0) return fail(Error::NotReady);
    if (revision < record.last_sent_revision || !revision) return fail(Error::InvalidArgument);
    record.last_sent_revision = revision; return {};
}
Status PeerReplicas::state_applied(const ReplicaKey& k, std::uint64_t revision) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if (record.phase != ReplicaPhase::Ready) return fail(Error::NotReady);
    if (!revision || revision > record.last_sent_revision) return fail(Error::ProtocolViolation);
    record.state_applied_revision = std::max(record.state_applied_revision, revision);
    return {};
}
Result<ReplicaEncodingReset> PeerReplicas::issue_reset(const ReplicaKey& k,PeerId owner,std::uint64_t revision) noexcept {
    auto i = index(k); if (!i) return fail(i.error()); auto& record = records_[*i];
    if(record.phase!=ReplicaPhase::Ready)return fail(Error::NotReady);
    if(record.reset_sequence>control_applied_)return fail(Error::Busy);
    if(record.encoding_epoch==UINT64_MAX || control_issued_==UINT64_MAX)return fail(Error::CounterExhausted);
    if(revision<record.ownership_revision || (revision==record.ownership_revision && owner!=record.owner))return fail(Error::InvalidArgument);
    clear_images(*i); record.previous_encoding_epoch=record.encoding_epoch; ++record.encoding_epoch; record.owner=owner; record.ownership_revision=revision;
    record.reset_sequence=++control_issued_; return ReplicaEncodingReset{k,key(*i),owner,revision,record.reset_sequence};
}
Result<ReplicaEncodingReset> PeerReplicas::reset_encoding(const ReplicaKey& k) noexcept {
    auto i=index(k); if(!i)return fail(i.error()); const auto& record=records_[*i]; return issue_reset(k,record.owner,record.ownership_revision);
}
Result<ReplicaEncodingReset> PeerReplicas::ownership_changed(const ReplicaKey& k, PeerId owner, std::uint64_t revision) noexcept {
    auto i = index(k); if (!i) return fail(i.error());
    if (revision <= records_[*i].ownership_revision) return fail(Error::InvalidArgument);
    return issue_reset(k,owner,revision);
}
Result<ReplicaEncodingReset> PeerReplicas::pending_reset(const ReplicaKey& fresh) const noexcept {
    auto i=index(fresh); if(!i)return fail(i.error()); const auto& record=records_[*i];
    if(!record.reset_sequence || record.reset_sequence<=control_applied_)return fail(Error::NotReady);
    auto old=fresh; old.encoding_epoch=record.previous_encoding_epoch;
    return ReplicaEncodingReset{old,fresh,record.owner,record.ownership_revision,record.reset_sequence};
}
Result<ReplicaRecord> PeerReplicas::inspect(const ReplicaKey& k) const noexcept {
    auto i = index(k); if (!i) return fail(i.error()); return records_[*i];
}
Result<ReplicaConfig> PeerReplicas::configuration() const noexcept { if(!owned())return fail(Error::PermissionDenied); if(records_.empty())return fail(Error::NotReady); return config_; }
Result<std::uint64_t> PeerReplicas::instance_identity() const noexcept { if(!owned())return fail(Error::PermissionDenied); if(records_.empty())return fail(Error::NotReady); return instance_; }
Result<std::size_t> PeerReplicas::slot_capacity() const noexcept { if(!owned())return fail(Error::PermissionDenied); if(records_.empty())return fail(Error::NotReady); return records_.size(); }
Result<bool> PeerReplicas::storage_overlaps(std::span<const std::byte> bytes) const noexcept {
    if(!owned())return fail(Error::PermissionDenied);if(records_.empty())return fail(Error::NotReady);
    return overlaps(this,sizeof(*this),bytes.data(),bytes.size())||overlaps(records_.data(),records_.size_bytes(),bytes.data(),bytes.size())||overlaps(images_.data(),images_.size(),bytes.data(),bytes.size());
}
}
