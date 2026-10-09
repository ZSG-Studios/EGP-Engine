// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/lease.hpp"
#include "superpos/sqlite_journal.hpp"
#include "superpos/service/control_wire.hpp"
#include <optional>

namespace superpos::service::admission { struct Grant; }
namespace superpos::service::control {
// Trusted native management input, never decoded from a control request. The
// principal credential generation is independent of durable-operation epochs.
struct PrincipalPolicy {
    std::uint64_t match{},session{},principal{},principal_epoch{};
    Epoch authority_epoch{};
    PeerId authority_owner{}; // Zero is a valid owner, not absence.
    AuthorityKind authority_kind{};
    std::uint64_t membership_generation{},permissions{};
    bool revoked{};
    bool operator==(const PrincipalPolicy&) const noexcept=default;
};
struct PolicyChange {
    std::uint64_t transition_id{}; // Stable nonzero native-management operation.
    std::optional<std::uint64_t> expected_revision{};
    PrincipalPolicy policy{};
    CoordinatorBootIdentity boot{};
    bool operator==(const PolicyChange&) const noexcept=default;
};
struct PolicyReceipt {
    PolicyChange change{};
    std::uint64_t revision{};
    bool duplicate{};
    // An old exact transition can reconfirm without replacing newer policy.
    bool current{true};
};
struct IssuerIdentityFields {
    CoordinatorBootIdentity boot{};
    LeaseScope clock_scope{};
    bool operator==(const IssuerIdentityFields&) const noexcept=default;
};
class PermitIssuer;
// These types have no public constructor or wire decoder. Ordinary network
// metadata cannot manufacture a storage permit or an execution-clock binding.
class IssuerIdentity {
    friend class PermitIssuer;
    IssuerIdentityFields fields_{};
    explicit IssuerIdentity(IssuerIdentityFields fields) noexcept:fields_(fields){}
public:
    IssuerIdentity(const IssuerIdentity&) noexcept=default;
    IssuerIdentity& operator=(const IssuerIdentity&) noexcept=default;
    const IssuerIdentityFields& fields() const noexcept{return fields_;}
};
struct ExecutionLeaseFields {
    IssuerIdentityFields issuer{};
    AuthorityGrant grant{};
    ClockSample anchor{};
    std::uint64_t deadline_us{};
};
class ExecutionLease {
    friend class PermitIssuer;
    ExecutionLeaseFields fields_{};
    explicit ExecutionLease(ExecutionLeaseFields fields) noexcept:fields_(fields){}
public:
    ExecutionLease(const ExecutionLease&) noexcept=default;
    ExecutionLease& operator=(const ExecutionLease&) noexcept=default;
    const ExecutionLeaseFields& fields() const noexcept{return fields_;}
};
struct ExecutionPermitFields {
    ExecutionLeaseFields lease{};
    PrincipalPolicy policy{};
    std::uint64_t policy_revision{},connection_incarnation{},permission{},deadline_us{};
};
class ExecutionPermit {
    friend class PermitIssuer;
    ExecutionPermitFields fields_{};
    explicit ExecutionPermit(ExecutionPermitFields fields) noexcept:fields_(fields){}
public:
    ExecutionPermit(const ExecutionPermit&) noexcept=default;
    ExecutionPermit& operator=(const ExecutionPermit&) noexcept=default;
    const ExecutionPermitFields& fields() const noexcept{return fields_;}
};
// This owner-only issuer borrows the same *native* PlatformClock used by its
// active AuthorityLease. The core lease friend declaration verifies
// source and the exact current grant without exposing grant mutation publicly.
class PermitIssuer {
    AuthorityLease& lease_;
    PlatformClock& clock_;
    std::thread::id owner_{std::this_thread::get_id()};
    std::optional<IssuerIdentity> identity_{};
    bool busy_{};
    Status check() const noexcept;
    Result<LeasePosition> current(const AuthorityGrant&) noexcept;
public:
    PermitIssuer(AuthorityLease&,PlatformClock&) noexcept;
    PermitIssuer(const PermitIssuer&)=delete;
    PermitIssuer& operator=(const PermitIssuer&)=delete;
    Status initialize(CoordinatorBootIdentity,LeaseNonceProvider&) noexcept;
    Result<IssuerIdentity> identity() const noexcept;
    Status matches_clock(const ClockSource&) const noexcept;
    Result<ExecutionLease> execution_lease(const DurableAuthorityReceipt&) noexcept;
    // Completion of install_execution_lease is required before queue dispatch.
    // The store independently compares this exact lease to its committed row.
    Result<ExecutionPermit> issue(const admission::Grant&,const PolicyReceipt&,
        const ExecutionLease&,Operation,std::uint64_t admission_deadline_us) noexcept;
};
bool same_grant(const AuthorityGrant&,const AuthorityGrant&) noexcept;
bool same_execution_lease(const ExecutionLeaseFields&,const ExecutionLeaseFields&) noexcept;
}
