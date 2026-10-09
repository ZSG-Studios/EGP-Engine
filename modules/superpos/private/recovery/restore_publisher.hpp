// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 publication of restored authority state to replica receivers.
#include "recovery_access.hpp"
#include <superpos/lease.hpp>
#include <superpos/replica_wire.hpp>

class SuperposSession;

namespace superpos_egp::recovery {
struct PublishRoute {
    // Wire context of the receiving peer. authority must equal the restored
    // grant epoch; connection/replica come from that peer's admitted Session.
    superpos::ReplicaWireContext context{};
    std::uint64_t incarnation{1}, encoding_epoch{1};
    std::uint32_t control_channel{0}, bulk_channel{2};
};
struct PublishResult {
    std::uint32_t binds{}, baselines{};
    std::uint64_t first_sequence{}, last_sequence{};
};
// Typed outcomes. PermissionDenied/Timeout/NotReady come from the lease gate
// and mean nothing was sent. StaleEpoch: the route or Session does not carry
// the restored grant epoch. Partial transport acceptance is reported through
// the Result error with no claim of delivery; Received/Applied are separate.
class RestorePublisher {
public:
    // Authorizes against the restored lease immediately before every send
    // batch, then queues one Bind on the ordered Control route and one
    // BaselineOffer per restored entity. Lifecycle sequences start after the
    // previous publication on this publisher.
    superpos::Result<PublishResult> publish(SuperposSession &authority, superpos::AuthorityLease &lease,
        const superpos::AuthorityGrant &grant, const PublishRoute &) noexcept;
    // Drains and acknowledges replies on the Control route, counting distinct
    // SpawnApplied receipts for keys this publisher bound under the route.
    superpos::Result<std::uint32_t> collect(SuperposSession &authority, const PublishRoute &) noexcept;
    std::uint32_t applied() const noexcept { return applied_; }
    std::uint32_t published() const noexcept { return published_; }
private:
    std::uint64_t sequence_{};
    std::uint32_t published_{}, applied_{};
    std::array<bool, 1024> applied_slots_{};
};
}
