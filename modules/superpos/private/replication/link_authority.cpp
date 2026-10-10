// SPDX-License-Identifier: MIT
#include "link_authority.hpp"
#include <algorithm>
#include <bit>

namespace superpos_egp::replication {
using superpos::Error;
using superpos::fail;
using superpos::MemoryDomain;
using superpos::ReplicaWireKind;

namespace {
// Transient bridge outcomes: work stays queued and is retried next frame.
bool pressure(Error error) noexcept {
    return error == Error::Busy || error == Error::CapacityExceeded || error == Error::NotReady;
}
}

LinkAuthority::LinkAuthority(superpos::Allocator &allocator) noexcept
    : records_(allocator, MemoryDomain::Replication), images_(allocator, MemoryDomain::Replication),
      arena_(allocator, MemoryDomain::Replication), validation_(allocator, MemoryDomain::Replication),
      freeze_scratch_(allocator, MemoryDomain::Session), compose_(allocator, MemoryDomain::Replication),
      captures_(allocator, MemoryDomain::Replication), exposures_(allocator, MemoryDomain::Replication),
      repairs_(allocator, MemoryDomain::Replication), tracked_(allocator, MemoryDomain::Replication),
      map_(allocator, MemoryDomain::Replication), candidates_(allocator, MemoryDomain::Replication),
      region_scratch_(allocator, MemoryDomain::Replication), message_(allocator, MemoryDomain::Session) {}

superpos::Status LinkAuthority::fail_with(Error error) noexcept {
    if (failure_ == Error::None) failure_ = error;
    return fail(error);
}

superpos::Status LinkAuthority::reserve(const LinkConfig &config, std::size_t stride, std::uint64_t incarnation, std::uint32_t region_members) noexcept {
    using Bridge = superpos::ReplicaAuthoritySession;
    if (reserved_) return fail(Error::Busy);
    const std::size_t records = std::size_t(config.maximum_active) + config.maximum_transitions;
    if (!config.context.replica || !incarnation || !config.maximum_active || config.maximum_active > 1000 || !config.maximum_transitions || config.maximum_transitions > 64 ||
            records > maximum_records || !config.scan_budget || !stride || stride > superpos::Schema::maximum_state_bytes || !region_members ||
            config.routes.control >= 32 || config.routes.state >= 32 || config.routes.bulk >= 32 ||
            config.routes.control == config.routes.state || config.routes.control == config.routes.bulk || config.routes.state == config.routes.bulk)
        return fail(Error::InvalidArgument);
    std::size_t buckets = 1;
    while (buckets < records * 2) buckets <<= 1;
    for (auto status : {records_.initialize(records), images_.initialize(records * stride * 2),
            arena_.initialize(Bridge::maximum_captures * Bridge::capture_bytes), validation_.initialize(65536),
            freeze_scratch_.initialize(65536), compose_.initialize(compose_bytes * 2 + stride),
            captures_.initialize(Bridge::maximum_captures), exposures_.initialize(records),
            repairs_.initialize(Bridge::maximum_repairs), tracked_.initialize(records), map_.initialize(buckets),
            candidates_.initialize(records), region_scratch_.initialize(region_members), message_.initialize(1)})
        if (!status) return status;
    config_ = config;
    state_stride_ = stride;
    incarnation_ = incarnation;
    reserved_ = true;
    return {};
}

superpos::Status LinkAuthority::bind(superpos::Session &session, superpos::SchemaRegistry &registry,
        std::span<const superpos::Schema> schemas, superpos::Epoch authority, superpos::Epoch connection, superpos::PeerId peer) noexcept {
    using Bridge = superpos::ReplicaAuthoritySession;
    if (!reserved_ || bridge_) return fail(Error::Busy);
    if (failure_ != Error::None) return fail(failure_);
    if (schemas.empty() || !authority || !connection) return fail(Error::InvalidArgument);
    config_.context.authority = authority;
    config_.context.connection = connection;
    config_.peer = peer;
    for (const auto &value : schemas) if (value.state_bytes() > state_stride_) return fail(Error::CapacityExceeded);
    auto frozen = registry.freeze(freeze_scratch_.span());
    if (!frozen) return fail(frozen.error());
    registry_.emplace(std::move(*frozen));
    superpos::ReplicaConfig replica;
    replica.authority_epoch = config_.context.authority;
    replica.connection_epoch = config_.context.connection;
    replica.replica_epoch = config_.context.replica;
    replica.maximum_active = config_.maximum_active;
    replica.maximum_transitions = config_.maximum_transitions;
    replica.state_stride = state_stride_;
    replica.peer = config_.peer;
    auto sender = superpos::PeerReplicas::create(replica, records_.span(), images_.span());
    if (!sender) { registry_.reset(); return fail(sender.error()); }
    sender_.emplace(std::move(*sender));
    auto bridge = Bridge::create(session, *sender_, *registry_, config_.context, config_.routes, captures_.span(),
        arena_.span(), exposures_.span(), repairs_.span(), validation_.span());
    if (!bridge) { sender_.reset(); registry_.reset(); return fail(bridge.error()); }
    bridge_.emplace(std::move(*bridge));
    schemas_ = schemas;
    return {};
}

superpos::Status LinkAuthority::set_interest(std::span<const std::uint32_t> regions, bool all) noexcept {
    if (regions.size() > maximum_interest) return fail(Error::CapacityExceeded);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (!regions[i]) return fail(Error::InvalidArgument); // region 0 is always visible
        for (std::size_t j = 0; j < i; ++j) if (regions[i] == regions[j]) return fail(Error::InvalidArgument);
    }
    std::copy(regions.begin(), regions.end(), interest_.begin());
    interest_count_ = regions.size();
    interest_all_ = all;
    region_cursor_ = region_offset_ = 0;
    return {};
}

