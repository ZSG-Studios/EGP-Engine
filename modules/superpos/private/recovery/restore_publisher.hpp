// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 fan-out of restored authority state to replica receivers,
// through one core ReplicaAuthoritySession per client link. Core delivery owns
// retransmission and receipts; repair requests are answered with FullRepair.
#include "recovery_access.hpp"
#include <superpos/lease.hpp>
#include <superpos/replica_wire.hpp>
#include "core/object/object_id.h"
#include <array>

class SuperposSession;

namespace superpos_egp::recovery {
struct LinkRoute {
    // Wire context of that client's admitted link. authority must equal the
    // restored grant epoch; connection must equal the link Session's epoch.
    superpos::ReplicaWireContext context{};
    superpos::PeerId client{};
    std::uint32_t maximum_active{64}, maximum_transitions{16};
};
struct LinkStatus {
    bool attached{}, egress{};
    std::uint32_t entities{}, queued{}, ready{}, repairs_answered{}, repairs_deferred{};
    AuthorityTotals totals{};
};
struct FanoutProgress {
    std::uint32_t links{}, attached{}, converged{}, ready{}, waiting{};
};
// Typed outcomes. PermissionDenied/Timeout/NotReady/StaleEpoch come from the
// restored-lease gate: every link's egress is closed and nothing is queued or
// retransmitted until a later step authorizes again. Link failures carry the
// core error; other links continue. Owner thread only.
class RestoreFanout {
public:
    static constexpr std::size_t maximum_links = 8, maximum_entities = 64;
    superpos::Result<std::uint32_t> add_link(SuperposSession &link, const LinkRoute &) noexcept;
    // Call once per frame before Sessions advance.
    superpos::Result<FanoutProgress> step(SuperposSession &world, superpos::AuthorityLease &,
        const superpos::AuthorityGrant &, std::uint64_t now_milliseconds) noexcept;
    superpos::Result<LinkStatus> status(std::uint32_t link) const noexcept;
    // Closes egress on every attached link (lease lost or shutdown).
    void close_egress() noexcept;
private:
    struct Link {
        ObjectID session{};
        LinkRoute route{};
        bool attached{}, started{};
        std::uint32_t entities{}, cursor{}, ready{}, repairs_answered{}, repairs_deferred{};
        std::array<superpos::ReplicaBinding, maximum_entities> bindings{};
        superpos::Error error{};
    };
    superpos::Status advance(Link &, SuperposSession &, std::span<const std::byte> payload, std::uint64_t now) noexcept;
    std::array<Link, maximum_links> links_{};
    std::uint32_t count_{};
};
}
