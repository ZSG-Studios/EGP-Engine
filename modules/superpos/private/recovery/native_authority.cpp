// SPDX-License-Identifier: MIT
#include "native_authority.hpp"

namespace superpos_egp::recovery {
using superpos::Error;
using superpos::fail;
using superpos::MemoryDomain;

NativeAuthority::NativeAuthority(superpos::Allocator &allocator) noexcept
    : records_(allocator, MemoryDomain::Replication), images_(allocator, MemoryDomain::Replication),
      arena_(allocator, MemoryDomain::Replication), validation_(allocator, MemoryDomain::Replication),
      freeze_scratch_(allocator, MemoryDomain::Session), captures_(allocator, MemoryDomain::Replication),
      exposures_(allocator, MemoryDomain::Replication), repairs_(allocator, MemoryDomain::Replication) {}

superpos::Status NativeAuthority::initialize(superpos::Session &session, superpos::SchemaRegistry &registry,
        superpos::ReplicaConfig config, superpos::ReplicaWireContext context, std::uint32_t records) noexcept {
    using Bridge = superpos::ReplicaAuthoritySession;
    if (initialized_ || !records || records > Bridge::maximum_exposures || config.maximum_active > records ||
            config.maximum_transitions > records) return fail(Error::InvalidArgument);
    // Complete capacity before any wire-visible state exists.
    for (auto status : {records_.initialize(records), images_.initialize(std::size_t(records) * config.state_stride * 2),
            arena_.initialize(Bridge::maximum_captures * Bridge::capture_bytes), validation_.initialize(65536),
            freeze_scratch_.initialize(65536), captures_.initialize(Bridge::maximum_captures),
            exposures_.initialize(records), repairs_.initialize(Bridge::maximum_repairs)})
        if (!status) return status;
    auto frozen = registry.freeze(freeze_scratch_.span()); if (!frozen) return fail(frozen.error());
    registry_.emplace(std::move(*frozen));
    auto sender = superpos::PeerReplicas::create(config, records_.span(), images_.span()); if (!sender) return fail(sender.error());
    sender_.emplace(std::move(*sender));
    auto bridge = Bridge::create(session, *sender_, *registry_, context, {static_cast<std::uint8_t>(control_channel),
        static_cast<std::uint8_t>(state_channel), static_cast<std::uint8_t>(bulk_channel)},
        captures_.span(), arena_.span(), exposures_.span(), repairs_.span(), validation_.span());
    if (!bridge) { sender_.reset(); registry_.reset(); return fail(bridge.error()); }
    bridge_.emplace(std::move(*bridge));
    context_ = context; initialized_ = true;
    return {};
}

superpos::Result<superpos::ReplicaAuthorityProgress> NativeAuthority::pump(superpos::Tick tick) noexcept {
    if (!initialized_) return fail(Error::NotReady);
    if (!egress_) { ++totals_.gated; return superpos::ReplicaAuthorityProgress{}; }
    auto progress = bridge_->pump(tick);
    if (!progress) return fail(progress.error());
    ++totals_.pumps;
    totals_.sent += progress->sent; totals_.transport_applied += progress->transport_applied;
    totals_.carrier_retired += progress->carrier_retired; totals_.received += progress->received;
    totals_.discarded += progress->discarded; totals_.backpressured += progress->backpressured;
    return progress;
}
}