const superpos::Schema *LinkAuthority::schema(superpos::SchemaId id) const noexcept {
    for (const auto &value : schemas_) if (value.id() == id) return &value;
    return nullptr;
}

superpos::Result<const superpos::SchemaRecord *> LinkAuthority::catalog(superpos::SchemaId id) const noexcept {
    if (!registry_) return fail(Error::NotReady);
    return registry_->find(id);
}

std::size_t LinkAuthority::probe(std::uint32_t world_slot) const noexcept {
    // Fibonacci hashing; the table size is a power of two.
    const std::uint64_t mixed = (std::uint64_t(world_slot) + 1) * 0x9E3779B97F4A7C15ull;
    return std::size_t(mixed >> 17) & (map_.size() - 1);
}

std::optional<std::uint16_t> LinkAuthority::find(std::uint32_t world_slot) const noexcept {
    auto entries = map_.span();
    for (std::size_t i = probe(world_slot), n = 0; n < entries.size(); ++n, i = (i + 1) & (entries.size() - 1)) {
        if (!entries[i].world_slot_plus_one) return std::nullopt;
        if (entries[i].world_slot_plus_one == world_slot + 1) return entries[i].sender_slot;
    }
    return std::nullopt;
}

superpos::Status LinkAuthority::insert(std::uint32_t world_slot, std::uint16_t sender_slot) noexcept {
    if (world_slot == UINT32_MAX) return fail(Error::InvalidArgument);
    auto entries = map_.span();
    for (std::size_t i = probe(world_slot), n = 0; n < entries.size(); ++n, i = (i + 1) & (entries.size() - 1)) {
        if (entries[i].world_slot_plus_one == world_slot + 1) return fail(Error::ProtocolViolation);
        if (!entries[i].world_slot_plus_one) { entries[i] = {world_slot + 1, sender_slot}; return {}; }
    }
    return fail(Error::CapacityExceeded);
}

void LinkAuthority::erase(std::uint32_t world_slot) noexcept {
    auto entries = map_.span();
    const std::size_t mask = entries.size() - 1;
    std::size_t i = probe(world_slot), n = 0;
    for (; n < entries.size(); ++n, i = (i + 1) & mask) {
        if (!entries[i].world_slot_plus_one) return;
        if (entries[i].world_slot_plus_one == world_slot + 1) break;
    }
    if (n == entries.size()) return;
    // Backward-shift deletion keeps every probe chain contiguous.
    for (std::size_t hole = i, j = (i + 1) & mask;; j = (j + 1) & mask) {
        if (!entries[j].world_slot_plus_one) { entries[hole] = {}; return; }
        const std::size_t home = probe(entries[j].world_slot_plus_one - 1);
        const bool between = hole <= j ? (home > hole && home <= j) : (home > hole || home <= j);
        if (!between) { entries[hole] = entries[j]; hole = j; }
    }
}

const SlotAttributes *LinkAuthority::attributes(const WorldFrame &frame, std::uint32_t world_slot) const noexcept {
    if (world_slot >= frame.attributes.size() || world_slot >= frame.slots.size()) return nullptr;
    const auto &value = frame.attributes[world_slot];
    return value.generation == frame.slots[world_slot].generation ? &value : nullptr;
}

bool LinkAuthority::visible(const WorldFrame &frame, std::uint32_t world_slot) const noexcept {
    if (interest_all_) return true;
    const auto *value = attributes(frame, world_slot);
    const std::uint32_t region = value ? value->region : 0;
    if (!region) return true;
    for (std::size_t i = 0; i < interest_count_; ++i) if (interest_[i] == region) return true;
    return false;
}

