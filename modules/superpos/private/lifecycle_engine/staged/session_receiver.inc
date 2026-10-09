// SPDX-License-Identifier: MIT
// Included after SuperposSession::Impl is complete. All factory callbacks run
// from explicit close/pump or retirement draining, never a Session destructor.
namespace {
superpos_egp::lifecycle_engine::RetirementQueue receiver_retirements{1024};
std::array<void*,1024> receiver_owners{};
}
superpos::Status SuperposNativeReceiverAccess::progress(void* context,bool shutdown) noexcept {
    auto* owned=static_cast<SuperposSession::Impl*>(context);
    return owned->network->receiver->progress_retirement(shutdown);
}
void SuperposNativeReceiverAccess::dispose(void* context) noexcept {
    auto* owned=static_cast<SuperposSession::Impl*>(context);
    const auto recipient=owned->native_recipient;
    receiver_owners[owned->native_slot]=nullptr;
    owned->network->receiver.reset();
    auto* session=Object::cast_to<SuperposSession>(ObjectDB::get_instance(recipient));
    // Ref acquisition increments only a live reference count; it cannot resurrect
    // a zero-ref owner. Its callbacks may reenter, so validate custody afterwards.
    Ref<SuperposSession> keep_alive(session);
    if(keep_alive.is_valid() && keep_alive->pending_native_retirement==owned){
        session=keep_alive.ptr();
        session->pending_native_retirement=nullptr;session->impl=owned;session->closing=false;
        if(session->owner_retired)session->_finish_retirement();else (void)session->close_checked();
    }else SuperposSession::Impl::destroy(owned);
}
void SuperposNativeReceiverAccess::drain() {
    auto result=receiver_retirements.drain();
    if(!result && result.error()!=superpos::Error::Busy && result.error()!=superpos::Error::NotReady)
        ERR_PRINT_ONCE("Superpos native retirement failed; custody retained until qualified shutdown.");
}
Error SuperposNativeReceiverAccess::attach(SuperposSession& session,
        std::span<const superpos_egp::lifecycle_engine::Registration> factories,superpos::ReceiverConfig config,
        superpos::ReplicaSessionRoutes routes,superpos_egp::lifecycle::Limits limits) noexcept {
    if(!Thread::is_main_thread() || Thread::get_caller_id()!=session.owner_thread)return ERR_BUSY;
    if(session.owner_retired || !session.impl || !session.impl->world || !session.impl->registry || !session.impl->network)return ERR_UNCONFIGURED;
    if(session.closing || session.managed_reload_paused || session.callbacks_in_flight || session.simulation_in_flight)return ERR_BUSY;
    Ref<SuperposSession> keep_alive(&session);
    session.closing=true;
    struct ClosingGuard { SuperposSession& owner;~ClosingGuard(){if(!owner.pending_native_retirement)owner.closing=false;} } closing{session};
    ++session.callbacks_in_flight;
    struct CallbackGuard { SuperposSession& owner;~CallbackGuard(){owner._end_callback();} } callbacks{session};
    auto* owned=session.impl;if(owned->network->receiver)return ERR_ALREADY_IN_USE;
    auto access=owned->network->access();if(!access)return translate(access.error());
    auto authority=owned->world->world().authority_epoch();if(!authority || *authority!=config.authority_epoch)return ERR_INVALID_PARAMETER;
    auto* tree=SceneTree::get_singleton();if(!tree)return ERR_UNCONFIGURED;
    auto callback=callable_mp_static(&SuperposNativeReceiverAccess::drain);
    if(!tree->is_connected(SNAME("process_frame"),callback) && tree->connect(SNAME("process_frame"),callback)!=OK)return ERR_OUT_OF_MEMORY;
    std::size_t slot=0;while(slot<receiver_owners.size() && receiver_owners[slot])++slot;
    if(slot==receiver_owners.size())return ERR_OUT_OF_MEMORY;
    auto reserved=receiver_retirements.reserve(owned->native_retirement);if(!reserved)return translate(reserved.error());
    auto candidate=superpos_egp::captured_create<superpos_egp::lifecycle_engine::NativeReceiver>(owned->allocator,superpos::MemoryDomain::Replication,owned->allocator);
    if(!candidate){(void)receiver_retirements.release(owned->native_retirement);return translate(candidate.error());}
    auto admitted=(*candidate)->initialize(*access->operator->(),*owned->registry,owned->binding_generation,factories,config,routes,limits);
    if(!admitted){(void)receiver_retirements.release(owned->native_retirement);return translate(admitted.error());}
    if(session.owner_retired || session.impl!=owned || session.managed_reload_paused){(void)receiver_retirements.release(owned->native_retirement);return ERR_UNCONFIGURED;}
    owned->native_slot=slot;owned->native_recipient=session.get_instance_id();receiver_owners[slot]=owned;
    owned->network->receiver=std::move(*candidate);return OK;
}
void SuperposNativeReceiverAccess::orphan(SuperposSession& session) noexcept {
    auto* owned=session.impl;
    if(!owned || !owned->network || !owned->network->receiver)return;
    // The whole allocator/registry/transport graph leaves Session ownership.
    // The zero-ref Session is not retained or resurrected.
    session.impl=nullptr;session.pending_native_retirement=owned;session.closing=true;
    const auto transferred=receiver_retirements.transfer(owned->native_retirement,owned,progress,dispose);
    CRASH_COND_MSG(!transferred,"Reserved native retirement custody was corrupted.");
}
Error SuperposNativeReceiverAccess::close(SuperposSession& session) noexcept {
    if(session.pending_native_retirement)return ERR_BUSY;
    auto* owned=session.impl;if(!owned || !owned->network || !owned->network->receiver)return OK;
    if(owned->binding_generation==UINT64_MAX)return ERR_PARAMETER_RANGE_ERROR;
    orphan(session);return ERR_BUSY;
}
Error SuperposNativeReceiverAccess::shutdown() noexcept {
    // Transfer every active owner before SceneTree and transport teardown.
    for(auto* pointer:receiver_owners)if(pointer){
        auto* owned=static_cast<SuperposSession::Impl*>(pointer);if(owned->native_retirement.is_queued())continue;
        auto* session=Object::cast_to<SuperposSession>(ObjectDB::get_instance(owned->native_recipient));
        Ref<SuperposSession> keep_alive(session);
        if(keep_alive.is_valid() && keep_alive->impl==owned)orphan(*keep_alive.ptr());
    }
    const auto result=receiver_retirements.drain(true);
    return result?OK:translate(result.error());
}
#ifdef SUPERPOS_LIFECYCLE_FIXTURE
void SuperposNativeReceiverAccess::retire_fixture_owner(SuperposSession& owner) noexcept { owner._retire_world_owner(0); }
Error SuperposNativeReceiverAccess::bind_fixture_transport(SuperposSession& owner,superpos::TransportProvider& transport,superpos::SessionConfig config) noexcept {
    if(!Thread::is_main_thread() || owner.owner_retired || owner.closing || !owner.impl || !owner.impl->world)return ERR_UNCONFIGURED;
    auto* state=owner.impl;if(state->network)return ERR_ALREADY_IN_USE;
    auto created=superpos_egp::captured_create<EngineNetwork>(state->allocator,superpos::MemoryDomain::Session);
    if(!created)return translate(created.error());
    auto profile=canonical_profile(state->digest);if(!profile)return translate(profile.error());
    config.capabilities.schemas=state->world->fingerprint();config.admission.schemas=config.capabilities.schemas;
    config.capabilities.simulation=*profile;config.admission.simulation=*profile;
    auto session=superpos::Session::create(state->allocator,transport,config);if(!session)return translate(session.error());
    (*created)->session.emplace(std::move(*session));(*created)->simulation=*profile;
    state->network=std::move(*created);return OK;
}
#endif
