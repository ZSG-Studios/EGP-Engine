#pragma once
#include "superpos/platform_clock.hpp"
#include "superpos/types.hpp"
#include <optional>
#include <array>
#include <span>

namespace superpos {
namespace service::control { class PermitIssuer; }
enum class AuthorityKind : std::uint8_t { Player, Dedicated };
using LeaseScope = std::array<std::byte,32>;
// Trusted provider: use a qualified CSPRNG to create a new independent 256-bit
// nonce on EVERY call, including object recreation and process/host restart.
// This interface provides no default or process-counter fallback. A provider
// failure is returned to the caller; an all-zero scope is rejected.
class LeaseNonceProvider {
public:
    virtual ~LeaseNonceProvider()=default;
    virtual Status fresh(std::span<std::byte,32>) noexcept=0;
};
struct AuthorityGrant {
    std::uint64_t request{},coordinator_term{},grant_sequence{},membership_generation{};
    Epoch epoch{};
    PeerId owner{};
    AuthorityKind kind{};
    std::uint64_t ttl_us{2000000};
    LeaseScope scope{};
    std::uint64_t proof_generation{};
};
struct LeaseRequest {
    std::uint64_t request{},proof_generation{};
    LeaseScope scope{};
};
enum class LeasePhase { Unavailable, AwaitingRestore, Active, Failed };
struct LeasePosition {
    std::uint64_t coordinator_term{},membership_generation{};
    Epoch epoch{};
    PeerId owner{};
    AuthorityKind kind{};
    std::uint64_t remaining_us{};
};
struct LeaseConfig {
    std::uint64_t maximum_ttl_us{2000000};
    std::uint32_t relative_rate_ppm{1000};
};
// Owner-thread enforcement for an already authenticated coordinator protocol.
// The guard must be independently revalidated with calibrated remote uncertainty.
// This class neither authenticates grants nor persists coordinator boot terms.
// The borrowed source and guard must outlive this lease and all its calls.
// Authority epochs start at one, matching the v1 replication protocol. Zero is
// valid for coordinator terms, grant/membership/request counters and peer IDs.
// It anchors expiry at request transmission, not delayed reply receipt, and
// charges growing uncertainty before permitting protected authority work.
class AuthorityLease {
    friend class service::control::PermitIssuer;
    class ValidatedScope {
        friend class AuthorityLease;
        explicit ValidatedScope(LeaseScope scope) noexcept:scope_(scope) {}
        LeaseScope scope_{};
    public:
        ValidatedScope(const ValidatedScope&) noexcept=default;
    };
public:
    static Result<AuthorityLease> create(ClockSource&,ContinuityGuard&,LeaseNonceProvider&,LeaseConfig={}) noexcept;
    // The private validated type is constructed only by create; it permits
    // allocation-free expected in-place construction of this immovable gate.
    AuthorityLease(ClockSource&,ContinuityGuard&,LeaseConfig,ValidatedScope) noexcept;
    AuthorityLease(const AuthorityLease&)=delete;
    AuthorityLease& operator=(const AuthorityLease&)=delete;
    AuthorityLease(AuthorityLease&&)=delete;
    AuthorityLease& operator=(AuthorityLease&&)=delete;
    Result<LeaseRequest> begin_request() noexcept;
    // Trusted service boundary: only a verified coordinator may call this.
    Status authenticated_grant(const AuthorityGrant&) noexcept;
    // Trusted recovery boundary: the exact authority/membership has completed
    // its qualified restore or first-authority initialization, without callbacks.
    Status restored(std::uint64_t term,Epoch,std::uint64_t membership,PeerId,AuthorityKind) noexcept;
    // A point-in-time check. Never cache this result as permission for later
    // asynchronous work; publication barriers and durable fencing recheck it.
    Result<LeasePosition> authorize(PeerId,AuthorityKind,Epoch,std::uint64_t membership) noexcept;
    Status revoke() noexcept;
    // Diagnostic last-observed phase; it does not sample time or authorize work.
    Result<LeasePhase> phase() const noexcept;
    // Callback-free creation identity, stable across requests and renewals. It
    // is not a current grant, admission, continuity or permission proof.
    Result<LeaseScope> scope_identity() const noexcept;
private:
    Result<ClockObservation> observe() noexcept;
    Status owner() const noexcept;
    Status invalidate(bool terminal=false) noexcept;
    Result<std::uint64_t> remaining(const ClockObservation&) noexcept;
    ClockSource* source_{};
    ContinuityGuard* continuity_{};
    LeaseConfig config_{};
    const LeaseScope scope_;
    std::thread::id owner_{};
    AuthorityGrant grant_{};
    ClockObservation requested_{};
    std::optional<std::uint64_t> request_{};
    std::uint64_t next_request_{},deadline_{},last_term_{},last_epoch_{},last_grant_{};
    Epoch expired_epoch_{};
    bool have_grant_{},have_deadline_{},have_expired_epoch_{},request_exhausted_{};
    LeasePhase phase_{LeasePhase::Unavailable};
};
}