superpos::Result<superpos::EntityView> LinkAuthority::live(const WorldFrame &frame, const Tracked &value) const noexcept {
    if (value.world_slot >= frame.slots.size()) return fail(Error::StaleGeneration);
    const auto &slot = frame.slots[value.world_slot];
    if (!slot.live || slot.generation != value.generation) return fail(Error::StaleGeneration);
    return frame.world->view(superpos::ObjectHandle::from_parts(value.world_slot + 1, slot.generation));
}

superpos::Result<std::uint64_t> LinkAuthority::next_revision() noexcept {
    auto next = superpos::increment(wire_revision_);
    if (!next) return fail(next.error());
    return *next;
}

// Queue the single issued-but-unqueued lifecycle record. Core lifecycle
// sequences must reach the ordered control route in issue order, so no new
// lifecycle operation is issued while one is pending.
superpos::Result<bool> LinkAuthority::flush_control(const WorldFrame &frame) noexcept {
    if (pending_ == Control::None) return true;
    auto &value = tracked_[pending_slot_ - 1];
    auto &message = message_[0];
    message = {};
    message.context = config_.context;
    if (pending_ == Control::Bind) {
        auto record = sender_->inspect(value.binding.key);
        if (!record) return fail(record.error());
        message.kind = ReplicaWireKind::Bind;
        message.binding = {value.binding.key, value.binding.handle, record->schema->id(), record->owner,
            record->ownership_revision, value.binding.lifecycle_sequence};
    } else if (pending_ == Control::Leave) {
        auto record = sender_->inspect(value.binding.key);
        if (!record) return fail(record.error());
        message.kind = ReplicaWireKind::Leave;
        message.key = value.binding.key;
        message.sequence = record->leave_sequence;
    } else {
        auto reset = sender_->pending_reset(value.binding.key);
        if (!reset) return fail(reset.error());
        message.kind = ReplicaWireKind::OwnershipReset;
        message.key = reset->old_key;
        message.new_key = reset->new_key;
        message.owner = reset->owner;
        message.ownership_revision = reset->ownership_revision;
        message.sequence = reset->lifecycle_sequence;
    }
    message.tick = frame.tick;
    auto queued = bridge_->queue(message);
    if (!queued) {
        if (pressure(queued.error())) { ++totals_.capture_pressure; return false; }
        return fail(queued.error());
    }
    if (pending_ == Control::Bind) value.stage = Stage::NeedOffer;
    else if (pending_ == Control::Leave) value.stage = Stage::Leaving;
    else value.stage = Stage::NeedResetOffer;
    pending_ = Control::None;
    pending_slot_ = 0;
    return true;
}

superpos::Result<bool> LinkAuthority::queue_offer(const WorldFrame &frame, Tracked &value) noexcept {
    auto pending = sender_->pending_offer(value.binding.key);
    if (!pending) return fail(pending.error());
    // The receiver pins an offer by its token together with its revision and
    // tick; a second copy stamped with a later tick is a protocol violation.
    if (pending->candidate_token == value.queued_offer_token) {
        if (value.stage == Stage::NeedOffer) value.stage = Stage::AwaitReady;
        else if (value.stage == Stage::NeedResetOffer) value.stage = Stage::AwaitResetPin;
        return true;
    }
    auto record = sender_->inspect(value.binding.key);
    if (!record) return fail(record.error());
    auto &message = message_[0];
    message = {};
    message.kind = ReplicaWireKind::BaselineOffer;
    message.context = config_.context;
    message.baseline = *pending;
    message.sequence = std::max(record->spawn_sequence, record->reset_sequence);
    message.tick = frame.tick;
    auto queued = bridge_->queue(message);
    if (!queued) {
        if (pressure(queued.error())) { ++totals_.capture_pressure; return false; }
        return fail(queued.error());
    }
    value.queued_offer_token = pending->candidate_token;
    if (value.stage == Stage::NeedOffer) value.stage = Stage::AwaitReady;
    else if (value.stage == Stage::NeedResetOffer) value.stage = Stage::AwaitResetPin;
    return true;
}

// Promote the active baseline to the current state of a Ready replica. The
// sender store refuses (Busy) inside its two-per-second interval or while an
// offer or retirement is outstanding; the attempt is simply retried later.
superpos::Result<int> LinkAuthority::queue_promotion(const WorldFrame &frame, Tracked &value) noexcept {
    auto record = sender_->inspect(value.binding.key);
    if (!record) return fail(record.error());
    if (record->phase != superpos::ReplicaPhase::Ready || record->active_image < 0) return 0;
    if (record->offered_image < 0) {
        if (record->retiring_image >= 0) return 0;
        auto entity = live(frame, value);
        if (!entity) return 0;
        auto revision = next_revision();
        if (!revision) return fail(revision.error());
        auto offered = sender_->offer_baseline(value.binding.key, entity->canonical, *revision, frame.now_milliseconds);
        if (!offered) {
            if (offered.error() == Error::Busy) return 0; // inside the two-per-second interval
            return fail(offered.error());
        }
        wire_revision_ = *revision;
    } else {
        // An offer already on the wire waits for its pin; never resend it.
        auto pending = sender_->pending_offer(value.binding.key);
        if (!pending) return fail(pending.error());
        if (pending->candidate_token == value.queued_offer_token) return 0;
    }
    auto queued = queue_offer(frame, value);
    if (!queued) return fail(queued.error());
    if (!*queued) return 2; // offer stays pending in the sender; retried later
    value.promotion_pending = false;
    ++totals_.promotions;
    return 1;
}

