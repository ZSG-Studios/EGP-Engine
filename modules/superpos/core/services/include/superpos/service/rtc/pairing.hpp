// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/allocator.hpp"
#include "superpos/capability.hpp"
#include "superpos/lease.hpp"
#include "superpos/transport.hpp"
#include "superpos/service/admission_types.hpp"
#include <thread>

namespace superpos::service::tls { class CryptoBorrow; }

namespace superpos::service::pairing {
inline constexpr std::size_t pending_capacity = 32;
inline constexpr std::size_t active_capacity = 8;
inline constexpr std::size_t connected_capacity = 256;
inline constexpr std::uint64_t deadline_us = 15000000;
inline constexpr std::string_view transcript_domain = "superpos-webrtc-pair-v1";
struct Token {
    std::uint64_t registry{}, generation{};
    bool operator==(const Token&) const = default;
};
struct Scope {
    admission::Grant principal{};
    PeerId peer{}; // Independently provisioned principal -> native peer mapping.
};
struct Certificates {
    Fingerprint local_control{}, remote_control{}, local_state{}, remote_state{};
    bool operator==(const Certificates&) const = default;
};
struct Ticket {
    Token pair{};
    CarrierLane role{CarrierLane::Single};
    std::array<std::byte,32> nonce{};
};
struct Offer {
    Token pair{};
    Scope scope{};
    Certificates certificates{};
    std::array<std::byte,32> nonce{};
    std::uint64_t issued_us{}, expires_us{}, continuity_generation{};
    admission::Request request{}; // Sign with existing admission::sign.
    Ticket control{}, state{};
};
// Native provider receipt: certificate digests read from the actual verified
// DTLS association, never client JSON/SDP assertions. Identity is a nonzero,
// nonrecycled native association identity; it must differ between both roles.
struct ObservedAssociation {
    CarrierLane role{CarrierLane::Single};
    std::uint64_t identity{};
    Fingerprint local{}, remote{};
};
class OwnershipToken {
    Token token_{};
    explicit OwnershipToken(Token t) noexcept:token_(t){}
    friend class Registry;
public:
    OwnershipToken()=default;
    bool operator==(const OwnershipToken&) const = default;
};
struct Binding {
    Offer offer{};
    std::uint64_t control_association{}, state_association{};
    OwnershipToken ownership{};
};
struct Started {
    Token pair{};
    std::uint64_t control_association{},state_association{};
};
struct Counts { std::size_t pending{}, active{}, connected{}, bytes{}; };
// Native owner adapter. create runs only AFTER a handshake credit is reserved;
// success transfers one new, unique association identity into Registry custody.
// Failure leaves no native resource or retained reference. The copied offer is
// temporary. close_and_quiesce is idempotent: success means actual close and all
// callbacks drained; Busy/error retains custody and must be retried. It must not
// recycle a claimed identity. No wire endpoint may implement this interface.
class NativeAssociations {
public:
    virtual ~NativeAssociations()=default;
    virtual Result<std::uint64_t> create(const Offer&,CarrierLane) noexcept=0;
    virtual Status close_and_quiesce(std::uint64_t) noexcept=0;
};
// Trusted current policy, not a wire callback. It checks the independently
// provisioned peer mapping, principal epoch/revocation and current authority.
// It cannot retain arguments; all borrowed providers outlive Registry.
class CurrentAuthorization {
public:
    virtual ~CurrentAuthorization() = default;
    virtual Status check(const Scope&) noexcept = 0;
};
class ProofVerifier {
public:
    virtual ~ProofVerifier() = default;
    virtual Result<admission::Grant> verify(const admission::Request&,
                                           const admission::Challenge&) noexcept = 0;
};
class PsaProofVerifier final : public ProofVerifier {
    tls::CryptoBorrow& borrow_;
    admission::CredentialProvider& credentials_;
public:
    PsaProofVerifier(tls::CryptoBorrow& b, admission::CredentialProvider& p) noexcept : borrow_(b), credentials_(p) {}
    Result<admission::Grant> verify(const admission::Request&,const admission::Challenge&) noexcept override;
};
// One fallible Backend allocation, at most 256 KiB excluding allocator metadata.
// No allocation after initialize; 32 pending logical pair tickets (each carries
// both role proofs), 8 active handshakes and 256 retained connected pair claims.
// All operations/destruction on creating thread; reentry is Busy. It never
// revalidates the borrowed continuity guard or extends the original deadline.
// Finish fallible cancel/release/sweep before destruction. Destruction aborts
// if any remaining native close cannot immediately prove callback quiescence;
// it is not an asynchronous shutdown or an alternate way to drop custody.
class Registry {
    struct Pool;
    Allocator& allocator_;
    ClockSource& clock_;
    ContinuityGuard& continuity_;
    LeaseNonceProvider& nonces_;
    CryptographicDigest& digest_;
    ProofVerifier& verifier_;
    CurrentAuthorization& authorization_;
    NativeAssociations& native_;
    const std::thread::id owner_{std::this_thread::get_id()};
    Pool* pool_{};
    std::uint64_t incarnation_{};
    std::atomic<std::uint64_t> next_{1};
    bool busy_{}, retired_{};
    Status own() const noexcept;
    Result<ClockObservation> observe() noexcept;
    void remove(std::size_t) noexcept;
    Status retire(std::size_t) noexcept;
    bool claimed(std::uint64_t) const noexcept;
    Result<std::size_t> find(Token) const noexcept;
public:
    Registry(Allocator& a,ClockSource& c,ContinuityGuard& g,LeaseNonceProvider& n,
             CryptographicDigest& d,ProofVerifier& v,CurrentAuthorization& p,NativeAssociations& owner) noexcept
        :allocator_(a),clock_(c),continuity_(g),nonces_(n),digest_(d),verifier_(v),authorization_(p),native_(owner){}
    ~Registry();
    Registry(const Registry&)=delete; Registry& operator=(const Registry&)=delete;
    Registry(Registry&&)=delete; Registry& operator=(Registry&&)=delete;
    Status initialize() noexcept;
    // Scope/certificates originate in trusted signaling admission. This creates
    // no ready connection. The challenge nonce commits the entire pair transcript.
    Result<Offer> issue(const Scope&,const Certificates&) noexcept;
    // Reserves one of eight active credits before invoking either native create.
    // Merely issuing a pending ticket never authorizes native resource creation.
    Result<Started> begin(Token) noexcept;
    Status admit(const Ticket&,const admission::Request&,const ObservedAssociation&) noexcept;
    // Single transfer only after both roles. Reserves one of 256 connected claims
    // before releasing pending/active credit. Full capacity leaves the ticket intact.
    // The native objects remain in Registry custody until release; the consumer
    // borrows them while holding the returned token. Binding establishes pairing
    // identity only, never future operation authority.
    Result<Binding> take(Token) noexcept;
    // Ledger-only lookup; it grants no future operation authority.
    Result<Binding> binding(OwnershipToken) const noexcept;
    // Explicit operation admission: current authorization and the original
    // independent continuity generation must still hold. Non-Busy rejection
    // marks this claim closing while preserving custody for release(). Offer
    // deadlines cover establishment; they do not extend or limit a live claim.
    Result<Binding> checked_binding(OwnershipToken) noexcept;
    // Claims remain reserved until native close+callback quiescence is proved.
    Status release(OwnershipToken) noexcept;
    Status cancel(Token) noexcept;
    Status sweep() noexcept;
    Result<Counts> counts() const noexcept;
};
}
