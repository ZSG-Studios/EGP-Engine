// SPDX-License-Identifier: MIT
#include "world_restore.hpp"
#include <cstring>

namespace superpos_egp::recovery {
using superpos::Error;
using superpos::fail;
namespace {
void put32(std::byte *p, std::uint32_t v) noexcept { for (int i = 0; i < 4; ++i) p[i] = std::byte(v >> (8 * i)); }
void put64(std::byte *p, std::uint64_t v) noexcept { for (int i = 0; i < 8; ++i) p[i] = std::byte(v >> (8 * i)); }
const superpos::Schema *find(std::span<const superpos::Schema> schemas, superpos::SchemaId id) noexcept {
    for (const auto &schema : schemas) if (schema.id() == id) return &schema;
    return nullptr;
}
}

std::size_t maximum_world_payload(std::uint32_t capacity, std::size_t stride) noexcept {
    return world_header_bytes + std::size_t(capacity) * (world_entry_bytes + stride);
}

superpos::Result<std::size_t> encode_world_payload(const superpos::World &world, std::span<const superpos::WorldSlot> slots,
        superpos::Tick tick, std::span<std::byte> output) noexcept {
    auto epoch = world.authority_epoch(); if (!epoch) return fail(epoch.error());
    if (output.size() < world_header_bytes || slots.size() > UINT32_MAX) return fail(Error::CapacityExceeded);
    std::size_t at = world_header_bytes;
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].live) continue;
        auto view = world.view(superpos::ObjectHandle::from_parts(std::uint32_t(i + 1), slots[i].generation));
        if (!view || !view->schema) return fail(view ? Error::ProtocolViolation : view.error());
        const auto bytes = view->canonical.size();
        if (output.size() - at < world_entry_bytes || output.size() - at - world_entry_bytes < bytes || bytes > UINT32_MAX)
            return fail(Error::CapacityExceeded);
        std::byte *e = output.data() + at;
        put32(e, std::uint32_t(i + 1)); put32(e + 4, slots[i].generation);
        put64(e + 8, view->schema->id()); put64(e + 16, view->owner); put64(e + 24, view->ownership_revision);
        put64(e + 32, view->revision); put64(e + 40, view->tick); put32(e + 48, std::uint32_t(bytes)); put32(e + 52, 0);
        std::memcpy(e + world_entry_bytes, view->canonical.data(), bytes);
        at += world_entry_bytes + bytes; ++count;
    }
    put32(output.data(), world_payload_magic); put32(output.data() + 4, 1); put32(output.data() + 8, count);
    put32(output.data() + 12, 0); put64(output.data() + 16, *epoch); put64(output.data() + 24, tick);
    return at;
}

superpos::Result<WorldPayloadHeader> validate_world_payload(std::span<const std::byte> payload,
        std::span<const superpos::Schema> schemas, std::uint32_t capacity) noexcept {
    if (payload.size() < world_header_bytes || detail::u32(payload.data()) != world_payload_magic ||
            detail::u32(payload.data() + 4) != 1 || detail::u32(payload.data() + 12) != 0) return fail(Error::IncompatibleSchema);
    WorldPayloadHeader header{detail::u32(payload.data() + 8), detail::u64(payload.data() + 16), detail::u64(payload.data() + 24)};
    if (!header.authority_epoch || header.count > capacity) return fail(Error::CapacityExceeded);
    std::uint32_t previous = 0;
    std::size_t consumed = world_header_bytes;
    auto checked = for_each_entity(payload, [&](const WorldEntity &entity) -> superpos::Status {
        // Complete semantic coverage: unique ascending in-range slots, live
        // generations, registered schemas and exact canonical state widths.
        if (entity.slot <= previous || entity.slot > capacity || !entity.generation || !entity.revision) return fail(Error::InvalidArgument);
        const auto *schema = find(schemas, entity.schema);
        if (!schema || entity.state.size() != schema->state_bytes()) return fail(Error::IncompatibleSchema);
        previous = entity.slot; consumed += world_entry_bytes + entity.state.size();
        return {};
    });
    if (!checked) return fail(checked.error());
    if (consumed != payload.size()) return fail(Error::InvalidArgument);
    return header;
}