// Enroll one untracked live object. Returns false under capacity pressure.
superpos::Result<bool> LinkAuthority::enroll(const WorldFrame &frame, std::uint32_t world_slot) noexcept {
    if (pending_ != Control::None) return false;
    const auto &slot = frame.slots[world_slot];
    auto entity = frame.world->view(superpos::ObjectHandle::from_parts(world_slot + 1, slot.generation));
    if (!entity) return fail(entity.error());
    const auto *local = schema(entity->schema->id());
    if (!local || !local->compatible_with(*entity->schema)) return fail(Error::IncompatibleSchema);
    auto revision = next_revision();
    if (!revision) return fail(revision.error());
    superpos::EntityView view = *entity;
    view.schema = local;
    auto binding = sender_->begin_spawn(view);
    if (!binding) {
        if (binding.error() == Error::CapacityExceeded) { ++totals_.deferred; return false; }
        return fail(binding.error());
    }
    // The offered image is projected for this peer by the sender store.
    auto offered = sender_->offer_baseline(binding->key, entity->canonical, *revision, frame.now_milliseconds);
    if (!offered) return fail(offered.error());
    wire_revision_ = *revision;
    auto &value = tracked_[binding->key.slot - 1];
    if (value.stage != Stage::Free) return fail(Error::ProtocolViolation);
    value = {};
    value.stage = Stage::NeedBind;
    value.world_slot = world_slot;
    value.generation = slot.generation;
    value.binding = *binding;
    value.owner = entity->owner;
    value.world_revision = entity->revision;
    value.ownership_revision = entity->ownership_revision;
    if (auto inserted = insert(world_slot, binding->key.slot); !inserted) return fail(inserted.error());
    pending_ = Control::Bind;
    pending_slot_ = binding->key.slot;
    ++totals_.spawns;
    auto flushed = flush_control(frame);
    if (!flushed || !*flushed) return flushed;
    return queue_offer(frame, value);
}

superpos::Status LinkAuthority::enrollment(const WorldFrame &frame, bool &control_open, bool &capture_open, std::size_t maximum) noexcept {
    const auto slots = std::uint32_t(frame.slots.size());
    if (!slots || !maximum) return {};
    std::size_t enrolled_count = 0;
    auto consider = [&](std::uint32_t world_slot, std::uint32_t generation, bool &stop) -> superpos::Status {
        if (world_slot >= slots) return {};
        const auto &slot = frame.slots[world_slot];
        if (!slot.live || slot.generation != generation) return {};
        if (auto index = find(world_slot)) {
            // A slot reused under a newer generation retires the old binding first.
            auto &value = tracked_[*index - 1];
            if (value.generation != slot.generation && value.stage != Stage::NeedLeave && value.stage != Stage::Leaving &&
                    value.stage != Stage::NeedBind && value.stage != Stage::NeedOffer) value.stage = Stage::NeedLeave;
            return {};
        }
        if (!visible(frame, world_slot)) return {};
        auto enrolled = enroll(frame, world_slot);
        if (!enrolled) return fail_with(enrolled.error());
        if (!*enrolled) {
            control_open = false;
            if (pending_ != Control::None) capture_open = false;
            stop = true;
        } else if (++enrolled_count >= maximum) {
            stop = true;
        }
        return {};
    };
    std::uint32_t visited = 0;
    bool stop = false;
    if (!interest_all_ && frame.regions) {
        // Indexed relevance: region 0 plus each interest region in turn; one
        // collected region per step, resuming at the saved offset.
        const std::size_t lists = interest_count_ + 1;
        for (std::size_t round = 0; round < lists && !stop && visited < config_.scan_budget; ++round) {
            const std::size_t which = region_cursor_ % lists;
            const std::uint32_t region = which ? interest_[which - 1] : 0;
            auto count = frame.regions->collect(region, region_scratch_.span());
            if (!count) {
                // A region larger than the scratch cannot be enumerated; it is
                // an invalid server configuration, not a peer fault.
                if (count.error() == Error::InvalidArgument) { region_cursor_ = std::uint32_t((which + 1) % lists); region_offset_ = 0; continue; }
                return fail_with(count.error());
            }
            for (; region_offset_ < *count && !stop && visited < config_.scan_budget; ++region_offset_, ++visited) {
                const auto handle = region_scratch_[region_offset_];
                if (auto status = consider(handle.slot() - 1, handle.generation(), stop); !status) return status;
                if (stop) break;
            }
            if (region_offset_ >= *count) { region_cursor_ = std::uint32_t((which + 1) % lists); region_offset_ = 0; }
        }
        return {};
    }
    for (; visited < std::min(config_.scan_budget, slots) && !stop; ++visited) {
        const std::uint32_t world_slot = cursor_;
        if (auto status = consider(world_slot, frame.slots[world_slot].generation, stop); !status) return status;
        cursor_ = (cursor_ + 1) % slots;
    }
    return {};
}

