#include "superpos/replica_session.hpp"
#include <algorithm>
#include <utility>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
bool transient(Error error) noexcept { return error==Error::Busy || error==Error::OutOfMemory || error==Error::CapacityExceeded || error==Error::NotReady; }
bool stale(Error error) noexcept { return error==Error::StaleEpoch || error==Error::StaleGeneration; }
}
Result<ReplicaReceiverSession> ReplicaReceiverSession::create(Session& session,PeerReplicaReceiver& receiver,ReplicaApplication& application,
    ReplicaWireContext context,ReplicaSessionRoutes routes,std::span<ReplicaReplySlot> replies,std::span<PendingReplicaSpawn> spawns,std::span<std::byte> wire) noexcept {
    if(!context.authority || !context.connection || !context.replica || routes.control==routes.state || routes.control==routes.bulk || routes.state==routes.bulk ||
        replies.empty() || replies.size()>maximum_replies || spawns.empty() || spawns.size()>maximum_pending_spawns || wire.size()<minimum_wire_scratch ||
        overlap(std::as_bytes(replies),std::as_bytes(spawns)) || overlap(std::as_bytes(replies),wire) || overlap(std::as_bytes(spawns),wire))return fail(Error::InvalidArgument);
    auto admitted=session.capabilities(); if(!admitted)return fail(admitted.error());
    auto identity=session.identity(); if(!identity)return fail(identity.error());
    auto fingerprint=receiver.schema_fingerprint(); if(!fingerprint)return fail(fingerprint.error());
    auto config=receiver.configuration(); if(!config)return fail(config.error());
    auto instance=receiver.instance_identity(); if(!instance)return fail(instance.error());
    for(auto range:{std::as_bytes(replies),std::as_bytes(spawns),std::span<const std::byte>(wire)}){
        auto session_alias=session.storage_overlaps(range),receiver_alias=receiver.storage_overlaps(range);if(!session_alias)return fail(session_alias.error());if(!receiver_alias)return fail(receiver_alias.error());
        if(*session_alias||*receiver_alias)return fail(Error::InvalidArgument);
    }
    if(*fingerprint!=identity->schemas)return fail(Error::IncompatibleSchema);
    if(identity->local_peer!=config->peer)return fail(Error::AuthenticationFailed);
    if(identity->connection_epoch!=context.connection || config->authority_epoch!=context.authority || config->connection_epoch!=context.connection || config->replica_epoch!=context.replica)return fail(Error::StaleEpoch);
    if(spawns.size()<config->maximum_transitions)return fail(Error::CapacityExceeded);
    auto control=session.channel_mode(routes.control),state=session.channel_mode(routes.state),bulk=session.channel_mode(routes.bulk);
    if(!control)return fail(control.error()); if(!state)return fail(state.error()); if(!bulk)return fail(bulk.error());
    if(*control!=DeliveryMode::ReliableOrdered || *bulk!=DeliveryMode::ReliableUnordered || (*state!=DeliveryMode::ReliableUnordered && *state!=DeliveryMode::Unreliable))return fail(Error::Unsupported);
    auto purpose=session.channel_purpose(routes.control);if(!purpose)return fail(purpose.error());if(*purpose!=ChannelPurpose::Control)return fail(Error::Unsupported);
    auto control_cap=session.channel_message_bytes(routes.control);if(!control_cap)return fail(control_cap.error());if(*control_cap!=4096)return fail(Error::Unsupported);
    auto control_lane=session.channel_carrier(routes.control);if(!control_lane)return fail(control_lane.error());if(*control_lane!=CarrierLane::Control)return fail(Error::Unsupported);
    for(auto channel:{routes.state,routes.bulk}){auto lane=session.channel_carrier(channel);if(!lane)return fail(lane.error());if(*lane!=CarrierLane::State)return fail(Error::Unsupported);}
    for(auto channel:{routes.state,routes.bulk}){auto declared=session.channel_purpose(channel);if(!declared)return fail(declared.error());if(*declared!=ChannelPurpose::Application)return fail(Error::Unsupported);}
    ReplicaReceiverSession bridge; bridge.session_=&session; bridge.receiver_=&receiver; bridge.application_=&application; bridge.context_=context; bridge.routes_=routes;
    bridge.identity_=*identity; bridge.receiver_config_=*config; bridge.receiver_instance_=*instance;
    bridge.replies_=replies; bridge.spawns_=spawns; bridge.wire_=wire.first(minimum_wire_scratch);
    for(auto& reply:replies)reply={}; for(auto& spawn:spawns)spawn={}; return bridge;
}
ReplicaReceiverSession::ReplicaReceiverSession(ReplicaReceiverSession&& other) noexcept { *this=std::move(other); }
ReplicaReceiverSession& ReplicaReceiverSession::operator=(ReplicaReceiverSession&& other) noexcept {
    if(this!=&other) { session_=std::exchange(other.session_,nullptr); receiver_=std::exchange(other.receiver_,nullptr); application_=std::exchange(other.application_,nullptr);
        context_=other.context_; routes_=other.routes_; identity_=other.identity_; receiver_config_=other.receiver_config_; receiver_instance_=other.receiver_instance_; replies_=std::exchange(other.replies_,{}); spawns_=std::exchange(other.spawns_,{}); wire_=std::exchange(other.wire_,{});
        consumed_pending_=other.consumed_pending_; reply_issued_=other.reply_issued_; owner_=other.owner_; failed_=other.failed_; }
    return *this;
}
ReplicaReplySlot* ReplicaReceiverSession::vacant_reply() noexcept { for(auto& slot:replies_)if(slot.phase==ReplicaReplyPhase::Empty)return &slot; return nullptr; }
Status ReplicaReceiverSession::binding_valid() const noexcept {
    auto identity=session_->identity(); if(!identity)return fail(identity.error());
    auto instance=receiver_->instance_identity(); if(!instance)return fail(instance.error());
    // Instance comparisons close same-address replacement even when every
    // admitted logical identity, epoch and descriptor remains byte-identical.
    if(identity->instance!=identity_.instance || *instance!=receiver_instance_)return fail(Error::StaleEpoch);
    auto config=receiver_->configuration(); if(!config)return fail(config.error());
    auto fingerprint=receiver_->schema_fingerprint(); if(!fingerprint)return fail(fingerprint.error());
    if(*fingerprint!=identity_.schemas || identity->schemas!=identity_.schemas)return fail(Error::IncompatibleSchema);
    if(*identity!=identity_ || *config!=receiver_config_)return fail(Error::StaleEpoch);
    return {};
}
Status ReplicaReceiverSession::flush(Tick tick,ReplicaSessionProgress& progress) noexcept {
    for(auto& slot:replies_) {
        if(slot.phase==ReplicaReplyPhase::AwaitingApplied) {
            auto outcome=session_->outcome(slot.ticket); if(!outcome)return fail(outcome.error());
            if(*outcome==Outcome::Applied) { if(auto retired=session_->retire(slot.ticket);!retired)return retired; slot={}; }
        }
    }
    for(std::size_t count=0;count<replies_.size();++count) {
        ReplicaReplySlot* oldest=nullptr;
        for(auto& slot:replies_)if(slot.phase==ReplicaReplyPhase::Queued && (!oldest || slot.order<oldest->order))oldest=&slot;
        if(!oldest)break;
        auto encoded=encode_replica_message(oldest->message,wire_); if(!encoded)return fail(encoded.error());
        auto sent=session_->send(wire_.first(*encoded),tick,routes_.control);
        if(!sent) { if(transient(sent.error())) { ++progress.backpressured; break; } return fail(sent.error()); }
        oldest->ticket=*sent; oldest->phase=ReplicaReplyPhase::AwaitingApplied; ++progress.sent;
    } return {};
}
Status ReplicaReceiverSession::consume(std::uint8_t channel,ReplicaLane lane,ReplicaSessionProgress& progress) noexcept {
    auto& consumed=consumed_pending_[static_cast<unsigned>(lane)];
    if(consumed) { if(auto applied=session_->applied(consumed,channel);!applied)return applied; consumed=0; ++progress.consumed; }
    auto ready=session_->receive(channel); if(!ready) { if(ready.error()==Error::NotReady)return {}; return fail(ready.error()); }
    auto decoded=decode_replica_message(ready->payload); if(!decoded)return fail(decoded.error()); auto& m=*decoded;
    auto mode=session_->channel_mode(channel); if(!mode)return fail(mode.error());
    if(auto role=validate_replica_delivery(m.kind,lane,*mode);!role)return role;
    auto release=[&]() -> Status { consumed=ready->message; auto applied=session_->applied(consumed,channel); if(applied) { consumed=0; ++progress.consumed; } return applied; };
    if(m.context!=context_) { ++progress.discarded; return release(); }
    if((lane==ReplicaLane::State || lane==ReplicaLane::Bulk) && m.sequence>receiver_->lifecycle_applied())return fail(Error::Busy);
    auto* reply=vacant_reply(); if(!reply)return fail(Error::Busy);
    if(reply_issued_==UINT64_MAX)return fail(Error::CounterExhausted);
    ReplicaWireMessage answer; answer.context=context_;
    auto result=[&]() -> Status {
        switch(m.kind) {
        case ReplicaWireKind::Bind: {
            const bool fresh=m.binding.lifecycle_sequence>receiver_->lifecycle_applied(); PendingReplicaSpawn* pending=nullptr;
            if(fresh) { for(auto& spawn:spawns_)if(!spawn.occupied) { pending=&spawn; break; } if(!pending)return fail(Error::CapacityExceeded); }
            auto applied=receiver_->bind(m.binding); if(!applied)return fail(applied.error());
            if(pending)*pending={true,m.binding.key}; answer.kind=ReplicaWireKind::LifecycleApplied; answer.sequence=applied->sequence; return {};
        }
        case ReplicaWireKind::BaselineOffer: {
            auto pinned=receiver_->offer(m.baseline,m.tick); if(!pinned)return fail(pinned.error()); answer.kind=ReplicaWireKind::BaselinePinned; answer.pinned=*pinned; return {};
        }
        case ReplicaWireKind::BaselineRetire: {
            auto applied=receiver_->retire_baseline(m.retirement); if(!applied)return fail(applied.error()); answer.kind=ReplicaWireKind::LifecycleApplied; answer.sequence=applied->sequence; return {};
        }
        case ReplicaWireKind::Leave: {
            auto applied=receiver_->leave(m.key,m.sequence,*application_); if(!applied)return fail(applied.error());
            for(auto& pending:spawns_)if(pending.occupied && pending.key==m.key)pending={};
            answer.kind=ReplicaWireKind::LifecycleApplied; answer.sequence=applied->sequence; return {};
        }
        case ReplicaWireKind::OwnershipReset: {
            auto applied=receiver_->reset_ownership(m.key,m.new_key,m.owner,m.ownership_revision,m.sequence,*application_); if(!applied)return fail(applied.error());
            answer.kind=ReplicaWireKind::LifecycleApplied; answer.sequence=applied->sequence; return {};
        }
        case ReplicaWireKind::Publication: {
            auto applied=receiver_->publish(std::span(m.patches).first(m.count),m.group_id,m.tick,answer.applied,answer.requested,*application_); if(!applied)return fail(applied.error());
            answer.kind=applied->disposition==PublicationDisposition::NeedRepair?ReplicaWireKind::RepairRequest:ReplicaWireKind::StateApplied;
            answer.count=applied->repairs?applied->repairs:applied->receipts; return {};
        }
        case ReplicaWireKind::FullRepair: {
            auto applied=receiver_->repair(std::span(m.repairs).first(m.count),m.group_id,m.tick,answer.applied,*application_); if(!applied)return fail(applied.error());
            answer.kind=ReplicaWireKind::StateApplied; answer.count=applied->receipts; return {};
        }
        default:return fail(Error::ProtocolViolation);
        }
    }();
    if(!result) { if(stale(result.error())) { ++progress.discarded; return release(); } return result; }
    reply->message=answer; reply->phase=ReplicaReplyPhase::Queued; reply->order=++reply_issued_;
    return release(); // If receipt capacity is Busy, the committed marker prevents a second application commit.
}
Status ReplicaReceiverSession::readiness(ReplicaSessionProgress& progress) noexcept {
    for(auto& pending:spawns_) { if(pending.occupied) {
        auto* reply=vacant_reply(); if(!reply)return fail(Error::Busy);
        if(reply_issued_==UINT64_MAX)return fail(Error::CounterExhausted);
        auto applied=receiver_->spawn_ready(pending.key,*application_);
        if(!applied) { if(transient(applied.error())) { if(applied.error()!=Error::NotReady)++progress.backpressured; continue; } if(stale(applied.error())) { pending={}; continue; } return fail(applied.error()); }
        reply->message={}; reply->message.kind=ReplicaWireKind::SpawnApplied; reply->message.context=context_; reply->message.spawned=*applied; reply->phase=ReplicaReplyPhase::Queued; reply->order=++reply_issued_;
        pending={}; ++progress.spawned;
    } }
    return {};
}
Result<ReplicaSessionProgress> ReplicaReceiverSession::pump(Tick tick) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied); if(!session_)return fail(Error::NotReady); if(failed_)return fail(Error::ChannelFailed);
    if(pumping_)return fail(Error::Busy); pumping_=true;
    struct PumpScope { bool& active; ~PumpScope() { active=false; } } scope{pumping_};
    // Validate before Session pumping, receipt retirement or application calls.
    if(auto valid=binding_valid();!valid) { if(!transient(valid.error()))failed_=true; return fail(valid.error()); }
    ReplicaSessionProgress progress;
    auto handle=[&](Status result) -> Status { if(result)return {}; if(transient(result.error())) { ++progress.backpressured; return {}; } failed_=true; return result; };
    if(auto result=handle(session_->pump(tick));!result)return fail(result.error());
    if(auto result=handle(flush(tick,progress));!result)return fail(result.error());
    if(auto result=handle(consume(routes_.control,ReplicaLane::Control,progress));!result)return fail(result.error());
    if(auto result=handle(consume(routes_.bulk,ReplicaLane::Bulk,progress));!result)return fail(result.error());
    if(auto result=handle(consume(routes_.state,ReplicaLane::State,progress));!result)return fail(result.error());
    if(auto result=handle(readiness(progress));!result)return fail(result.error());
    if(auto result=handle(flush(tick,progress));!result)return fail(result.error()); return progress;
}
}
