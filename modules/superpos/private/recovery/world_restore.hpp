// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 durable-recovery participant. Compiled only with the opt-in
// superpos_durable_recovery module option; never part of a generated SDK.
#include <superpos/host_restore.hpp>
#include <superpos/canonical_state.hpp>
#include <superpos/world.hpp>
#include <array>
#include <cstdint>
#include <span>

namespace superpos_egp::recovery {
// Durable restore payloads are exact core World snapshots (World::capture):
// handles, generations, revisions, ticks, free-list order and the publication
// counter survive restore. The listing below is a separate read-only view of
// live entities used for publication and fixtures; it is never journaled.
//
// Live-entity listing, version 1 (little-endian, no padding):
//   header (32 bytes): magic "EGW1", version, count, reserved=0,
//                      authority epoch, tick
//   entry  (56 bytes): slot, generation, schema, owner, ownership revision,
//                      revision, tick, state bytes, reserved=0
//   followed by exactly the schema's canonical state bytes.
// Entries are in strictly ascending slot order. The complete-state digest is
// SHA-256 over the payload, the value a producer claims as the envelope result.
inline constexpr std::uint32_t world_payload_magic = 0x31574745u;
inline constexpr std::size_t world_header_bytes = 32, world_entry_bytes = 56;

struct WorldEntity {
    std::uint32_t slot{}, generation{};
    superpos::SchemaId schema{};
    superpos::PeerId owner{};
    std::uint64_t ownership_revision{}, revision{};
    superpos::Tick tick{};
    std::span<const std::byte> state{};
};
struct WorldPayloadHeader {
    std::uint32_t count{};
    superpos::Epoch authority_epoch{};
    superpos::Tick tick{};
};

// Encodes every live slot of a quiescent World. Caller owns the slot storage.
superpos::Result<std::size_t> encode_world_payload(const superpos::World&, std::span<const superpos::WorldSlot>,
    superpos::Tick, std::span<std::byte> output) noexcept;
// Structural and semantic validation against the target's schemas and
// capacity. Never trusts length fields beyond the supplied span.
superpos::Result<WorldPayloadHeader> validate_world_payload(std::span<const std::byte>,
    std::span<const superpos::Schema>, std::uint32_t capacity) noexcept;
// Visits validated entities in slot order. Payload must already be validated.
template <class F> superpos::Status for_each_entity(std::span<const std::byte>, F &&) noexcept;
std::size_t maximum_world_payload(std::uint32_t capacity, std::size_t state_stride) noexcept;
// Upper bound of World::capture for a world of this capacity and stride.
std::size_t maximum_world_snapshot(std::uint32_t capacity, std::size_t state_stride) noexcept;

// Aggregate trusted participant for one target Session world. Every mutation
// is to private state from the supplied allocator: no scene, Session or World
// change happens until a separate, explicit apply after a successful seal.
// Each candidate snapshot is semantically validated by restoring it into a
// private scratch World with the target's schemas, capacity, stride and the
// plan's successor epoch, so core's complete World::restore checks run before
// it is staged and again at seal. Owner-thread only (the driver's thread).
class WorldParticipant final : public superpos::HostRestoreParticipant {
public:
    WorldParticipant(std::span<const superpos::Schema> schemas, std::uint32_t capacity, std::size_t state_stride,
        superpos::PeerId authority_peer, superpos::CryptographicDigest &digest, superpos::HostRestoreCapabilities capabilities) noexcept;
    ~WorldParticipant() override;
    WorldParticipant(const WorldParticipant &) = delete;
    WorldParticipant &operator=(const WorldParticipant &) = delete;
    superpos::HostRestoreCapabilities capabilities() const noexcept override { return capabilities_; }
    superpos::Status prepare(const superpos::HostRestorePlan &, superpos::Allocator &) noexcept override;
    superpos::Status checkpoint_chunk(std::uint32_t, std::span<const std::byte>) noexcept override;
    superpos::Status poststate(superpos::Epoch, std::uint64_t, superpos::Tick, std::span<const std::byte>,
        std::span<const std::byte>) noexcept override;
    superpos::Result<superpos::Fingerprint> seal() noexcept override;
    void discard() noexcept override;
    // Sealed private candidate, valid until discard. Not live state.
    superpos::Result<std::span<const std::byte>> sealed_payload() const noexcept;
    bool active() const noexcept { return buffer_ != nullptr; }
    std::uint32_t chunks() const noexcept { return chunks_; }
    std::uint32_t records() const noexcept { return records_; }
private:
    superpos::Status adopt(std::span<const std::byte> envelope, superpos::CanonicalStateKind) noexcept;
    superpos::Status validate(std::span<const std::byte> snapshot) noexcept;
    std::span<const superpos::Schema> schemas_;
    std::uint32_t capacity_{};
    std::size_t stride_{};
    superpos::PeerId authority_peer_{};
    superpos::Epoch successor_{};
    superpos::WorldSlot *scratch_slots_{};
    std::byte *scratch_arena_{};
    superpos::CryptographicDigest *digest_{};
    superpos::HostRestoreCapabilities capabilities_{};
    superpos::Allocator *allocator_{};
    std::byte *buffer_{};
    std::size_t limit_{}, size_{};
    std::uint32_t chunks_{}, records_{};
    bool sealed_{};
};

// Little-endian readers shared by the codec and callers that visit entities.
namespace detail {
inline std::uint32_t u32(const std::byte *p) noexcept {
    std::uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | std::uint32_t(p[i]); return v;
}
inline std::uint64_t u64(const std::byte *p) noexcept {
    std::uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | std::uint64_t(p[i]); return v;
}
}
template <class F> superpos::Status for_each_entity(std::span<const std::byte> payload, F &&visit) noexcept {
    if (payload.size() < world_header_bytes) return superpos::fail(superpos::Error::InvalidArgument);
    const std::uint32_t count = detail::u32(payload.data() + 8);
    std::size_t at = world_header_bytes;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (payload.size() - at < world_entry_bytes) return superpos::fail(superpos::Error::InvalidArgument);
        const std::byte *e = payload.data() + at;
        WorldEntity entity{detail::u32(e), detail::u32(e + 4), detail::u64(e + 8), detail::u64(e + 16),
            detail::u64(e + 24), detail::u64(e + 32), detail::u64(e + 40), {}};
        const std::uint32_t bytes = detail::u32(e + 48);
        at += world_entry_bytes;
        if (payload.size() - at < bytes) return superpos::fail(superpos::Error::InvalidArgument);
        entity.state = payload.subspan(at, bytes);
        at += bytes;
        if (auto result = visit(entity); !result) return result;
    }
    return {};
}
}