superpos::Result<int> LinkAuthority::add_member(const WorldFrame &frame, Batch &batch, std::uint16_t sender_slot) noexcept {
    auto &value = tracked_[sender_slot - 1];
    auto entity = live(frame, value);
    if (!entity) return 0;
    auto record = sender_->inspect(value.binding.key);
    if (!record) return fail(record.error());
    if (record->phase != superpos::ReplicaPhase::Ready || record->active_image < 0) return 0;
    auto contract = catalog(record->schema->id());
    if (!contract) return fail(contract.error());
    const auto &terms = (*contract)->contract;
    const std::uint64_t group = terms.publication == superpos::PublicationSemantics::ExplicitAtomicGroup ? terms.group_id : 0;
    const std::size_t limit = group ? std::min<std::size_t>(maximum_members, terms.maximum_group_objects) : 1;
    const std::size_t size = record->schema->state_bytes();
    if (batch.count && (batch.group != group || !group || batch.count >= limit)) return 2;
    if (batch.state_bytes + size > std::min<std::size_t>(compose_bytes, terms.maximum_group_bytes)) {
        if (batch.count) return 2;
        return fail(Error::CapacityExceeded);
    }
    // Delta/canonical staging: [0,compose) payload, [2*compose, +stride) projection.
    auto projected = compose_.span().subspan(compose_bytes * 2, size);
    if (auto masked = record->schema->project(entity->canonical, config_.peer == record->owner, false, projected); !masked)
        return fail(masked.error());
    auto &message = message_[0];
    const std::size_t index = batch.count;
    if (batch.kind == ReplicaWireKind::Publication) {
        const auto token = record->images[record->active_image].token;
        auto base = sender_->active_baseline(value.binding.key, token);
        if (!base) return fail(base.error());
        auto payload = compose_.span().subspan(batch.delta_bytes, compose_bytes - batch.delta_bytes);
        auto encoded = superpos::encode_delta(*record->schema, *base, projected, payload);
        if (!encoded) {
            if (encoded.error() == Error::CapacityExceeded && batch.count) return 2;
            return fail(encoded.error());
        }
        message.patches[index] = {value.binding.key, token, 0, payload.first(*encoded)};
        batch.delta_bytes += *encoded;
        batch.member_bytes[index] = std::uint32_t(*encoded);
    } else {
        if (batch.delta_bytes + size > compose_bytes) {
            if (batch.count) return 2;
            return fail(Error::CapacityExceeded);
        }
        auto copy = compose_.span().subspan(batch.delta_bytes, size);
        std::copy(projected.begin(), projected.end(), copy.begin());
        message.repairs[index] = {value.binding.key, 0, copy};
        batch.delta_bytes += size;
    }
    message.sequence = std::max(message.sequence, std::max(record->spawn_sequence, record->reset_sequence));
    batch.group = group;
    batch.state_bytes += size;
    batch.members[index] = sender_slot;
    batch.world_revisions[index] = entity->revision;
    ++batch.count;
    return 1;
}

