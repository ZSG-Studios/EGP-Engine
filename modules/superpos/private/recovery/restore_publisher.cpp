// SPDX-License-Identifier: MIT
#include "restore_publisher.hpp"
#include "superpos_session.h"
#include "core/os/thread.h"
#include <cstring>

namespace superpos_egp::recovery {
using superpos::Error;
using superpos::fail;
namespace {
Error from_engine(int error) noexcept {
    switch (error) {
        case OK: return Error::None;
        case ERR_BUSY: return Error::Busy;
        case ERR_OUT_OF_MEMORY: return Error::OutOfMemory;
        case ERR_UNCONFIGURED: return Error::NotReady;
        default: return Error::ChannelFailed;
    }
}
superpos::Status send(SuperposSession &session, const superpos::ReplicaWireMessage &message, std::uint32_t channel) noexcept {
    std::array<std::byte, 8192> bytes{};
    auto encoded = superpos::encode_replica_message(message, bytes); if (!encoded) return fail(encoded.error());
    PackedByteArray packet;
    if (packet.resize(int(*encoded)) != OK) return fail(Error::OutOfMemory);
    std::memcpy(packet.ptrw(), bytes.data(), *encoded);
    const Dictionary accepted = session.enqueue_packet(packet, channel);
    const int error = int(accepted.get("error", ERR_UNCONFIGURED));
    return error == OK ? superpos::Status{} : superpos::Status(fail(from_engine(error)));
}
}

superpos::Result<PublishResult> RestorePublisher::publish(SuperposSession &authority, superpos::AuthorityLease &lease,
        const superpos::AuthorityGrant &grant, const PublishRoute &route) noexcept {
    if (!Thread::is_main_thread()) return fail(Error::PermissionDenied);
    if (route.context.authority != grant.epoch || !route.context.connection || !route.context.replica)
        return fail(Error::StaleEpoch);
    // Point-in-time authority: never cached across this call's sends.
    auto authorized = lease.authorize(grant.owner, grant.kind, grant.epoch, grant.membership_generation);
    if (!authorized) return fail(authorized.error());
    auto target = SuperposRecoveryAccess::target(authority); if (!target) return fail(target.error());
    if (target->authority_epoch != grant.epoch) return fail(Error::StaleEpoch);
    const std::size_t limit = maximum_world_payload(target->capacity, target->state_stride);
    PackedByteArray storage;
    if (storage.resize(int(limit)) != OK) return fail(Error::OutOfMemory);
    std::span<std::byte> buffer(reinterpret_cast<std::byte *>(storage.ptrw()), limit);
    auto captured = SuperposRecoveryAccess::capture(authority, buffer); if (!captured) return fail(captured.error());
    const auto payload = buffer.first(*captured);
    auto header = validate_world_payload(payload, target->schemas, target->capacity); if (!header) return fail(header.error());
    if (header->count > applied_slots_.size() || header->count > UINT16_MAX) return fail(Error::CapacityExceeded);
    PublishResult result;
    result.first_sequence = sequence_ + 1;
    std::uint16_t slot = 0;
    auto sent = for_each_entity(payload, [&](const WorldEntity &entity) -> superpos::Status {
        // Recheck the lease before each entity: expiry mid-batch stops sending.
        if (auto again = lease.authorize(grant.owner, grant.kind, grant.epoch, grant.membership_generation); !again) return fail(again.error());
        const superpos::ReplicaKey key{++slot, route.incarnation, route.context.authority, route.context.connection,
            route.context.replica, route.encoding_epoch};
        superpos::ReplicaWireMessage bind;
        bind.kind = superpos::ReplicaWireKind::Bind; bind.context = route.context;
        bind.binding = {key, superpos::ObjectHandle::from_parts(entity.slot, entity.generation), entity.schema, entity.owner,
            entity.ownership_revision ? entity.ownership_revision : 1, sequence_ + 1};
        if (auto queued = send(authority, bind, route.control_channel); !queued) return queued;
        ++sequence_; ++result.binds;
        superpos::ReplicaWireMessage offer;
        offer.kind = superpos::ReplicaWireKind::BaselineOffer; offer.context = route.context; offer.sequence = sequence_;
        offer.baseline = {key, 0, 1, entity.revision, entity.state}; offer.tick = entity.tick;
        if (auto queued = send(authority, offer, route.bulk_channel); !queued) return queued;
        ++result.baselines;
        return {};
    });
    result.last_sequence = sequence_;
    published_ += result.binds;
    if (!sent) return fail(sent.error());
    return result;
}

superpos::Result<std::uint32_t> RestorePublisher::collect(SuperposSession &authority, const PublishRoute &route) noexcept {
    if (!Thread::is_main_thread()) return fail(Error::PermissionDenied);
    // An available Session distinguishes an empty queue from a lost owner:
    // core reports no ready delivery as NotReady (ERR_UNCONFIGURED).
    if (auto ready = SuperposRecoveryAccess::target(authority); !ready) return fail(ready.error());
    for (int n = 0; n < 256; ++n) {
        const Dictionary packet = authority.read_packet(route.control_channel);
        const int error = int(packet.get("error", ERR_UNCONFIGURED));
        if (error == ERR_BUSY || error == ERR_UNCONFIGURED) break;
        if (error != OK) return fail(from_engine(error));
        const PackedByteArray payload = packet["payload"];
        auto message = superpos::decode_replica_message(std::span(reinterpret_cast<const std::byte *>(payload.ptr()), size_t(payload.size())));
        (void)authority.acknowledge_packet(uint64_t(int64_t(packet["message"])), uint64_t(int64_t(packet["binding_generation"])), route.control_channel);
        if (!message || message->kind != superpos::ReplicaWireKind::SpawnApplied) continue;
        const auto &key = message->spawned.key;
        if (message->context != route.context || key.authority_epoch != route.context.authority || !key.slot ||
                key.slot > published_ || key.slot > applied_slots_.size()) continue;
        if (!applied_slots_[key.slot - 1]) { applied_slots_[key.slot - 1] = true; ++applied_; }
    }
    return applied_;
}
}
