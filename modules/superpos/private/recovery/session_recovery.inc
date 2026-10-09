// SPDX-License-Identifier: MIT
// Included by superpos_session.cpp after SuperposSession::Impl is complete,
// only when SUPERPOS_HAS_DURABLE_RECOVERY is defined.
superpos::Status SuperposRecoveryAccess::available(const SuperposSession &owner) noexcept {
    const auto *impl = owner.impl;
    if (Thread::get_caller_id() != owner.owner_thread) return superpos::fail(superpos::Error::PermissionDenied);
    if (owner.owner_retired || !impl || !impl->world) return superpos::fail(superpos::Error::NotReady);
    if (owner.closing || owner.managed_reload_paused || owner.simulation_in_flight || owner.callbacks_in_flight)
        return superpos::fail(superpos::Error::Busy);
    return {};
}

superpos::Result<superpos_egp::recovery::SessionTarget> SuperposRecoveryAccess::target(SuperposSession &owner) noexcept {
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    auto *impl = owner.impl;
    auto identity = impl->world->world().identity(); if (!identity) return superpos::fail(identity.error());
    auto profile = canonical_profile(impl->digest); if (!profile) return superpos::fail(profile.error());
    superpos_egp::recovery::SessionTarget result;
    result.schemas = impl->schemas.span();
    result.capacity = impl->capacity;
    result.state_stride = identity->config.state_stride;
    result.authority_epoch = identity->config.authority_epoch;
    result.authority_peer = identity->config.authority_peer;
    result.schemas_fingerprint = impl->world->fingerprint();
    result.simulation_fingerprint = *profile;
    result.digest = &impl->digest;
    return result;
}

superpos::Result<std::size_t> SuperposRecoveryAccess::capture(SuperposSession &owner, std::span<std::byte> output) noexcept {
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    auto *impl = owner.impl;
    return superpos_egp::recovery::encode_world_payload(impl->world->world(), {impl->slots, impl->capacity}, impl->tick, output);
}

superpos::Result<std::size_t> SuperposRecoveryAccess::live_count(SuperposSession &owner) noexcept {
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    return owner.impl->world->world().live_count();
}

superpos::Result<std::size_t> SuperposRecoveryAccess::snapshot_bytes(SuperposSession &owner) noexcept {
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    return owner.impl->world->world().capture_bytes();
}

superpos::Result<std::size_t> SuperposRecoveryAccess::snapshot(SuperposSession &owner, std::span<std::byte> output) noexcept {
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    return owner.impl->world->world().capture(output);
}

superpos::Result<superpos_egp::recovery::ApplyResult> SuperposRecoveryAccess::apply(SuperposSession &owner,
        std::span<const std::byte> snapshot, superpos::Epoch successor) noexcept {
    using superpos::Error;
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    auto *impl = owner.impl;
    if (owner.pending_native_retirement || (impl->network && impl->network->receiver)) return superpos::fail(Error::Busy);
    if (impl->network && impl->network->authority) return superpos::fail(Error::Busy);
    if (!successor) return superpos::fail(Error::StaleEpoch);
    // RegisteredWorld exposes no restore pass-through yet. Its World member
    // is not a const object, so restoring through it is well defined; the
    // frozen registry is unaffected and World::restore validates schemas,
    // capacity, the pristine state and the successor epoch itself.
    auto &world = const_cast<superpos::World &>(impl->world->world());
    auto restored = world.restore(snapshot, successor);
    if (!restored) return superpos::fail(restored.error());
    // Resume the Session tick at least at the newest restored entity tick.
    superpos::Tick newest = 0;
    for (uint32_t i = 0; i < impl->capacity; ++i) if (impl->slots[i].live && impl->slots[i].tick > newest) newest = impl->slots[i].tick;
    if (newest > impl->tick) impl->tick = newest;
    return superpos_egp::recovery::ApplyResult{uint32_t(*restored), true, impl->tick};
}

superpos::Status SuperposRecoveryAccess::attach_authority(SuperposSession &link, superpos::ReplicaConfig config,
        superpos::ReplicaWireContext context) noexcept {
    using superpos::Error;
    if (auto ready = available(link); !ready) return ready;
    auto *impl = link.impl;
    if (link.pending_native_retirement || !impl->network || !impl->network->session || !impl->registry) return superpos::fail(Error::NotReady);
#ifdef SUPERPOS_HAS_RTC
    if (impl->network->rtc) return superpos::fail(Error::Unsupported);
#endif
    if (impl->network->receiver || impl->network->authority) return superpos::fail(Error::Busy);
    if (!impl->network->session->ready()) return superpos::fail(Error::NotReady);
    auto candidate = superpos_egp::captured_create<superpos_egp::recovery::NativeAuthority>(impl->allocator,
        superpos::MemoryDomain::Replication, impl->allocator);
    if (!candidate) return superpos::fail(candidate.error());
    if (auto initialized = (*candidate)->initialize(*impl->network->session, *impl->registry, config, context, impl->capacity); !initialized)
        return initialized;
    impl->network->authority = std::move(*candidate);
    return {};
}

superpos_egp::recovery::NativeAuthority *SuperposRecoveryAccess::authority(SuperposSession &link) noexcept {
    if (!available(link) || !link.impl->network || !link.impl->network->authority) return nullptr;
    return &*link.impl->network->authority;
}

superpos::Status SuperposRecoveryAccess::detach_authority(SuperposSession &link) noexcept {
    if (auto ready = available(link); !ready) return ready;
    if (link.impl->network) link.impl->network->authority = {};
    return {};
}

superpos::Error SuperposRecoveryAccess::network_failure(SuperposSession &owner) noexcept {
    if (Thread::get_caller_id() != owner.owner_thread || !owner.impl || !owner.impl->network) return superpos::Error::None;
    return owner.impl->network->failure;
}