superpos::Result<bool> LinkAuthority::flush_batch(const WorldFrame &frame, Batch &batch) noexcept {
    if (!batch.count) return true;
    auto revision = next_revision();
    if (!revision) return fail(revision.error());
    auto &message = message_[0];
    // Batch records must name strictly increasing compact slots on the wire;
    // members were added in priority order, so sort them (and their
    // bookkeeping) by slot. Insertion sort over at most sixteen members.
    for (std::size_t i = 1; i < batch.count; ++i) {
        for (std::size_t j = i; j > 0; --j) {
            const auto left = batch.kind == ReplicaWireKind::Publication ? message.patches[j - 1].key.slot : message.repairs[j - 1].key.slot;
            const auto right = batch.kind == ReplicaWireKind::Publication ? message.patches[j].key.slot : message.repairs[j].key.slot;
            if (left < right) break;
            std::swap(message.patches[j - 1], message.patches[j]);
            std::swap(message.repairs[j - 1], message.repairs[j]);
            std::swap(batch.members[j - 1], batch.members[j]);
            std::swap(batch.world_revisions[j - 1], batch.world_revisions[j]);
            std::swap(batch.member_bytes[j - 1], batch.member_bytes[j]);
        }
    }
    message.kind = batch.kind;
    message.context = config_.context;
    message.group_id = batch.group;
    message.tick = frame.tick;
    message.revision = *revision;
    message.count = std::uint16_t(batch.count);
    for (std::size_t i = 0; i < batch.count; ++i) {
        if (batch.kind == ReplicaWireKind::Publication) message.patches[i].revision = *revision;
        else message.repairs[i].revision = *revision;
    }
    auto queued = bridge_->queue(message);
    const auto kind = batch.kind;
    batch = Batch{kind};
    message = {};
    if (!queued) {
        if (pressure(queued.error())) { ++totals_.capture_pressure; return false; }
        return fail(queued.error());
    }
    wire_revision_ = *revision;
    return true;
}

// Bookkeeping after a queued batch: members are clean at the captured world
// revision, and published members become baseline promotion candidates.
superpos::Status LinkAuthority::after_send(const WorldFrame &frame, const Batch &sent) noexcept {
    step_bytes_ += sent.delta_bytes;
    for (std::size_t m = 0; m < sent.count; ++m) {
        auto &member = tracked_[sent.members[m] - 1];
        member.world_revision = sent.world_revisions[m];
        member.dirty_since = 0;
        member.served_step = frame.step;
        if (sent.kind == ReplicaWireKind::FullRepair) { member.repair = false; ++totals_.repairs; }
        else {
            ++totals_.published_members;
            // Promote once a delta carries more than a quarter of the state:
            // a fresher baseline then shrinks every later delta.
            auto record = sender_->inspect(member.binding.key);
            if (config_.baseline_promotion && record && sent.member_bytes[m] > 8 + record->schema->state_bytes() / 4) member.promotion_pending = true;
        }
    }
    if (sent.kind == ReplicaWireKind::Publication) ++totals_.publications;
    return {};
}