WorldParticipant::WorldParticipant(std::span<const superpos::Schema> schemas, std::uint32_t capacity, std::size_t stride,
        superpos::CryptographicDigest &digest, superpos::HostRestoreCapabilities capabilities) noexcept
    : schemas_(schemas), capacity_(capacity), stride_(stride), digest_(&digest), capabilities_(capabilities) {}

WorldParticipant::~WorldParticipant() { discard(); }

superpos::Status WorldParticipant::prepare(const superpos::HostRestorePlan &, superpos::Allocator &allocator) noexcept {
    if (buffer_) return fail(Error::Busy);
    limit_ = maximum_world_payload(capacity_, stride_);
    if (!capacity_ || !stride_ || limit_ > capabilities_.maximum_record_bytes) return fail(Error::CapacityExceeded);
    buffer_ = static_cast<std::byte *>(allocator.allocate(limit_, alignof(std::max_align_t), superpos::MemoryDomain::Recovery));
    if (!buffer_) return fail(Error::OutOfMemory);
    allocator_ = &allocator; size_ = 0; chunks_ = records_ = 0; sealed_ = false;
    return {};
}

superpos::Status WorldParticipant::adopt(std::span<const std::byte> envelope, superpos::CanonicalStateKind kind) noexcept {
    if (!buffer_ || sealed_) return fail(Error::NotReady);
    auto view = superpos::decode_canonical_state(envelope);
    if (!view || view->header.kind != kind) return fail(Error::RecoveryUnavailable);
    if (view->payload.size() > limit_) return fail(Error::CapacityExceeded);
    // Validate before replacing: a rejected image never becomes the candidate.
    auto header = validate_world_payload(view->payload, schemas_, capacity_);
    if (!header) return fail(header.error());
    std::memcpy(buffer_, view->payload.data(), view->payload.size());
    size_ = view->payload.size();
    return {};
}

superpos::Status WorldParticipant::checkpoint_chunk(std::uint32_t ordinal, std::span<const std::byte> content) noexcept {
    // The checkpoint holds one complete canonical envelope in its first chunk.
    if (ordinal != 0 || chunks_) return fail(Error::RecoveryUnavailable);
    if (auto adopted = adopt(content, superpos::CanonicalStateKind::Checkpoint); !adopted) return adopted;
    ++chunks_; return {};
}

superpos::Status WorldParticipant::poststate(superpos::Epoch, std::uint64_t, superpos::Tick, std::span<const std::byte> record,
        std::span<const std::byte>) noexcept {
    // Version 1 world records are complete replacements. Deltas need a
    // separately qualified codec and are refused rather than guessed.
    auto view = superpos::decode_canonical_state(record);
    if (!view) return fail(Error::RecoveryUnavailable);
    if (view->header.kind == superpos::CanonicalStateKind::DeltaRecord) return fail(Error::Unsupported);
    if (!chunks_) return fail(Error::RecoveryUnavailable);
    if (auto adopted = adopt(record, superpos::CanonicalStateKind::FullRecord); !adopted) return adopted;
    ++records_; return {};
}

superpos::Result<superpos::Fingerprint> WorldParticipant::seal() noexcept {
    if (!buffer_ || sealed_ || !chunks_) return fail(Error::NotReady);
    const std::span<const std::byte> payload(buffer_, size_);
    if (auto checked = validate_world_payload(payload, schemas_, capacity_); !checked) return fail(checked.error());
    superpos::Fingerprint result{};
    if (auto hashed = digest_->hash(payload, result); !hashed) return fail(hashed.error());
    sealed_ = true;
    return result;
}

void WorldParticipant::discard() noexcept {
    if (buffer_ && allocator_) allocator_->deallocate(buffer_);
    buffer_ = nullptr; allocator_ = nullptr; size_ = limit_ = 0; sealed_ = false;
}

superpos::Result<std::span<const std::byte>> WorldParticipant::sealed_payload() const noexcept {
    if (!buffer_ || !sealed_) return fail(Error::NotReady);
    return std::span<const std::byte>(buffer_, size_);
}
}
