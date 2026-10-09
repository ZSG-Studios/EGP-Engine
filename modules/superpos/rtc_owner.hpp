// SPDX-License-Identifier: MIT
// PRIVATE C++23 lifecycle prototype; never include in generated SDK headers.
#pragma once
#include "rtc_crypto.hpp"
#include "superpos/service/rtc/paired_transport.hpp"
#include "superpos/session.hpp"
#include <superpos/allocated_owner.hpp>
#include <optional>

namespace superpos_egp {
namespace pairing = superpos::service::pairing;
struct SessionBinding {
    superpos::PeerId local_peer{};
    superpos::Epoch connection_epoch{};
    bool operator==(const SessionBinding&) const = default;
};
// Trusted provisioned mapping, never created from remote SDP or script values.
// The admission protocol does not define the local peer or map its authority
// epoch/connection incarnation to the Session HELLO connection epoch.
class AttachmentPolicy : public pairing::CurrentAuthorization {
public:
    // Callback-free owner-thread observation. Monotonic, never wraps; zero is
    // valid. Missing readiness is a typed error, never a numeric sentinel.
    // Every change affecting check() or session_binding() advances this value
    // before publication. NotReady/CounterExhausted closes admission. A getter
    // that executes user code or mutates policy cannot satisfy this contract.
    virtual superpos::Result<std::uint64_t> policy_revision() const noexcept = 0;
    // Must bind this exact authenticated scope to the endpoint's local peer and
    // independently issued fresh connection epoch; Unsupported if unavailable.
    // Zero is a valid server peer. A Session connection epoch must be nonzero.
    virtual superpos::Result<SessionBinding> session_binding(const pairing::Scope&) noexcept = 0;
};
// The application creates exactly one of these on the engine main thread.
// It owns providers (which must not borrow a Session), and owns all backing
// allocations independently of RefCounted/GDScript/Mono Session wrappers.
class RtcOwner final {
    enum class State { Fresh, Running, Stopping, Stopped };
    struct Pending {
        pairing::Token token{};
        pairing::Offer offer{};
        pairing::Started associations{};
        bool occupied{}, closing{}, started{};
    };
    struct Connection {
        superpos::QuotaAllocator allocator;
        std::optional<superpos::Session> session;
        std::optional<pairing::PairedTransport> carrier;
        pairing::OwnershipToken claim{};
        pairing::Scope scope{};
        SessionBinding binding{};
        std::uint64_t continuity_generation{};
        std::uint64_t generation{};
        bool occupied{}, closing{};
    };
    const std::thread::id thread_ = std::this_thread::get_id();
    State state_ = State::Fresh;
    bool busy_{};
    std::uint64_t generation_{};
    std::size_t cursor_{};
    superpos::BudgetAllocator& allocator_;
    const superpos::MemoryPlan peer_plan_;
    superpos::AllocatedOwner<superpos::service::admission::CredentialProvider> credentials_;
    superpos::AllocatedOwner<AttachmentPolicy> authorization_;
    BorrowedPsaCrypto crypto_;
    std::optional<superpos::PlatformClock> clock_;
    superpos::ContinuityGuard continuity_;
    std::optional<pairing::NativeAdapter> native_;
    std::optional<pairing::Registry> registry_;
    std::array<Pending, pairing::pending_capacity> pending_{};
    std::array<Connection, pairing::connected_capacity> connections_{};
    superpos::Status own() const noexcept;
    bool empty() const noexcept;
    void detach(Connection&) noexcept;
    superpos::Status validate_connection(Connection&) noexcept;
    Pending* pending(pairing::Token) noexcept;
    superpos::Result<std::uint64_t> association(pairing::Token,superpos::CarrierLane) noexcept;
public:
    // Scope-bound private borrow. Reentrant retirement/poll/shutdown returns Busy
    // until this lease dies. No Session pointer may escape the lease lifetime.
    class SessionLease {
        friend class RtcOwner;
        RtcOwner* owner_{};
        superpos::Session* session_{};
        SessionLease(RtcOwner& owner,superpos::Session& session) noexcept : owner_(&owner),session_(&session) {}
    public:
        SessionLease(const SessionLease&)=delete;
        SessionLease& operator=(const SessionLease&)=delete;
        SessionLease(SessionLease&& other) noexcept : owner_(other.owner_),session_(other.session_) { other.owner_=nullptr;other.session_=nullptr; }
        ~SessionLease();
        superpos::Session* operator->() const noexcept { return session_; }
    };
    struct Handle { std::size_t index{}; std::uint64_t generation{}; };
    struct Diagnostics {
        // host_bytes is the aggregate shared-parent total, including row quotas and
        // process fixed backing. session_bytes is a peer subset; never add it again.
        std::size_t pending{}, claims{}, closing_claims{}, sessions{}, native_live{}, host_bytes{}, session_bytes{};
    };
    // Both providers must be nonnull and own their persistent policy data.
    RtcOwner(superpos::BudgetAllocator&, superpos::AllocatedOwner<superpos::service::admission::CredentialProvider>,
             superpos::AllocatedOwner<AttachmentPolicy>, superpos::MemoryPlan peer_plan=superpos::MemoryPlan::client()) noexcept;
    ~RtcOwner();
    RtcOwner(const RtcOwner&)=delete; RtcOwner& operator=(const RtcOwner&)=delete;
    superpos::Status initialize(pairing::NativeIceConfig={}) noexcept;
    // Only trusted signaling may call this with independently calibrated data.
    superpos::Status revalidate(std::uint64_t generation, std::uint64_t uncertainty_us) noexcept;
    superpos::Result<pairing::Offer> issue(const pairing::Scope&,const pairing::Certificates&) noexcept;
    superpos::Result<superpos::Fingerprint> local_fingerprint() noexcept;
    superpos::Status start(pairing::Token,pairing::Negotiation) noexcept;
    superpos::Status submit_remote(pairing::Token,superpos::CarrierLane,pairing::Negotiation,std::span<const char>) noexcept;
    superpos::Result<pairing::DescriptionText> local_description(pairing::Token,superpos::CarrierLane) noexcept;
    superpos::Result<pairing::NativeInfo> native_info(pairing::Token,superpos::CarrierLane) noexcept;
    // Reads actual verified native certificate identity itself. Callers provide
    // the signed fixed-size admission Request, never an ObservedAssociation.
    superpos::Status admit(pairing::Token,superpos::CarrierLane,const superpos::service::admission::Request&) noexcept;
    superpos::Result<superpos::SessionConfig> prepare_session(pairing::Token,superpos::SessionConfig) noexcept;
    superpos::Result<Handle> attach(pairing::Token,const superpos::SessionConfig&) noexcept;
    superpos::Status pump_session(Handle,superpos::Tick) noexcept;
    superpos::Result<SessionLease> borrow_session(Handle) noexcept;
    superpos::Result<bool> session_ready(Handle) noexcept;
    superpos::Result<Diagnostics> diagnostics() noexcept;
    superpos::Status retire(Handle) noexcept;
    superpos::Status cancel(pairing::Token) noexcept;
    superpos::Status pump() noexcept;
    superpos::Status begin_stop() noexcept;
    // One bounded step. Busy retains this entire object and every dependency.
    superpos::Status stop_step(std::chrono::steady_clock::time_point deadline) noexcept;
};
}
