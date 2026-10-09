#include "superpos/replica_authority.hpp"
#include <algorithm>
#include <utility>

namespace superpos {
namespace {
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty()||b.empty())return false;const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());return x<=y?y-x<a.size():x-y<b.size();
}
bool transient(Error e) noexcept {return e==Error::Busy||e==Error::CapacityExceeded||e==Error::OutOfMemory||e==Error::NotReady;}
bool stale(Error e) noexcept {return e==Error::StaleEpoch||e==Error::StaleGeneration;}
Status projected(const Schema& schema,std::span<const std::byte> state,bool owner) noexcept {
    if(auto valid=schema.validate(state);!valid)return valid;
    for(const auto& f:schema.fields())if(f.audience==FieldAudience::Authority||(f.audience==FieldAudience::Owner&&!owner))
        for(auto byte:state.subspan(f.offset,f.size))if(byte!=std::byte{})return fail(Error::PermissionDenied);
    return {};
}
}
Result<ReplicaAuthoritySession> ReplicaAuthoritySession::create(Session& session,PeerReplicas& sender,const FrozenRegistry& registry,ReplicaWireContext context,ReplicaSessionRoutes routes,
    std::span<AuthorityCapture> captures,std::span<std::byte> arena,std::span<AuthorityExposure> exposures,std::span<RepairRequest> repairs,std::span<std::byte> scratch) noexcept {
    if(!context.authority||!context.connection||!context.replica||routes.control==routes.state||routes.control==routes.bulk||routes.state==routes.bulk||captures.empty()||captures.size()>maximum_captures||
        exposures.empty()||exposures.size()>maximum_exposures||repairs.empty()||repairs.size()>maximum_repairs||arena.size()<captures.size()*capture_bytes||scratch.size()<65536)return fail(Error::InvalidArgument);
    const std::array ranges{std::as_bytes(captures),std::span<const std::byte>(arena),std::as_bytes(exposures),std::as_bytes(repairs),std::span<const std::byte>(scratch)};
    for(std::size_t a=0;a<ranges.size();++a)for(std::size_t b=a+1;b<ranges.size();++b)if(overlap(ranges[a],ranges[b]))return fail(Error::InvalidArgument);
    auto identity=session.identity();if(!identity)return fail(identity.error());auto config=sender.configuration();if(!config)return fail(config.error());
    auto instance=sender.instance_identity();if(!instance)return fail(instance.error());auto slots=sender.slot_capacity();if(!slots)return fail(slots.error());
    auto limits=session.delivery_limits();if(!limits)return fail(limits.error());
    for(auto range:ranges){auto session_alias=session.storage_overlaps(range),sender_alias=sender.storage_overlaps(range);if(!session_alias)return fail(session_alias.error());if(!sender_alias)return fail(sender_alias.error());
        if(*session_alias||*sender_alias||overlap(range,std::as_bytes(std::span(&registry,1))))return fail(Error::InvalidArgument);}
    if(registry.fingerprint()!=identity->schemas||!registry.size())return fail(Error::IncompatibleSchema);
    if(config->peer!=identity->remote_peer)return fail(Error::AuthenticationFailed);
    if(context.authority!=config->authority_epoch||context.connection!=config->connection_epoch||context.connection!=identity->connection_epoch||context.replica!=config->replica_epoch)return fail(Error::StaleEpoch);
    if(exposures.size()<*slots)return fail(Error::CapacityExceeded);if(sender.active_count()||sender.transition_count()||sender.lifecycle_issued()!=sender.lifecycle_applied())return fail(Error::NotReady);
    auto control=session.channel_mode(routes.control),state=session.channel_mode(routes.state),bulk=session.channel_mode(routes.bulk);
    if(!control)return fail(control.error());if(!state)return fail(state.error());if(!bulk)return fail(bulk.error());
    if(*control!=DeliveryMode::ReliableOrdered||*bulk!=DeliveryMode::ReliableUnordered||(*state!=DeliveryMode::ReliableUnordered&&*state!=DeliveryMode::Unreliable))return fail(Error::Unsupported);
    auto purpose=session.channel_purpose(routes.control);if(!purpose)return fail(purpose.error());if(*purpose!=ChannelPurpose::Control)return fail(Error::Unsupported);
    auto control_cap=session.channel_message_bytes(routes.control);if(!control_cap)return fail(control_cap.error());if(*control_cap!=4096)return fail(Error::Unsupported);
    auto control_lane=session.channel_carrier(routes.control);if(!control_lane)return fail(control_lane.error());if(*control_lane!=CarrierLane::Control)return fail(Error::Unsupported);
    for(auto channel:{routes.state,routes.bulk}){auto lane=session.channel_carrier(channel);if(!lane)return fail(lane.error());if(*lane!=CarrierLane::State)return fail(Error::Unsupported);}
    for(auto channel:{routes.state,routes.bulk}){auto declared=session.channel_purpose(channel);if(!declared)return fail(declared.error());if(*declared!=ChannelPurpose::Application)return fail(Error::Unsupported);}
    std::array<Schema,SchemaRegistry::maximum_schemas> views{};if(auto materialized=registry.materialize(views);!materialized)return fail(materialized.error());
    for(std::size_t n=0;n<registry.size();++n){auto catalog=registry.find(views[n].id());if(!catalog)return fail(catalog.error());for(auto range:ranges)
        if(overlap(std::as_bytes(std::span(*catalog,1)),range)||overlap(std::as_bytes(views[n].fields()),range)||overlap(std::as_bytes(views[n].rpcs()),range))return fail(Error::InvalidArgument);}
    ReplicaAuthoritySession bridge;bridge.session_=&session;bridge.sender_=&sender;bridge.registry_=&registry;bridge.identity_=*identity;bridge.config_=*config;bridge.limits_=*limits;bridge.sender_instance_=*instance;bridge.context_=context;bridge.routes_=routes;
    bridge.captures_=captures;bridge.arena_=arena.first(captures.size()*capture_bytes);bridge.exposures_=exposures;bridge.repairs_=repairs;bridge.scratch_=scratch.first(65536);
    bridge.control_queued_=bridge.control_sent_=sender.lifecycle_applied();
    for(auto& capture:captures)capture={};for(auto& exposure:exposures)exposure={};for(auto& repair:repairs)repair={};return bridge;
}
ReplicaAuthoritySession::ReplicaAuthoritySession(ReplicaAuthoritySession&& other) noexcept {*this=std::move(other);}
ReplicaAuthoritySession& ReplicaAuthoritySession::operator=(ReplicaAuthoritySession&& other) noexcept {
    if(this!=&other){session_=std::exchange(other.session_,nullptr);sender_=std::exchange(other.sender_,nullptr);registry_=std::exchange(other.registry_,nullptr);identity_=other.identity_;config_=other.config_;limits_=other.limits_;sender_instance_=other.sender_instance_;context_=other.context_;routes_=other.routes_;
        captures_=std::exchange(other.captures_,{});arena_=std::exchange(other.arena_,{});scratch_=std::exchange(other.scratch_,{});exposures_=std::exchange(other.exposures_,{});repairs_=std::exchange(other.repairs_,{});
        repair_count_=std::exchange(other.repair_count_,0);order_=other.order_;control_queued_=other.control_queued_;control_sent_=other.control_sent_;consumed_pending_=other.consumed_pending_;owner_=other.owner_;failed_=other.failed_;}return *this;
}
Status ReplicaAuthoritySession::binding_valid() const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(!session_)return fail(Error::NotReady);if(failed_)return fail(Error::ChannelFailed);
    auto identity=session_->identity();if(!identity)return fail(identity.error());auto instance=sender_->instance_identity();if(!instance)return fail(instance.error());
    auto config=sender_->configuration();if(!config)return fail(config.error());
    if(*identity!=identity_||*instance!=sender_instance_||*config!=config_)return fail(Error::StaleEpoch);
    if(!registry_->size()||registry_->fingerprint()!=identity_.schemas)return fail(Error::IncompatibleSchema);return {};
}
Result<ReplicaRecord> ReplicaAuthoritySession::record(const ReplicaKey& key) const noexcept {
    auto value=sender_->inspect(key);if(!value)return fail(value.error());auto schema=registry_->find(value->schema->id());if(!schema)return fail(schema.error());
    if(!value->schema->compatible_with((*schema)->schema))return fail(Error::IncompatibleSchema);return value;
}
AuthorityExposure* ReplicaAuthoritySession::exposure(const ReplicaKey& key) noexcept {
    if(!key.slot||key.slot>exposures_.size())return nullptr;auto& value=exposures_[key.slot-1];return value.occupied&&value.key==key?&value:nullptr;
}
AuthorityCapture* ReplicaAuthoritySession::vacant() noexcept {for(auto& capture:captures_)if(capture.phase==AuthorityCapturePhase::Empty)return &capture;return nullptr;}
std::span<std::byte> ReplicaAuthoritySession::storage(AuthorityCapture& capture) noexcept {return arena_.subspan(std::size_t(&capture-captures_.data())*capture_bytes,capture_bytes);}
Status ReplicaAuthoritySession::validate_group(const ReplicaWireMessage& m,bool sending) noexcept {
    if(!m.count||m.count>m.maximum_members)return fail(Error::InvalidArgument);
    if(overlap(std::as_bytes(std::span(&m,1)),scratch_))return fail(Error::InvalidArgument);
    // No member may borrow the decoder's reserved scratch. A previous member's
    // decode can otherwise destroy a later member's delta before it is checked.
    if(m.kind==ReplicaWireKind::Publication)for(std::size_t n=0;n<m.count;++n)
        if(overlap(m.patches[n].delta,scratch_))return fail(Error::InvalidArgument);
    if(m.kind==ReplicaWireKind::FullRepair)for(std::size_t n=0;n<m.count;++n)
        if(overlap(m.repairs[n].canonical,scratch_))return fail(Error::InvalidArgument);
    std::size_t bytes{};
    for(std::size_t n=0;n<m.count;++n){const auto key=m.kind==ReplicaWireKind::Publication?m.patches[n].key:m.repairs[n].key;auto value=record(key);if(!value)return fail(value.error());
        auto* exposed=exposure(key);if(!exposed||!exposed->ready_received||value->phase!=ReplicaPhase::Ready||value->active_image<0)return fail(Error::NotReady);
        auto catalog=registry_->find(value->schema->id());if(!catalog)return fail(catalog.error());const auto& c=(*catalog)->contract;
        if((c.publication==PublicationSemantics::PerObject&&(m.group_id||m.count!=1))||(c.publication==PublicationSemantics::ExplicitAtomicGroup&&m.group_id!=c.group_id)||m.count>c.maximum_group_objects)return fail(Error::IncompatibleSchema);
        if(!m.revision)return fail(Error::InvalidArgument);
        if(m.revision<value->last_sent_revision)return fail(sending?Error::StaleGeneration:Error::InvalidArgument);
        if(!sending)for(const auto& capture:captures_)if(capture.phase!=AuthorityCapturePhase::Empty&&(capture.kind==ReplicaWireKind::Publication||capture.kind==ReplicaWireKind::FullRepair))
            for(std::size_t member=0;member<capture.count;++member)if(capture.members[member]==key&&m.revision<capture.revision)return fail(Error::InvalidArgument);
        const auto size=value->schema->state_bytes();if(size>scratch_.size()-bytes)return fail(Error::CapacityExceeded);
        auto decoded=scratch_.subspan(bytes,size);
        if(m.kind==ReplicaWireKind::Publication){auto base=sender_->active_baseline(key,m.patches[n].baseline_token);if(!base)return fail(base.error());
            if(auto delta=apply_delta(*value->schema,*base,m.patches[n].delta,decoded);!delta)return delta;
        }else{if(m.repairs[n].canonical.size()!=size)return fail(Error::InvalidArgument);std::copy(m.repairs[n].canonical.begin(),m.repairs[n].canonical.end(),decoded.begin());}
        if(auto projection=projected(*value->schema,decoded,config_.peer==value->owner);!projection)return projection;
        bytes+=size;
        if(m.sequence<std::max(value->spawn_sequence,value->reset_sequence)||m.sequence>control_queued_)return fail(Error::StaleGeneration);
    }
    for(std::size_t n=0;n<m.count;++n){auto value=record(m.kind==ReplicaWireKind::Publication?m.patches[n].key:m.repairs[n].key);if(!value)return fail(value.error());auto catalog=registry_->find(value->schema->id());if(!catalog)return fail(catalog.error());if(bytes>(*catalog)->contract.maximum_group_bytes)return fail(Error::CapacityExceeded);}
    return {};
}
Status ReplicaAuthoritySession::validate(const ReplicaWireMessage& m,bool sending) noexcept {
    if(m.context!=context_)return fail(Error::StaleEpoch);
    switch(m.kind){
    case ReplicaWireKind::Bind:{auto value=record(m.binding.key);if(!value)return fail(value.error());if(m.binding.handle!=value->handle||m.binding.schema!=value->schema->id()||m.binding.owner!=value->owner||m.binding.ownership_revision!=value->ownership_revision||m.binding.lifecycle_sequence!=value->spawn_sequence)return fail(Error::ProtocolViolation);return {};}
    case ReplicaWireKind::BaselineOffer:{auto pending=sender_->pending_offer(m.baseline.key);if(!pending)return fail(pending.error());auto value=record(m.baseline.key);if(!value)return fail(value.error());
        if(pending->base_token!=m.baseline.base_token||pending->candidate_token!=m.baseline.candidate_token||pending->revision!=m.baseline.revision||!std::ranges::equal(pending->canonical,m.baseline.canonical))return fail(Error::ProtocolViolation);
        if(m.sequence<std::max(value->spawn_sequence,value->reset_sequence)||m.sequence>control_queued_)return fail(Error::StaleGeneration);return {};}
    case ReplicaWireKind::Leave:{auto value=record(m.key);if(!value)return fail(value.error());if(value->phase!=ReplicaPhase::Retiring||m.sequence!=value->leave_sequence)return fail(Error::ProtocolViolation);return {};}
    case ReplicaWireKind::OwnershipReset:{auto pending=sender_->pending_reset(m.new_key);if(!pending)return fail(pending.error());if(*pending!=ReplicaEncodingReset{m.key,m.new_key,m.owner,m.ownership_revision,m.sequence})return fail(Error::ProtocolViolation);return {};}
    case ReplicaWireKind::BaselineRetire:{auto value=record(m.retirement.key);if(!value)return fail(value.error());if(value->retiring_image<0||value->active_image<0||value->retire_sequence!=m.retirement.lifecycle_sequence||value->images[value->retiring_image].token!=m.retirement.old_token||value->images[value->active_image].token!=m.retirement.replacement_token)return fail(Error::ProtocolViolation);return {};}
    case ReplicaWireKind::Publication:case ReplicaWireKind::FullRepair:return validate_group(m,sending);
    default:return fail(Error::ProtocolViolation);
    }
}
Result<std::uint32_t> ReplicaAuthoritySession::queue(const ReplicaWireMessage& m) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pumping_)return fail(Error::Busy);pumping_=true;struct CallScope{bool& active;~CallScope(){active=false;}} scope{pumping_};
    if(auto bound=binding_valid();!bound){if(!transient(bound.error()))failed_=true;return fail(bound.error());}auto lane=replica_lane(m.kind);if(!lane)return fail(lane.error());
    const auto channel=*lane==ReplicaLane::Control?routes_.control:*lane==ReplicaLane::State?routes_.state:routes_.bulk;auto mode=session_->channel_mode(channel);if(!mode)return fail(mode.error());if(auto role=validate_replica_delivery(m.kind,*lane,*mode);!role)return fail(role.error());
    if(auto valid=validate(m,false);!valid)return fail(valid.error());auto* slot=vacant();if(!slot)return fail(Error::CapacityExceeded);if(order_==UINT64_MAX)return fail(Error::CounterExhausted);
    const auto sequence=m.kind==ReplicaWireKind::Bind?m.binding.lifecycle_sequence:m.kind==ReplicaWireKind::BaselineRetire?m.retirement.lifecycle_sequence:m.sequence;
    if(*lane==ReplicaLane::Control&&(control_queued_==UINT64_MAX||sequence!=control_queued_+1))return fail(Error::Busy);
    auto encoded=encode_replica_message(m,storage(*slot));if(!encoded)return fail(encoded.error());
    auto cap=session_->channel_message_bytes(channel);if(!cap)return fail(cap.error());if(*encoded>limits_.max_message_bytes||*encoded>*cap)return fail(Error::CapacityExceeded);
    // Single-frame unreliable state cannot accept a capture that its configured
    // carrier will subsequently reject indefinitely.
    if(*mode==DeliveryMode::Unreliable&&*encoded>limits_.fragment_payload_bytes)return fail(Error::CapacityExceeded);
    *slot={};slot->phase=AuthorityCapturePhase::Queued;slot->kind=m.kind;slot->lane=*lane;slot->bytes=static_cast<std::uint32_t>(*encoded);slot->order=++order_;slot->lifecycle=sequence;slot->count=m.count;slot->revision=m.revision;
    if(m.kind==ReplicaWireKind::BaselineOffer){slot->key=m.baseline.key;slot->token=m.baseline.candidate_token;slot->revision=m.baseline.revision;}
    if(m.kind==ReplicaWireKind::Bind)slot->key=m.binding.key;
    for(std::size_t n=0;n<m.count&&n<slot->members.size();++n)slot->members[n]=m.kind==ReplicaWireKind::Publication?m.patches[n].key:m.repairs[n].key;
    if(*lane==ReplicaLane::Control)control_queued_=sequence;return slot->bytes;
}
Status ReplicaAuthoritySession::flush(Tick tick,ReplicaAuthorityProgress& progress) noexcept {
    for(auto& slot:captures_)if(slot.phase==AuthorityCapturePhase::AwaitingCompletion){
        if(slot.ticket.mode==DeliveryMode::Unreliable){
            auto retired=session_->retire(slot.ticket);
            if(retired){slot={};++progress.carrier_retired;continue;}
            if(retired.error()!=Error::NotReady)return retired;
            auto result=session_->outcome(slot.ticket);
            if(!result&&result.error()==Error::NotReady){slot={};++progress.discarded;continue;}
            if(!result)return fail(result.error());continue;
        }
        auto result=session_->outcome(slot.ticket);if(!result)return fail(result.error());if(*result==Outcome::Applied){if(auto retired=session_->retire(slot.ticket);!retired)return retired;slot={};++progress.transport_applied;}
    }
    for(std::size_t n=0;n<captures_.size();++n){AuthorityCapture* oldest=nullptr;for(auto& slot:captures_)if(slot.phase==AuthorityCapturePhase::Queued&&(!oldest||slot.order<oldest->order))oldest=&slot;if(!oldest)break;
        auto payload=storage(*oldest).first(oldest->bytes);auto decoded=decode_replica_message(payload);if(!decoded)return fail(decoded.error());
        if(auto valid=validate(*decoded,true);!valid){if(oldest->lane!=ReplicaLane::Control&&(stale(valid.error())||valid.error()==Error::MissingBaseline||valid.error()==Error::NotReady)){*oldest={};++progress.discarded;continue;}return valid;}
        std::size_t token_slot=2;
        if(oldest->kind==ReplicaWireKind::BaselineOffer){auto* exposed=exposure(oldest->key);if(!exposed)return fail(Error::NotReady);for(std::size_t i=0;i<2;++i)if(!exposed->offered_tokens[i]||exposed->offered_tokens[i]==oldest->token){token_slot=i;break;}if(token_slot==2)return fail(Error::CapacityExceeded);}
        const auto channel=oldest->lane==ReplicaLane::Control?routes_.control:oldest->lane==ReplicaLane::State?routes_.state:routes_.bulk;auto sent=session_->send(payload,tick,channel);
        if(!sent){if(transient(sent.error())){++progress.backpressured;break;}return fail(sent.error());}
        oldest->ticket=*sent;oldest->phase=AuthorityCapturePhase::AwaitingCompletion;++progress.sent;
        if(oldest->lane==ReplicaLane::Control)control_sent_=oldest->lifecycle;
        if(oldest->kind==ReplicaWireKind::Bind){auto& exposure=exposures_[oldest->key.slot-1];exposure={};exposure.occupied=true;exposure.key=oldest->key;exposure.bind_sent=oldest->lifecycle;}
        if(oldest->kind==ReplicaWireKind::OwnershipReset){auto& exposure=exposures_[decoded->new_key.slot-1];const auto ready=exposure.ready_received;const auto bind=exposure.bind_sent;exposure={};exposure.occupied=true;exposure.ready_received=ready;exposure.bind_sent=bind;exposure.key=decoded->new_key;}
        if(oldest->kind==ReplicaWireKind::BaselineOffer){auto* exposed=exposure(oldest->key);exposed->offered_tokens[token_slot]=oldest->token;exposed->offered_revisions[token_slot]=oldest->revision;}
        if(oldest->kind==ReplicaWireKind::Publication||oldest->kind==ReplicaWireKind::FullRepair)for(std::size_t i=0;i<oldest->count;++i){auto applied=sender_->note_state_sent(oldest->members[i],oldest->revision);if(!applied)return applied;exposure(oldest->members[i])->state_sent=oldest->revision;}
    }return {};
}
Status ReplicaAuthoritySession::stage_retirement(const BaselineRetirement& retirement,AuthorityCapture& slot) noexcept {
    ReplicaWireMessage m;m.kind=ReplicaWireKind::BaselineRetire;m.context=context_;m.retirement=retirement;auto encoded=encode_replica_message(m,storage(slot));if(!encoded)return fail(encoded.error());
    slot={};slot.phase=AuthorityCapturePhase::Queued;slot.kind=m.kind;slot.lane=ReplicaLane::Control;slot.bytes=static_cast<std::uint32_t>(*encoded);slot.order=++order_;slot.lifecycle=retirement.lifecycle_sequence;control_queued_=retirement.lifecycle_sequence;return {};
}
Status ReplicaAuthoritySession::receipts(ReplicaAuthorityProgress& progress) noexcept {
    if(consumed_pending_){if(auto consumed=session_->applied(consumed_pending_,routes_.control);!consumed)return consumed;consumed_pending_=0;++progress.received;}
    auto delivery=session_->receive(routes_.control);if(!delivery){if(delivery.error()==Error::NotReady)return {};return fail(delivery.error());}
    auto decoded=decode_replica_message(delivery->payload);if(!decoded)return fail(decoded.error());auto& m=*decoded;
    auto release=[&]()->Status{consumed_pending_=delivery->message;auto released=session_->applied(consumed_pending_,routes_.control);if(released){consumed_pending_=0;++progress.received;}return released;};
    if(m.context!=context_){++progress.discarded;return release();}
    auto result=[&]()->Status{
        switch(m.kind){
        case ReplicaWireKind::LifecycleApplied:
            if(m.sequence>control_sent_)return fail(Error::ProtocolViolation);
            if(auto applied=sender_->control_applied(m.sequence);!applied)return applied;
            for(auto& exposed:exposures_)if(exposed.occupied){auto value=sender_->inspect(exposed.key);if(!value){exposed={};continue;}for(std::size_t i=0;i<2;++i){bool retained=false;for(const auto& image:value->images)retained=retained||image.token==exposed.offered_tokens[i];if(!retained){exposed.offered_tokens[i]=exposed.offered_revisions[i]=0;}}}return {};
        case ReplicaWireKind::SpawnApplied:{auto* exposed=exposure(m.spawned.key);if(!exposed||!exposed->bind_sent)return fail(Error::ProtocolViolation);auto value=record(m.spawned.key);if(!value)return fail(value.error());if(value->phase==ReplicaPhase::Retiring)return fail(Error::StaleGeneration);bool offered=false;for(std::size_t i=0;i<2;++i)offered=offered||(exposed->offered_tokens[i]&&exposed->offered_revisions[i]==m.spawned.revision);if(!offered)return fail(Error::ProtocolViolation);
            if(auto applied=sender_->spawn_applied(m.spawned.key,m.spawned.revision,m.spawned.ownership_revision);!applied)return applied;exposed->ready_received=true;exposed->state_sent=std::max(exposed->state_sent,m.spawned.revision);return {};}
        case ReplicaWireKind::BaselinePinned:{auto value=record(m.pinned.key);if(!value)return fail(value.error());if(value->phase==ReplicaPhase::Retiring)return fail(Error::StaleGeneration);auto* exposed=exposure(m.pinned.key);if(!exposed)return fail(Error::ProtocolViolation);bool offered=false;for(std::size_t i=0;i<2;++i)offered=offered||(exposed->offered_tokens[i]==m.pinned.token&&exposed->offered_revisions[i]==m.pinned.revision);if(!offered)return fail(Error::ProtocolViolation);
            const bool retiring=value->active_image>=0&&value->images[value->active_image].token!=m.pinned.token;AuthorityCapture* reserved=nullptr;
            if(retiring){if(control_queued_!=sender_->lifecycle_issued())return fail(Error::Busy);reserved=vacant();if(!reserved)return fail(Error::CapacityExceeded);if(order_==UINT64_MAX)return fail(Error::CounterExhausted);}
            auto pinned=sender_->baseline_pinned(m.pinned.key,m.pinned.token,m.pinned.revision);if(!pinned)return fail(pinned.error());if(retiring)return stage_retirement(*pinned,*reserved);return {};}
        case ReplicaWireKind::StateApplied:
            for(std::size_t n=0;n<m.count;++n){auto value=record(m.applied[n].key);if(!value)return fail(value.error());if(value->phase==ReplicaPhase::Retiring)return fail(Error::StaleGeneration);auto* exposed=exposure(m.applied[n].key);if(!exposed||!exposed->ready_received||value->phase!=ReplicaPhase::Ready||!m.applied[n].revision||m.applied[n].revision>exposed->state_sent||m.applied[n].revision>value->last_sent_revision)return fail(Error::ProtocolViolation);}
            for(std::size_t n=0;n<m.count;++n)if(auto applied=sender_->state_applied(m.applied[n].key,m.applied[n].revision);!applied)return applied;return {};
        case ReplicaWireKind::RepairRequest:
            for(std::size_t n=0;n<m.count;++n){auto value=record(m.requested[n].key);if(!value)return fail(value.error());if(value->phase==ReplicaPhase::Retiring)return fail(Error::StaleGeneration);auto* exposed=exposure(m.requested[n].key);if(!exposed||!exposed->ready_received||m.requested[n].last_applied_revision>exposed->state_sent)return fail(Error::ProtocolViolation);}
            if(m.count>repairs_.size()-repair_count_)return fail(Error::CapacityExceeded);
            for(std::size_t n=0;n<m.count;++n)repairs_[repair_count_++]=m.requested[n];return {};
        default:return fail(Error::ProtocolViolation);
        }
    }();
    // A late pin/readiness/state receipt for an actually retiring binding is
    // obsolete, not retryable pressure. Consume it so the same ordered route
    // can advance to the Leave acknowledgement behind it. Batch checks above
    // precede every semantic update; discarded batches cannot partially apply.
    if(!result){if(stale(result.error())){++progress.discarded;return release();}return result;}return release();
}
Result<ReplicaAuthorityProgress> ReplicaAuthoritySession::pump(Tick tick) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pumping_)return fail(Error::Busy);pumping_=true;struct Scope{bool& active;~Scope(){active=false;}} scope{pumping_};
    if(auto bound=binding_valid();!bound){if(!transient(bound.error()))failed_=true;return fail(bound.error());}ReplicaAuthorityProgress progress;
    auto handle=[&](Status status)->Status{if(status)return {};if(transient(status.error())){++progress.backpressured;return {};}failed_=true;return status;};
    if(auto result=handle(session_->pump(tick));!result)return fail(result.error());if(auto result=handle(flush(tick,progress));!result)return fail(result.error());
    // This bridge's three routes are reserved for authority replication. Client
    // semantic replies belong exclusively on ordered Control; other gameplay
    // messages must use separately negotiated channels/dispatchers.
    for(auto channel:{routes_.state,routes_.bulk}){auto unexpected=session_->receive(channel);if(unexpected){failed_=true;return fail(Error::ProtocolViolation);}if(unexpected.error()!=Error::NotReady)return fail(unexpected.error());}
    for(unsigned n=0;n<4;++n)if(auto result=handle(receipts(progress));!result)return fail(result.error());if(auto result=handle(flush(tick,progress));!result)return fail(result.error());return progress;
}
Result<std::size_t> ReplicaAuthoritySession::take_repairs(std::span<RepairRequest> out) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pumping_)return fail(Error::Busy);pumping_=true;struct Scope{bool& active;~Scope(){active=false;}} scope{pumping_};
    if(auto bound=binding_valid();!bound){if(!transient(bound.error()))failed_=true;return fail(bound.error());}const auto destination=std::as_bytes(out);
    for(auto source:{std::as_bytes(repairs_),std::as_bytes(captures_),std::as_bytes(exposures_),std::span<const std::byte>(arena_),std::span<const std::byte>(scratch_),std::as_bytes(std::span(this,1)),std::as_bytes(std::span(registry_,1))})if(overlap(destination,source))return fail(Error::InvalidArgument);
    auto session_alias=session_->storage_overlaps(destination),sender_alias=sender_->storage_overlaps(destination);if(!session_alias)return fail(session_alias.error());if(!sender_alias)return fail(sender_alias.error());if(*session_alias||*sender_alias)return fail(Error::InvalidArgument);
    std::array<Schema,SchemaRegistry::maximum_schemas> views{};if(auto materialized=registry_->materialize(views);!materialized)return fail(materialized.error());
    for(std::size_t n=0;n<registry_->size();++n){auto catalog=registry_->find(views[n].id());if(!catalog)return fail(catalog.error());if(overlap(destination,std::as_bytes(std::span(*catalog,1)))||overlap(destination,std::as_bytes(views[n].fields()))||overlap(destination,std::as_bytes(views[n].rpcs())))return fail(Error::InvalidArgument);}
    const auto count=std::min(out.size(),repair_count_);std::copy_n(repairs_.begin(),count,out.begin());std::move(repairs_.begin()+count,repairs_.begin()+repair_count_,repairs_.begin());repair_count_-=count;return count;
}
}
