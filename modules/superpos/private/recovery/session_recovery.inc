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

superpos::Result<superpos_egp::recovery::ApplyResult> SuperposRecoveryAccess::apply(SuperposSession &owner,
        std::span<const std::byte> payload, superpos::Epoch successor, std::span<superpos_egp::recovery::HandleMapping> mapping) noexcept {
    using superpos::Error;
    if (auto ready = available(owner); !ready) return superpos::fail(ready.error());
    auto *impl = owner.impl;
    if (owner.pending_native_retirement || (impl->network && impl->network->receiver)) return superpos::fail(Error::Busy);
    auto &world = impl->world->world();
    auto epoch = world.authority_epoch(); if (!epoch) return superpos::fail(epoch.error());
    if (!successor || *epoch != successor) return superpos::fail(Error::StaleEpoch);
    auto live = world.live_count(); if (!live) return superpos::fail(live.error());
    if (*live) return superpos::fail(Error::Busy);
    auto header = superpos_egp::recovery::validate_world_payload(payload, impl->schemas.span(), impl->capacity);
    if (!header) return superpos::fail(header.error());
    if (mapping.size() < header->count) return superpos::fail(Error::CapacityExceeded);
    // Callback-free: World spawn performs no scene or user calls.
    std::uint32_t spawned = 0;
    bool preserved = true;
    auto applied = superpos_egp::recovery::for_each_entity(payload, [&](const superpos_egp::recovery::WorldEntity &entity) -> superpos::Status {
        auto handle = impl->world->spawn(entity.schema, entity.owner, entity.state, entity.tick);
        if (!handle) return superpos::fail(handle.error());
        const auto original = superpos::ObjectHandle::from_parts(entity.slot, entity.generation).value;
        mapping[spawned++] = {original, handle->value};
        preserved = preserved && original == handle->value;
        return {};
    });
    if (!applied) {
        for (std::uint32_t i = 0; i < spawned; ++i) (void)impl->world->destroy(superpos::ObjectHandle{mapping[i].restored}, successor);
        return superpos::fail(applied.error());
    }
    if (header->tick > impl->tick) impl->tick = header->tick;
    return superpos_egp::recovery::ApplyResult{spawned, preserved, impl->tick};
}