superpos::Status LinkAuthority::step(const WorldFrame &frame) noexcept {
    if (failure_ != Error::None) return fail(failure_);
    if (!bridge_) return fail(Error::NotReady);
    if (!frame.world || frame.slots.size() > UINT32_MAX || !frame.step) return fail(Error::InvalidArgument);
    ++totals_.steps;
    step_bytes_ = 0;
    auto checked = [&](superpos::Result<bool> result, bool &open) -> superpos::Status {
        if (!result) return fail_with(result.error());
        if (!*result) open = false;
        return {};
    };
    bool control_open = true, capture_open = true;
    // At most one baseline promotion per step: offers are individual captures.
    unsigned promotions = 0;
    if (auto status = checked(flush_control(frame), control_open); !status) return status;
    const auto records = tracked_.size();
    // Lifecycle, relevance and offers first; they free or establish capacity.
    for (std::size_t n = 0; n < records; ++n) {
        const std::size_t i = (rotation_ + n) % records;
        auto &value = tracked_[i];
        if (value.stage == Stage::Free) continue;
        const bool alive = bool(live(frame, value));
        const bool shown = alive && visible(frame, value.world_slot);
        switch (value.stage) {
        case Stage::NeedOffer: case Stage::NeedResetOffer:
            if (capture_open) if (auto status = checked(queue_offer(frame, value), capture_open); !status) return status;
            break;
        case Stage::AwaitReady: case Stage::AwaitResetPin: {
            auto record = sender_->inspect(value.binding.key);
            if (!record) return fail_with(record.error());
            if (value.stage == Stage::AwaitReady && record->phase == superpos::ReplicaPhase::Ready) value.stage = Stage::Ready;
            if (value.stage == Stage::AwaitResetPin && record->active_image >= 0) { value.stage = Stage::Ready; value.repair = true; }
            if (!shown && value.stage != Stage::Ready) { value.hidden = alive; value.stage = Stage::NeedLeave; }
            break;
        }
        case Stage::Leaving: {
            auto record = sender_->inspect(value.binding.key);
            if (record) break;
            if (record.error() != Error::StaleGeneration) return fail_with(record.error());
            erase(value.world_slot);
            ++totals_.leaves;
            if (value.hidden) ++totals_.relevance_leaves;
            value = {};
            break;
        }
        default: break;
        }
        if (value.stage == Stage::Ready && !shown) { value.hidden = alive; value.stage = Stage::NeedLeave; }
        if (value.stage == Stage::Ready) {
            const auto &slot = frame.slots[value.world_slot];
            if (slot.ownership_revision != value.ownership_revision || slot.owner != value.owner) value.stage = Stage::NeedReset;
            else if (slot.revision != value.world_revision && !value.dirty_since) value.dirty_since = frame.step;
            if (value.stage == Stage::Ready && value.promotion_pending && capture_open && !value.repair && promotions < 1) {
                ++promotions;
                auto promoted = queue_promotion(frame, value);
                if (!promoted) return fail_with(promoted.error());
                if (*promoted == 2) capture_open = false;
            }
        }
        if (!control_open || pending_ != Control::None) continue;
        if (value.stage == Stage::NeedLeave) {
            auto sequence = sender_->begin_retire(value.binding.key);
            if (!sequence) {
                if (sequence.error() == Error::CapacityExceeded) { control_open = false; continue; }
                return fail_with(sequence.error());
            }
            pending_ = Control::Leave; pending_slot_ = std::uint16_t(i + 1);
            if (auto status = checked(flush_control(frame), control_open); !status) return status;
        } else if (value.stage == Stage::NeedReset) {
            const auto &slot = frame.slots[value.world_slot];
            auto reset = sender_->ownership_changed(value.binding.key, slot.owner, slot.ownership_revision);
            if (!reset) {
                if (pressure(reset.error())) { control_open = false; continue; }
                return fail_with(reset.error());
            }
            auto entity = live(frame, value);
            if (!entity) return fail_with(entity.error());
            auto revision = next_revision();
            if (!revision) return fail_with(revision.error());
            value.binding.key = reset->new_key;
            value.owner = slot.owner;
            value.ownership_revision = slot.ownership_revision;
            value.world_revision = entity->revision;
            value.promotion_pending = false;
            value.dirty_since = 0;
            auto offered = sender_->offer_baseline(reset->new_key, entity->canonical, *revision, frame.now_milliseconds);
            if (!offered) return fail_with(offered.error());
            wire_revision_ = *revision;
            ++totals_.ownership_resets;
            pending_ = Control::Reset; pending_slot_ = std::uint16_t(i + 1);
            if (auto status = checked(flush_control(frame), control_open); !status) return status;
        }
    }
    // One enrollment ahead of state: under steady state traffic the capture
    // queue is always full by the end of a step, so spawns enrolled only after
    // state would starve. Further enrollments use what state leaves open.
    if (control_open && capture_open) if (auto status = enrollment(frame, control_open, capture_open, 1); !status) return status;
    auto budget_spent = [&]() { return config_.state_budget_bytes && step_bytes_ >= config_.state_budget_bytes; };
    // Repairs first (they restore correctness), in atomic batches of up to 16.
    {
        Batch batch{ReplicaWireKind::FullRepair};
        message_[0] = {};
        auto send = [&]() -> superpos::Status {
            const Batch sent = batch;
            if (auto status = checked(flush_batch(frame, batch), capture_open); !status) return status;
            if (capture_open) return after_send(frame, sent);
            return {};
        };
        for (std::size_t n = 0; n < records && capture_open; ++n) {
            const std::size_t i = (rotation_ + n) % records;
            if (tracked_[i].stage != Stage::Ready || !tracked_[i].repair) continue;
            auto added = add_member(frame, batch, std::uint16_t(i + 1));
            if (!added) return fail_with(added.error());
            if (*added != 2) continue;
            if (auto status = send(); !status) return status;
            if (!capture_open) break;
            message_[0] = {};
            added = add_member(frame, batch, std::uint16_t(i + 1));
            if (!added) return fail_with(added.error());
        }
        if (capture_open && batch.count) if (auto status = send(); !status) return status;
    }
    // State: weight times age, explicit groups atomically, within the budget.
    std::size_t candidates = 0;
    for (std::size_t i = 0; i < records; ++i) {
        const auto &value = tracked_[i];
        if (value.stage != Stage::Ready || value.repair || !value.dirty_since) continue;
        const auto *attribute = attributes(frame, value.world_slot);
        if (attribute && attribute->dormant) continue;
        const std::uint64_t weight = attribute ? std::max<std::uint16_t>(attribute->weight, 1) : 1;
        candidates_[candidates++] = {weight * (frame.step - value.dirty_since + 1), std::uint16_t(i + 1)};
    }
    std::sort(candidates_.span().begin(), candidates_.span().begin() + std::ptrdiff_t(candidates),
        [](const Candidate &a, const Candidate &b) { return a.score != b.score ? a.score > b.score : a.sender_slot < b.sender_slot; });
    Batch batch{ReplicaWireKind::Publication};
    message_[0] = {};
    bool queued_any = false;
    auto send = [&]() -> superpos::Status {
        const Batch sent = batch;
        if (auto status = checked(flush_batch(frame, batch), capture_open); !status) return status;
        message_[0] = {};
        if (!capture_open) return {};
        queued_any = true;
        return after_send(frame, sent);
    };
    for (std::size_t c = 0; c < candidates && capture_open; ++c) {
        const auto sender_slot = candidates_[c].sender_slot;
        auto &value = tracked_[sender_slot - 1];
        if (value.served_step == frame.step || value.stage != Stage::Ready) continue;
        if (queued_any && budget_spent()) { ++totals_.budget_limited; break; }
        const auto *attribute = attributes(frame, value.world_slot);
        const std::uint32_t group = attribute ? attribute->group : 0;
        if (!group) {
            auto added = add_member(frame, batch, sender_slot);
            if (!added) return fail_with(added.error());
            if (*added != 2) continue;
            if (auto status = send(); !status) return status;
            if (!capture_open || (queued_any && budget_spent())) break;
            added = add_member(frame, batch, sender_slot);
            if (!added) return fail_with(added.error());
            continue;
        }
        // Explicit group: every visible tracked member must be Ready; publish
        // all of them alone in one atomic message.
        if (batch.count) {
            if (auto status = send(); !status) return status;
            if (!capture_open) break;
        }
        bool complete = true;
        std::array<std::uint16_t, maximum_members> members{};
        std::size_t member_count = 0;
        for (std::size_t i = 0; i < records && complete; ++i) {
            const auto &other = tracked_[i];
            if (other.stage == Stage::Free) continue;
            const auto *other_attribute = attributes(frame, other.world_slot);
            if (!other_attribute || other_attribute->group != group || !live(frame, other)) continue;
            if (other.stage != Stage::Ready || other.repair || member_count == members.size()) { complete = false; break; }
            members[member_count++] = std::uint16_t(i + 1);
        }
        if (!complete) {
            ++totals_.groups_deferred;
            for (std::size_t i = 0; i < records; ++i) {
                const auto *other_attribute = attributes(frame, tracked_[i].world_slot);
                if (tracked_[i].stage != Stage::Free && other_attribute && other_attribute->group == group) tracked_[i].served_step = frame.step;
            }
            continue;
        }
        for (std::size_t m = 0; m < member_count; ++m) {
            auto added = add_member(frame, batch, members[m]);
            if (!added) return fail_with(added.error());
            if (*added != 1) { batch = Batch{ReplicaWireKind::Publication}; message_[0] = {}; complete = false; break; }
        }
        if (!complete) { ++totals_.groups_deferred; continue; }
        if (auto status = send(); !status) return status;
        if (capture_open) ++totals_.group_publications;
    }
    if (capture_open && batch.count) if (auto status = send(); !status) return status;
    // Enrollment of not-yet-replicated visible objects.
    if (control_open && capture_open) if (auto status = enrollment(frame, control_open, capture_open, SIZE_MAX); !status) return status;
    rotation_ = records ? std::uint32_t((rotation_ + 1) % records) : 0;
    return {};
}

