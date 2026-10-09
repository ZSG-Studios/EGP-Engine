// SPDX-License-Identifier: MIT
#include "restore_publisher.hpp"
#include "superpos_session.h"
#include "core/os/thread.h"
#include <algorithm>

namespace superpos_egp::recovery {
using superpos::Error;
using superpos::fail;
namespace {
bool transient(Error error) noexcept { return error == Error::Busy || error == Error::NotReady; }
const superpos::Schema *schema_for(const SessionTarget &target, superpos::SchemaId id) noexcept {
    for (const auto &schema : target.schemas) if (schema.id() == id) return &schema;
    return nullptr;
}
}

superpos::Result<std::uint32_t> RestoreFanout::add_link(SuperposSession &link, const LinkRoute &route) noexcept {
    if (!Thread::is_main_thread()) return fail(Error::PermissionDenied);
    if (count_ == maximum_links) return fail(Error::CapacityExceeded);
    if (!route.context.authority || !route.context.connection || !route.context.replica || !route.client ||
            !route.maximum_active || !route.maximum_transitions) return fail(Error::InvalidArgument);
    links_[count_] = Link{};
    links_[count_].session = link.get_instance_id();
    links_[count_].route = route;
    return count_++;
}

void RestoreFanout::close_egress() noexcept {
    for (std::uint32_t i = 0; i < count_; ++i) {
        Ref<SuperposSession> pin(Object::cast_to<SuperposSession>(ObjectDB::get_instance(links_[i].session)));
        if (pin.is_null()) continue;
        if (auto *owner = SuperposRecoveryAccess::authority(*pin.ptr())) owner->set_egress(false);
    }
}

superpos::Status RestoreFanout::advance(Link &link, SuperposSession &session, std::span<const std::byte> payload, std::uint64_t now) noexcept {
    auto target = SuperposRecoveryAccess::target(session); if (!target) return fail(target.error());
    if (!link.attached) {
        superpos::ReplicaConfig config;
        config.authority_epoch = link.route.context.authority; config.connection_epoch = link.route.context.connection;
        config.replica_epoch = link.route.context.replica; config.peer = link.route.client;
        config.maximum_active = std::min<std::size_t>(link.route.maximum_active, target->capacity);
        config.maximum_transitions = std::min<std::size_t>(link.route.maximum_transitions, target->capacity);
        config.state_stride = target->state_stride;
        auto attached = SuperposRecoveryAccess::attach_authority(session, config, link.route.context);
        // A late link waits until its Session is admitted and network-ready.
        if (!attached) return transient(attached.error()) ? superpos::Status{} : attached;
        link.attached = true;
    }
    auto *owner = SuperposRecoveryAccess::authority(session); if (!owner) return fail(Error::NotReady);
    owner->set_egress(true);
    auto &sender = owner->sender();
    auto &bridge = owner->bridge();
    if (!link.started) {
        // Spawn every restored entity into this link's sender store once.
        std::uint32_t n = 0;
        auto begun = for_each_entity(payload, [&](const WorldEntity &entity) -> superpos::Status {
            if (n == maximum_entities) return fail(Error::CapacityExceeded);
            const auto *schema = schema_for(*target, entity.schema); if (!schema) return fail(Error::IncompatibleSchema);
            const superpos::EntityView view{superpos::ObjectHandle::from_parts(entity.slot, entity.generation), schema, entity.owner,
                entity.ownership_revision ? entity.ownership_revision : 1, entity.revision, entity.tick, entity.state};
            auto binding = sender.begin_spawn(view); if (!binding) return fail(binding.error());
            if (auto offered = sender.offer_baseline(binding->key, entity.state, entity.revision, now); !offered) return fail(offered.error());
            link.bindings[n++] = *binding;
            return {};
        });
        if (!begun) return begun;
        link.entities = n; link.started = true;
    }
    // Bind then BaselineOffer per entity, as capture slots free up.
    while (link.cursor < 2 * link.entities) {
        const auto &binding = link.bindings[link.cursor / 2];
        auto record = sender.inspect(binding.key); if (!record) return fail(record.error());
        superpos::ReplicaWireMessage message;
        message.context = link.route.context;
        if (link.cursor % 2 == 0) {
            message.kind = superpos::ReplicaWireKind::Bind;
            message.binding = {binding.key, binding.handle, record->schema->id(), record->owner, record->ownership_revision, binding.lifecycle_sequence};
        } else {
            auto pending = sender.pending_offer(binding.key); if (!pending) return fail(pending.error());
            message.kind = superpos::ReplicaWireKind::BaselineOffer; message.baseline = *pending; message.sequence = binding.lifecycle_sequence;
        }
        auto queued = bridge.queue(message);
        if (!queued) { if (transient(queued.error()) || queued.error() == Error::CapacityExceeded) break; return fail(queued.error()); }
        ++link.cursor;
    }
    // Repairs: answer with the restored canonical state of that entity.
    std::array<superpos::RepairRequest, 8> requests{};
    auto taken = bridge.take_repairs(requests); if (!taken) return fail(taken.error());
    for (std::size_t r = 0; r < *taken; ++r) {
        bool answered = false;
        for (std::uint32_t e = 0; e < link.entities && !answered; ++e) {
            if (link.bindings[e].key != requests[r].key) continue;
            std::uint32_t index = 0;
            (void)for_each_entity(payload, [&](const WorldEntity &entity) -> superpos::Status {
                if (index++ != e || answered) return {};
                auto record = sender.inspect(requests[r].key); if (!record) return {};
                superpos::ReplicaWireMessage repair;
                repair.kind = superpos::ReplicaWireKind::FullRepair; repair.context = link.route.context; repair.count = 1;
                repair.revision = entity.revision; repair.sequence = std::max(record->spawn_sequence, record->reset_sequence);
                repair.repairs[0] = {requests[r].key, entity.revision, entity.state};
                answered = bool(bridge.queue(repair));
                return {};
            });
        }
        if (answered) ++link.repairs_answered; else ++link.repairs_deferred;
    }
    std::uint32_t ready = 0;
    for (std::uint32_t e = 0; e < link.entities; ++e) {
        auto record = sender.inspect(link.bindings[e].key);
        if (record && record->phase == superpos::ReplicaPhase::Ready) ++ready;
    }
    link.ready = ready;
    return {};
}

superpos::Result<FanoutProgress> RestoreFanout::step(SuperposSession &world, superpos::AuthorityLease &lease,
        const superpos::AuthorityGrant &grant, std::uint64_t now) noexcept {
    if (!Thread::is_main_thread()) return fail(Error::PermissionDenied);
    // Point-in-time authority for this frame's egress, never cached.
    auto authorized = lease.authorize(grant.owner, grant.kind, grant.epoch, grant.membership_generation);
    if (!authorized) { close_egress(); return fail(authorized.error()); }
    auto target = SuperposRecoveryAccess::target(world); if (!target) { close_egress(); return fail(target.error()); }
    if (target->authority_epoch != grant.epoch) { close_egress(); return fail(Error::StaleEpoch); }
    std::array<std::byte, 64 * 1024> storage{};
    const std::size_t limit = std::min(storage.size(), maximum_world_payload(target->capacity, target->state_stride));
    auto captured = SuperposRecoveryAccess::capture(world, std::span(storage).first(limit)); if (!captured) return fail(captured.error());
    const auto payload = std::span<const std::byte>(storage.data(), *captured);
    if (auto checked = validate_world_payload(payload, target->schemas, target->capacity); !checked) return fail(checked.error());
    FanoutProgress progress; progress.links = count_;
    for (std::uint32_t i = 0; i < count_; ++i) {
        auto &link = links_[i];
        if (link.route.context.authority != grant.epoch) { link.error = Error::StaleEpoch; continue; }
        Ref<SuperposSession> pin(Object::cast_to<SuperposSession>(ObjectDB::get_instance(link.session)));
        if (pin.is_null()) { link.error = Error::StaleGeneration; continue; }
        if (link.error != Error::None) continue;
        if (auto advanced = advance(link, *pin.ptr(), payload, now); !advanced) { link.error = advanced.error(); continue; }
        if (!link.attached) { ++progress.waiting; continue; }
        ++progress.attached; progress.ready += link.ready;
        if (link.started && link.ready == link.entities) ++progress.converged;
    }
    return progress;
}

superpos::Result<LinkStatus> RestoreFanout::status(std::uint32_t index) const noexcept {
    if (index >= count_) return fail(Error::InvalidArgument);
    const auto &link = links_[index];
    if (link.error != Error::None) return fail(link.error);
    LinkStatus result{link.attached, false, link.entities, link.cursor, link.ready, link.repairs_answered, link.repairs_deferred, {}};
    Ref<SuperposSession> pin(Object::cast_to<SuperposSession>(ObjectDB::get_instance(link.session)));
    if (pin.is_valid()) if (auto *owner = SuperposRecoveryAccess::authority(*pin.ptr())) { result.egress = owner->egress(); result.totals = owner->totals(); }
    return result;
}
}
