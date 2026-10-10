// SPDX-License-Identifier: MIT
#include "receiver_binding.hpp"
#include <algorithm>
namespace superpos_egp::lifecycle_engine {
using namespace superpos;
namespace {
bool wait(Error e) noexcept { return e==Error::Busy || e==Error::NotReady; }
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty() || b.empty())return false;
    auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
}
Status Routes::reserve(ReplicaSessionRoutes r,std::uint64_t generation) noexcept {
    if(!generation || r.control>=32 || r.state>=32 || r.bulk>=32 || r.control==r.state || r.control==r.bulk || r.state==r.bulk)return fail(Error::InvalidArgument);
    if(mask_)return fail(Error::Busy);
    mask_=(std::uint32_t{1}<<r.control)|(std::uint32_t{1}<<r.state)|(std::uint32_t{1}<<r.bulk);generation_=generation;return {};
}
Status Routes::release(std::uint64_t generation) noexcept {
    if(!mask_ || generation_!=generation)return fail(Error::StaleGeneration);
    mask_=0;generation_=0;return {};
}
NativeFactory* ReceiverBinding::factory(std::uint64_t resource) noexcept {
    for(std::size_t i=0;i<factory_count_;++i)if(factories_[i].binding.resource==resource)return factories_[i].native;
    return nullptr;
}
Job* ReceiverBinding::job(lifecycle::Ticket ticket) noexcept { for(auto& row:jobs_)if(row.occupied && row.work.ticket==ticket)return &row;return nullptr; }
Status ReceiverBinding::initialize(Session& session,PeerReplicaReceiver& receiver,Routes& routes,std::uint64_t generation,
    std::span<const Factory> factories,std::span<lifecycle::Record> records,std::span<Job> jobs,
    std::span<ReplicaReplySlot> replies,std::span<PendingReplicaSpawn> pending,std::span<std::byte> wire,
    ReplicaWireContext context,ReplicaSessionRoutes channels,lifecycle::Limits limits) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(poisoned_)return fail(Error::ChannelFailed);
    if(session_ || !generation || factories.empty() || factories.size()>factories_.size() || jobs.size()!=records.size() ||
        replies.empty() || replies.size()>ReplicaReceiverSession::maximum_replies || pending.empty() ||
        pending.size()>ReplicaReceiverSession::maximum_pending_spawns || wire.size()<ReplicaReceiverSession::minimum_wire_scratch)return fail(Error::InvalidArgument);
    const std::array ranges{std::as_bytes(records),std::as_bytes(replies),std::as_bytes(pending),std::span<const std::byte>(wire),
        std::as_bytes(factories),std::as_bytes(jobs),std::as_bytes(std::span(this,1)),std::as_bytes(std::span(&routes,1))};
    for(std::size_t i=0;i<ranges.size();++i)for(std::size_t j=0;j<i;++j)if(overlap(ranges[i],ranges[j]))return fail(Error::InvalidArgument);
    for(auto range:ranges){auto a=session.storage_overlaps(range),b=receiver.storage_overlaps(range);
        if(!a)return fail(a.error());if(!b)return fail(b.error());if(*a || *b)return fail(Error::InvalidArgument);}
    std::array<lifecycle::FactoryBinding,64> bindings{};
    for(std::size_t i=0;i<factories.size();++i) {
        if(!factories[i].native || factories[i].binding.factory!=factories[i].native)return fail(Error::InvalidArgument);
        bindings[i]=factories[i].binding;
        // A schema chooses one local resource; incoming wire data never chooses
        // factory pointers, paths, scripts or resource names.
        for(std::size_t j=0;j<i;++j)if(bindings[i].schema==bindings[j].schema)return fail(Error::InvalidArgument);
    }
    auto identity=session.identity();if(!identity)return fail(identity.error());
    if(auto reserved=routes.reserve(channels,generation);!reserved)return reserved;
    // Admission can fail for channel modes, identities or capacity. Probe with
    // private queues before irrevocably initializing Application or changing
    // caller rows. create performs no application callbacks or wire writes.
    std::array<ReplicaReplySlot,ReplicaReceiverSession::maximum_replies> probe_replies{};
    std::array<PendingReplicaSpawn,ReplicaReceiverSession::maximum_pending_spawns> probe_pending{};
    std::array<std::byte,ReplicaReceiverSession::minimum_wire_scratch> probe_wire{};
    auto probe=ReplicaReceiverSession::create(session,receiver,application_,context,channels,
        std::span(probe_replies).first(replies.size()),std::span(probe_pending).first(pending.size()),probe_wire);
    if(!probe){(void)routes.release(generation);return fail(probe.error());}
    auto initialized=application_.initialize(receiver,std::span(bindings).first(factories.size()),records,limits);
    if(!initialized){(void)routes.release(generation);return initialized;}
    auto bridge=ReplicaReceiverSession::create(session,receiver,application_,context,channels,replies,pending,wire);
    if(!bridge){poisoned_=true;(void)routes.release(generation);return fail(bridge.error());}
    for(auto& row:jobs)row={};
    std::copy(factories.begin(),factories.end(),factories_.begin());factory_count_=factories.size();
    session_=&session;receiver_=&receiver;routes_=&routes;generation_=generation;identity_=*identity;
    jobs_=jobs;pending_=pending;bridge_.emplace(std::move(*bridge));return {};
}
Status ReceiverBinding::cleanup() noexcept {
    for(std::size_t n=0;n<jobs_.size();++n) {
        if(!cleanup_){auto next=application_.take_cleanup();if(!next){if(next.error()==Error::NotReady)return {};return fail(next.error());}cleanup_=*next;disposed_=false;}
        auto* native=factory(cleanup_->resource);if(!native)return fail(Error::ProtocolViolation);
        auto* row=job(cleanup_->ticket);if(!row)return fail(Error::ProtocolViolation);
        if(!disposed_) {
            if(cleanup_->cancel_construction){auto result=native->cancel(row->work);if(!result)return result;}
            else native->destroy(cleanup_->instance);
            disposed_=true;
        }
        auto ack=application_.acknowledge_cleanup(cleanup_->ticket);if(!ack)return ack;
        *row={};cleanup_.reset();disposed_=false;
    }return {};
}
Status ReceiverBinding::shutdown() noexcept {
    if(!shutdown_started_){auto result=application_.begin_shutdown();if(!result)return result;shutdown_started_=true;}
    auto result=cleanup();if(!result)return result;
    auto counts=application_.counts();if(!counts)return fail(counts.error());
    if(counts->occupied)return fail(Error::Busy);
    bridge_.reset();if(auto released=routes_->release(generation_);!released)return released;drained_=true;return {};
}
Status ReceiverBinding::stop() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!session_)return fail(Error::NotReady);
    stopping_=true;if(pumping_)return fail(Error::Busy);
    if(drained_)return {};
    pumping_=true;struct Guard{bool& value;~Guard(){value=false;}} guard{pumping_};return shutdown();
}
Result<ReplicaSessionProgress> ReceiverBinding::pump(Session& session,std::uint64_t generation,Tick tick) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(pumping_)return fail(Error::Busy);
    if(!session_ || drained_)return fail(Error::NotReady);
    pumping_=true;struct Guard{bool& value;~Guard(){value=false;}} guard{pumping_};
    if(stopping_){auto result=shutdown();if(!result)return fail(result.error());return ReplicaSessionProgress{};}
    auto identity=session.identity();
    if(&session!=session_ || generation!=generation_ || !identity || *identity!=identity_){stopping_=true;return fail(Error::StaleGeneration);}
    Result<ReplicaSessionProgress> result=fail(Error::Busy);
    {auto dispatch=application_.dispatch();if(!dispatch)return fail(dispatch.error());result=bridge_->pump(tick);}
    if(!result){stopping_=true;return result;}
    auto cleaned=cleanup();if(!cleaned){if(wait(cleaned.error()))return result;stopping_=true;return fail(cleaned.error());}
    if(stopping_)return result;
    for(const auto& pending:pending_)if(pending.occupied) {
        bool found=false;for(const auto& row:jobs_)if(row.occupied && row.work.key==pending.key){found=true;break;}if(found)continue;
        auto state=receiver_->inspect_record(pending.key);if(!state){if(state.error()==Error::StaleGeneration)continue;stopping_=true;return fail(state.error());}
        const Factory* selected=nullptr;for(std::size_t i=0;i<factory_count_;++i)if(factories_[i].binding.schema==state->catalog->schema.id()){selected=&factories_[i];break;}
        if(!selected){stopping_=true;return fail(Error::Unsupported);}
        Job* vacant=nullptr;for(auto& row:jobs_)if(!row.occupied){vacant=&row;break;}if(!vacant)return fail(Error::CapacityExceeded);
        auto request=application_.request(pending.key,selected->binding.resource);if(!request){if(wait(request.error())||request.error()==Error::CapacityExceeded)continue;stopping_=true;return fail(request.error());}
        *vacant={true,false,*request};
        auto started=selected->native->begin(vacant->work);if(!started){stopping_=true;return fail(started.error());}
        if(stopping_)return result;
    }
    for(auto& row:jobs_)if(row.occupied && !row.complete) {
        auto* native=factory(row.work.resource);auto instance=native->poll(row.work);
        if(!instance){if(wait(instance.error()))continue;stopping_=true;return fail(instance.error());}
        if(stopping_){native->destroy(*instance);return result;}
        auto complete=application_.complete(row.work.ticket,*instance);
        if(!complete){native->destroy(*instance);stopping_=true;return fail(complete.error());}
        row.complete=true;if(stopping_)break;
    }
    return result;
}
}
