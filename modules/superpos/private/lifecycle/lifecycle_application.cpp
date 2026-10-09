// SPDX-License-Identifier: MIT
#include "lifecycle_application.hpp"
#include <algorithm>
#include <atomic>
#include <limits>

namespace superpos_egp::lifecycle {
using namespace superpos;
namespace {
std::atomic<std::uint64_t> identities{1};
Result<std::uint64_t> next_identity() noexcept {
    auto value=identities.load(std::memory_order_relaxed);
    for(;;){if(value==UINT64_MAX)return fail(Error::CounterExhausted);
        if(identities.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;}
}
bool overlap(std::span<const std::byte> a,std::span<const std::byte> b) noexcept {
    if(a.empty()||b.empty())return false;
    auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
bool same_route(const ReplicaKey& a,const ReplicaKey& b) noexcept {
    return a.slot==b.slot&&a.incarnation==b.incarnation&&a.authority_epoch==b.authority_epoch&&
        a.connection_epoch==b.connection_epoch&&a.replica_epoch==b.replica_epoch;
}
}
Status Application::initialize(PeerReplicaReceiver& receiver,std::span<const FactoryBinding> factories,
        std::span<Record> records,Limits limits) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(receiver_)return fail(Error::Busy);
    if(!limits.active||limits.active>1000||!limits.transitions||limits.transitions>64||
       !limits.initial_generation||!limits.native_bytes||records.size()<limits.active+limits.transitions||records.size()>1064||
       factories.empty()||factories.size()>64)return fail(Error::InvalidArgument);
    auto instance=receiver.instance_identity();if(!instance)return fail(instance.error());
    auto config=receiver.configuration();if(!config)return fail(config.error());
    if(limits.active>config->maximum_active||limits.transitions>config->maximum_transitions)return fail(Error::CapacityExceeded);
    const auto self=std::as_bytes(std::span(this,1));
    if(overlap(std::as_bytes(factories),std::as_bytes(records))||overlap(std::as_bytes(factories),self)||
       overlap(std::as_bytes(records),self))return fail(Error::InvalidArgument);
    for(auto bytes:{std::as_bytes(factories),std::as_bytes(records)}){
        auto aliased=receiver.storage_overlaps(bytes);if(!aliased)return fail(aliased.error());
        if(*aliased)return fail(Error::InvalidArgument);
    }
    for(std::size_t i=0;i<factories.size();++i){const auto& f=factories[i];
        if(!f.schema||!f.resource||!f.factory||!f.instance_bytes||f.instance_bytes>limits.native_bytes||
           f.capability!=FactoryCapability::NativeTransactional)return fail(Error::Unsupported);
        for(std::size_t j=0;j<i;++j)if(factories[j].resource==f.resource)return fail(Error::InvalidArgument);
    }
    auto identity=next_identity();if(!identity)return fail(identity.error());
    for(auto& row:records){row={};row.generation=limits.initial_generation;}
    std::copy(factories.begin(),factories.end(),factory_storage_.begin());
    receiver_=&receiver;receiver_instance_=*instance;identity_=*identity;
    factories_=std::span(factory_storage_).first(factories.size());records_=records;limits_=limits;
    return {};
}
Status Application::available(bool inside_dispatch) const noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!receiver_)return fail(Error::NotReady);
    if(preparing_||staged_valid_||(!inside_dispatch&&dispatching_))return fail(Error::Busy);
    if(stopping_)return {};
    auto instance=receiver_->instance_identity();if(!instance)return fail(instance.error());
    if(*instance!=receiver_instance_)return fail(Error::StaleGeneration);
    return {};
}
Result<Application::Dispatch> Application::dispatch() noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(stopping_)return fail(Error::NotReady);
    dispatching_=true;return Dispatch(*this);
}
Application::Dispatch::~Dispatch(){if(owner_)owner_->end_dispatch();}
void Application::end_dispatch() noexcept {if(preparing_||staged_valid_)abort();dispatching_=false;}
Ticket Application::ticket(std::size_t index) const noexcept {return {identity_,records_[index].generation,std::uint32_t(index)};}
Result<std::size_t> Application::find(Ticket value) const noexcept {
    if(value.application!=identity_||!value.generation||value.slot>=records_.size())return fail(Error::StaleGeneration);
    const auto& row=records_[value.slot];
    if(row.generation!=value.generation||row.phase==ConstructionPhase::Empty||row.phase==ConstructionPhase::Exhausted)return fail(Error::StaleGeneration);
    return value.slot;
}
Result<std::size_t> Application::find(ReplicaKey key,bool reset) const noexcept {
    for(std::size_t i=0;i<records_.size();++i){const auto& row=records_[i];
        if(row.phase==ConstructionPhase::Empty||row.phase==ConstructionPhase::Exhausted)continue;
        if(row.key==key)return i;
        if(reset&&same_route(row.key,key)&&row.key.encoding_epoch!=UINT64_MAX&&row.key.encoding_epoch+1==key.encoding_epoch)return i;
    }
    return fail(Error::NotReady);
}
Result<Construction> Application::request(ReplicaKey key,std::uint64_t resource) noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    if(stopping_)return fail(Error::NotReady);
    auto state=receiver_->inspect_record(key);if(!state)return fail(state.error());
    if(state->phase!=ReceiverPhase::SpawnPending)return fail(Error::NotReady);
    auto prior=find(key);if(prior)return fail(Error::Busy);
    std::size_t selected=factories_.size();
    for(std::size_t i=0;i<factories_.size();++i)if(factories_[i].resource==resource){selected=i;break;}
    if(selected==factories_.size()||factories_[selected].schema!=state->catalog->schema.id())return fail(Error::PermissionDenied);
    if(factories_[selected].instance_bytes>limits_.native_bytes-native_bytes_)return fail(Error::OutOfMemory);
    if(transitions_>=limits_.transitions||active_+transitions_>=limits_.active+limits_.transitions)return fail(Error::CapacityExceeded);
    std::size_t index=records_.size();bool exhausted=false;
    for(std::size_t i=0;i<records_.size();++i){if(records_[i].phase==ConstructionPhase::Empty){index=i;break;}
        exhausted|=records_[i].phase==ConstructionPhase::Exhausted;}
    if(index==records_.size())return fail(exhausted?Error::CounterExhausted:Error::CapacityExceeded);
    auto& row=records_[index];row.phase=ConstructionPhase::Requested;row.key=key;row.handle=state->handle;row.factory=std::uint16_t(selected);++transitions_;
    native_bytes_+=factories_[selected].instance_bytes;
    return Construction{ticket(index),key,row.handle,state->catalog->schema.id(),resource};
}
Status Application::complete(Ticket value,Instance instance) noexcept {
    if(auto ready=available();!ready)return ready;
    if(stopping_)return fail(Error::StaleGeneration);
    auto index=find(value);if(!index)return fail(index.error());auto& row=records_[*index];
    if(row.phase!=ConstructionPhase::Requested)return fail(Error::StaleGeneration);
    if(!instance.value)return fail(Error::InvalidArgument);
    auto current=receiver_->inspect_record(row.key);if(!current)return fail(current.error());
    if(current->phase!=ReceiverPhase::SpawnPending||current->handle!=row.handle)return fail(Error::StaleGeneration);
    for(const auto& other:records_)if(other.instance==instance)return fail(Error::InvalidArgument);
    row.instance=instance;row.phase=ConstructionPhase::Constructed;return {};
}
Status Application::begin_shutdown() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!receiver_)return fail(Error::NotReady);
    if(dispatching_||preparing_||staged_valid_)return fail(Error::Busy);
    stopping_=true;return {};
}
Result<Cleanup> Application::take_cleanup() noexcept {
    if(auto ready=available();!ready)return fail(ready.error());
    for(std::size_t i=0;i<records_.size();++i){auto& row=records_[i];
        if(stopping_){
            if(row.phase==ConstructionPhase::Requested)row.phase=ConstructionPhase::Cancelled;
            else if(row.phase==ConstructionPhase::Constructed)row.phase=ConstructionPhase::Retiring;
            else if(row.phase==ConstructionPhase::Published&&transitions_<limits_.transitions){
                row.phase=ConstructionPhase::Retiring;++transitions_;--active_;
            }
        }
        if((row.phase==ConstructionPhase::Cancelled||row.phase==ConstructionPhase::Retiring)&&!row.cleanup_taken){
            row.cleanup_taken=true;return Cleanup{ticket(i),row.instance,factories_[row.factory].resource,row.phase==ConstructionPhase::Cancelled};}}
    return fail(Error::NotReady);
}
Status Application::acknowledge_cleanup(Ticket value) noexcept {
    if(auto ready=available();!ready)return ready;
    auto index=find(value);if(!index)return fail(index.error());auto& row=records_[*index];
    if(!row.cleanup_taken||(row.phase!=ConstructionPhase::Cancelled&&row.phase!=ConstructionPhase::Retiring))return fail(Error::InvalidArgument);
    const auto generation=row.generation;native_bytes_-=factories_[row.factory].instance_bytes;row={};--transitions_;
    if(generation==UINT64_MAX){row.phase=ConstructionPhase::Exhausted;row.generation=generation;}
    else row.generation=generation+1;
    return {};
}
Result<Counts> Application::counts() const noexcept {
    if(auto ready=available();!ready)return fail(ready.error());Counts result{active_,transitions_};
    for(const auto& row:records_){result.occupied+=row.phase!=ConstructionPhase::Empty&&row.phase!=ConstructionPhase::Exhausted;result.exhausted+=row.phase==ConstructionPhase::Exhausted;}
    result.native_bytes=native_bytes_;
    return result;
}
Status Application::stage(ReplicaChangeKind kind,std::span<const CanonicalReplica> states) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    if(!dispatching_||preparing_||staged_valid_)return fail(Error::Busy);
    if(states.empty()||states.size()>staged_.size())return fail(Error::CapacityExceeded);
    kind_=kind;staged_count_=0;std::size_t bytes=0,reservations=0,spawns=0;
    // Complete validation/reservation precedes every factory callback.
    for(const auto& state:states){
        if(!state.schema||state.canonical.size()>65536-bytes)return fail(Error::CapacityExceeded);bytes+=state.canonical.size();
        auto index=find(state.key,kind==ReplicaChangeKind::OwnershipReset);
        if(!index){if(kind!=ReplicaChangeKind::Destroy)return fail(index.error());staged_[staged_count_++]={};continue;}
        auto& row=records_[*index];
        if(row.phase==ConstructionPhase::Cancelled||row.phase==ConstructionPhase::Retiring)return fail(Error::StaleGeneration);
        if(row.handle!=state.handle||factories_[row.factory].schema!=state.schema->id())return fail(Error::IncompatibleSchema);
        for(std::size_t i=0;i<staged_count_;++i)if(staged_[i].index==*index)return fail(Error::InvalidArgument);
        if(kind==ReplicaChangeKind::Spawn){if(row.phase!=ConstructionPhase::Constructed)return fail(Error::Busy);++spawns;}
        else if(kind!=ReplicaChangeKind::Destroy&&row.phase!=ConstructionPhase::Published)return fail(Error::NotReady);
        if(kind==ReplicaChangeKind::Destroy&&row.phase==ConstructionPhase::Published)++reservations;
        staged_[staged_count_++]={*index,state.key,false,kind==ReplicaChangeKind::Destroy&&row.phase==ConstructionPhase::Published};
    }
    if(reservations>limits_.transitions-transitions_||spawns>limits_.active-active_)return fail(Error::CapacityExceeded);
    transitions_+=reservations;preparing_=true;
    for(std::size_t n=0;n<staged_count_;++n){auto& staged=staged_[n];if(staged.index==SIZE_MAX)continue;auto& row=records_[staged.index];
        if(!row.instance.value)continue;
        staged.attempted=true;auto prepared=factories_[row.factory].factory->prepare(row.instance,kind,states[n]);
        if(!prepared)return prepared;
    }
    preparing_=false;staged_valid_=true;return {};
}
void Application::abort() noexcept {
    if(owner_!=std::this_thread::get_id()||(!preparing_&&!staged_valid_))return;
    preparing_=true;
    for(std::size_t n=staged_count_;n>0;--n){auto& staged=staged_[n-1];if(staged.index==SIZE_MAX)continue;auto& row=records_[staged.index];
        if(staged.attempted)factories_[row.factory].factory->abort(row.instance,kind_);
        if(staged.reserved_transition)--transitions_;
    }
    staged_count_=0;preparing_=false;staged_valid_=false;
}
void Application::commit() noexcept {
    if(owner_!=std::this_thread::get_id()||!dispatching_||!staged_valid_)return;
    preparing_=true;
    for(std::size_t n=0;n<staged_count_;++n){auto& staged=staged_[n];if(staged.index==SIZE_MAX)continue;auto& row=records_[staged.index];
        if(staged.attempted)factories_[row.factory].factory->commit(row.instance,kind_);
        if(kind_==ReplicaChangeKind::Spawn){row.phase=ConstructionPhase::Published;--transitions_;++active_;}
        else if(kind_==ReplicaChangeKind::Destroy){if(row.phase==ConstructionPhase::Published)--active_;
            row.phase=row.instance.value?ConstructionPhase::Retiring:ConstructionPhase::Cancelled;}
        else if(kind_==ReplicaChangeKind::OwnershipReset)row.key=staged.key;
    }
    staged_count_=0;preparing_=false;staged_valid_=false;
}
}