superpos::Result<superpos::ReplicaAuthorityProgress> LinkAuthority::pump(superpos::Tick tick) noexcept {
    if (failure_ != Error::None) return fail(failure_);
    if (!bridge_) return fail(Error::NotReady);
    auto progress = bridge_->pump(tick);
    if (!progress) { (void)fail_with(progress.error()); return fail(progress.error()); }
    ++totals_.pumps;
    totals_.sent += progress->sent; totals_.transport_applied += progress->transport_applied;
    totals_.carrier_retired += progress->carrier_retired; totals_.received += progress->received;
    totals_.discarded += progress->discarded; totals_.backpressured += progress->backpressured;
    std::array<superpos::RepairRequest, 16> requests{};
    for (;;) {
        auto taken = bridge_->take_repairs(requests);
        if (!taken) { (void)fail_with(taken.error()); return fail(taken.error()); }
        for (std::size_t r = 0; r < *taken; ++r) {
            ++totals_.repair_requests;
            const auto slot = requests[r].key.slot;
            if (!slot || slot > tracked_.size()) continue;
            auto &value = tracked_[slot - 1];
            if (value.stage != Stage::Free && value.binding.key == requests[r].key) value.repair = true;
        }
        if (*taken < requests.size()) break;
    }
    return progress;
}

LinkCounts LinkAuthority::counts() const noexcept {
    LinkCounts result;
    for (const auto &value : tracked_.span()) {
        switch (value.stage) {
        case Stage::Free: continue;
        case Stage::NeedBind: case Stage::NeedOffer: case Stage::AwaitReady: ++result.spawning; break;
        case Stage::Ready: ++result.ready; break;
        case Stage::NeedLeave: case Stage::Leaving: ++result.retiring; break;
        default: ++result.resetting; break;
        }
        ++result.tracked;
        if (value.repair) ++result.dirty;
    }
    return result;
}
}
