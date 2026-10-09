// SPDX-License-Identifier: MIT
#include "native_receiver.hpp"

namespace superpos_egp::lifecycle_engine {
using namespace superpos;
NativeReceiver::NativeReceiver(Allocator& allocator) noexcept:
    receiver_rows_(allocator,MemoryDomain::Replication),images_(allocator,MemoryDomain::Replication),
    scratch_(allocator,MemoryDomain::Replication),wire_(allocator,MemoryDomain::Session),
    lifecycle_rows_(allocator,MemoryDomain::Replication),jobs_(allocator,MemoryDomain::Replication),
    replies_(allocator,MemoryDomain::Session),pending_(allocator,MemoryDomain::Session) {}
Status NativeReceiver::initialize(Session& session,SchemaRegistry& registry,std::uint64_t generation,
    std::span<const Registration> registrations,ReceiverConfig config,ReplicaSessionRoutes channels,lifecycle::Limits limits) noexcept {
    if(initialized_ || !generation || registrations.empty() || registrations.size()>factory_pins_.size())return fail(superpos::Error::InvalidArgument);
    if(!config.state_stride || config.state_stride>Schema::maximum_state_bytes || !config.maximum_active ||
       config.maximum_active>1000 || !config.maximum_transitions || config.maximum_transitions>64 ||
       limits.active!=config.maximum_active || limits.transitions!=config.maximum_transitions)return fail(superpos::Error::InvalidArgument);
    for(std::size_t i=0;i<registrations.size();++i){const auto& r=registrations[i];
        if(r.factory.is_null() || r.factory->get_script_instance() || !r.schema || !r.resource || !r.native_bytes)return fail(superpos::Error::PermissionDenied);
        for(std::size_t j=0;j<i;++j)if(registrations[j].schema==r.schema || registrations[j].resource==r.resource)return fail(superpos::Error::InvalidArgument);
    }
    const auto count=config.maximum_active+config.maximum_transitions;
    for(auto result:{receiver_rows_.initialize(count),images_.initialize(count*config.state_stride*3),
            scratch_.initialize(PeerReplicaReceiver::maximum_group_bytes),wire_.initialize(ReplicaReceiverSession::minimum_wire_scratch),
            lifecycle_rows_.initialize(count),jobs_.initialize(count),replies_.initialize(ReplicaReceiverSession::maximum_replies),
            pending_.initialize(config.maximum_transitions)})if(!result)return result;
    auto frozen=registry.freeze(scratch_.span());if(!frozen)return fail(frozen.error());
    auto receiver=PeerReplicaReceiver::create(config,std::move(*frozen),receiver_rows_.span(),images_.span(),scratch_.span());
    if(!receiver)return fail(receiver.error());receiver_.emplace(std::move(*receiver));
    for(std::size_t i=0;i<registrations.size();++i){const auto& r=registrations[i];factory_pins_[i]=r.factory;
        factories_[i]={{r.schema,r.resource,lifecycle::FactoryCapability::NativeTransactional,r.factory.ptr(),r.native_bytes},r.factory.ptr()};}
    auto attached=binding_.initialize(session,*receiver_,routes_,generation,std::span(factories_).first(registrations.size()),
        lifecycle_rows_.span(),jobs_.span(),replies_.span(),pending_.span(),wire_.span(),
        {config.authority_epoch,config.connection_epoch,config.replica_epoch},channels,limits);
    if(!attached)return attached;initialized_=true;return {};
}
Result<ReplicaSessionProgress> NativeReceiver::pump(Session& session,std::uint64_t generation,Tick tick) noexcept {
    return binding_.pump(session,generation,tick);
}
Status NativeReceiver::progress_retirement(bool shutdown) noexcept {
    if(shutdown)for(auto& factory:factory_pins_)if(factory.is_valid()){
        auto settled=factory->quiesce_native();if(!settled)return settled;
    }
    return binding_.stop();
}
}
