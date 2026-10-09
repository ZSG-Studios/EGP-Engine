// SPDX-License-Identifier: MIT
#pragma once
// Private C++23 authority-side replication owner for one link Session. Holds
// the core PeerReplicas sender store and ReplicaAuthoritySession bridge, so
// retransmission, receipts and repair requests follow core delivery. All
// storage is charged to the owning Session's allocator; the Session outlives
// this owner (it is destroyed first, with no callbacks).
#include "private/charged_array.hpp"
#include <superpos/replica_authority.hpp>
#include <superpos/registry.hpp>
#include <optional>

namespace superpos_egp::recovery {
struct AuthorityTotals {
    std::uint64_t pumps{}, gated{}, sent{}, transport_applied{}, carrier_retired{}, received{}, discarded{}, backpressured{};
};
class NativeAuthority {
public:
    static constexpr std::uint32_t control_channel = 0, state_channel = 1, bulk_channel = 2;
    explicit NativeAuthority(superpos::Allocator &) noexcept;
    NativeAuthority(const NativeAuthority &) = delete;
    NativeAuthority &operator=(const NativeAuthority &) = delete;
    superpos::Status initialize(superpos::Session &, superpos::SchemaRegistry &, superpos::ReplicaConfig,
        superpos::ReplicaWireContext, std::uint32_t records) noexcept;
    // Replaces the plain Session pump for this link. While egress is closed
    // (the restored lease is not current) nothing is sent or retransmitted.
    superpos::Result<superpos::ReplicaAuthorityProgress> pump(superpos::Tick) noexcept;
    bool raw_allowed(std::uint32_t channel) const noexcept { return channel > bulk_channel; }
    void set_egress(bool open) noexcept { egress_ = open; }
    bool egress() const noexcept { return egress_; }
    superpos::PeerReplicas &sender() noexcept { return *sender_; }
    superpos::ReplicaAuthoritySession &bridge() noexcept { return *bridge_; }
    const superpos::ReplicaWireContext &context() const noexcept { return context_; }
    const AuthorityTotals &totals() const noexcept { return totals_; }
private:
    ChargedArray<superpos::ReplicaRecord> records_;
    ChargedArray<std::byte> images_, arena_, validation_, freeze_scratch_;
    ChargedArray<superpos::AuthorityCapture> captures_;
    ChargedArray<superpos::AuthorityExposure> exposures_;
    ChargedArray<superpos::RepairRequest> repairs_;
    std::optional<superpos::FrozenRegistry> registry_;
    std::optional<superpos::PeerReplicas> sender_;
    std::optional<superpos::ReplicaAuthoritySession> bridge_;
    superpos::ReplicaWireContext context_{};
    AuthorityTotals totals_{};
    bool egress_{}, initialized_{};
};
}
