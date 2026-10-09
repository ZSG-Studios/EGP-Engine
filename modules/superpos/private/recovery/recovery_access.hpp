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
    superpos::Fingerprint schemas_fingerprint{}, simulation_fingerprint{};
    superpos::CryptographicDigest *digest{};
};
struct HandleMapping { std::uint64_t original{}, restored{}; };
// Typed apply outcome. handles_preserved is true only when every restored
// handle equals its sealed handle; otherwise callers use the mapping.
struct ApplyResult {
    std::uint32_t objects{};
    bool handles_preserved{};
    superpos::Tick tick{};
};
}

struct SuperposRecoveryAccess {
    static superpos::Result<superpos_egp::recovery::SessionTarget> target(SuperposSession &) noexcept;
    // Canonical payload of the Session's current world (quiescent owner thread).
    static superpos::Result<std::size_t> capture(SuperposSession &, std::span<std::byte>) noexcept;
    static superpos::Result<std::size_t> live_count(SuperposSession &) noexcept;
    // Applies a sealed, validated payload to an empty Session whose world
    // already uses the restore's successor authority epoch. Complete preflight
    // precedes the first spawn; any later failure removes what it spawned, so
    // a refused apply leaves the Session's live state unchanged.
    static superpos::Result<superpos_egp::recovery::ApplyResult> apply(SuperposSession &, std::span<const std::byte> payload,
        superpos::Epoch successor, std::span<superpos_egp::recovery::HandleMapping>) noexcept;
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
