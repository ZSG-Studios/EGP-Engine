// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 bridge between a SuperposSession and durable recovery. Not a
// ClassDB or SDK surface; capabilities are native opaque core objects.
#include "world_restore.hpp"
#include "native_authority.hpp"
#include <superpos/crypto_digest.hpp>

class SuperposSession;

namespace superpos_egp::recovery {
// Read-only description of a configured target Session for its participant.
struct SessionTarget {
    std::span<const superpos::Schema> schemas{};
    std::uint32_t capacity{};
    std::size_t state_stride{};
    superpos::Epoch authority_epoch{};
    superpos::PeerId authority_peer{};
    superpos::Fingerprint schemas_fingerprint{}, simulation_fingerprint{};
    superpos::CryptographicDigest *digest{};
};
// Typed apply outcome. World::restore reproduces exact handles, generations,
// revisions, ticks, free-list order and the publication counter, so
// handles_preserved is always true for a successful apply.
struct ApplyResult {
    std::uint32_t objects{};
    bool handles_preserved{};
    superpos::Tick tick{};
};
}

struct SuperposRecoveryAccess {
    static superpos::Result<superpos_egp::recovery::SessionTarget> target(SuperposSession &) noexcept;
    // Read-only live-entity listing of the Session's world (publication view).
    static superpos::Result<std::size_t> capture(SuperposSession &, std::span<std::byte>) noexcept;
    // Exact core World snapshot (World::capture), the durable restore payload.
    static superpos::Result<std::size_t> snapshot_bytes(SuperposSession &) noexcept;
    static superpos::Result<std::size_t> snapshot(SuperposSession &, std::span<std::byte>) noexcept;
    static superpos::Result<std::size_t> live_count(SuperposSession &) noexcept;
    // Applies a sealed snapshot with World::restore to a Session whose world is
    // pristine (never mutated since configure) and already uses the restore's
    // successor authority epoch. Core restore is all-or-nothing: a refused
    // apply leaves the Session's world unchanged.
    static superpos::Result<superpos_egp::recovery::ApplyResult> apply(SuperposSession &, std::span<const std::byte> snapshot,
        superpos::Epoch successor) noexcept;
    // Authority-side replication on a network-ready link Session. The link
    // reserves Control/State/Bulk for the core ReplicaAuthoritySession and its
    // Session pump is replaced by the bridge pump. Not for RTC links.
    static superpos::Status attach_authority(SuperposSession &link, superpos::ReplicaConfig, superpos::ReplicaWireContext) noexcept;
    // Borrowed owner-thread pointer, valid only until the next Session call
    // that can close, retire or detach the link. Never retain it across frames.
    static superpos_egp::recovery::NativeAuthority *authority(SuperposSession &link) noexcept;
    static superpos::Status detach_authority(SuperposSession &link) noexcept;
    // Exact core error behind the Session's last network pump failure, or None.
    static superpos::Error network_failure(SuperposSession &) noexcept;
private:
    static superpos::Status available(const SuperposSession &) noexcept;
};
