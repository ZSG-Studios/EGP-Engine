#pragma once
#include "types.hpp"
#include <algorithm>
#include <array>

namespace superpos {
using Fingerprint=std::array<std::byte,32>;
enum class DigestAlgorithm : std::uint8_t { Sha256=1 };
// Providers implement a qualified cryptographic digest. The engine-independent
// core contains no substitute hash, crypto runtime or backend allocation.
class CryptographicDigest {
public:
    virtual ~CryptographicDigest()=default;
    virtual DigestAlgorithm algorithm() const noexcept=0;
    virtual Status hash(std::span<const std::byte>,std::span<std::byte,32>) noexcept=0;
};
constexpr bool known_fingerprint(const Fingerprint& value) noexcept {
    for(auto b:value)if(b!=std::byte{})return true; return false;
}
enum class Capability : std::uint64_t {
    None=0, DeterministicReplay=1, Prediction=2, Interpolation=4,
    CanonicalHistory=8, PredictedSpawns=16, PortableRestore=32, CompletePostState=64,
};
constexpr Capability operator|(Capability a,Capability b) noexcept { return static_cast<Capability>(std::uint64_t(a)|std::uint64_t(b)); }
constexpr bool has_capability(Capability set,Capability flag) noexcept { return (std::uint64_t(set)&std::uint64_t(flag))==std::uint64_t(flag); }
// LocalRestart is bound to one qualified backend/ABI. PortableRestart restores a
// portable checkpoint but may rebuild transient simulation state. PortableExact
// declares complete canonical post-state recovery, including hidden state and
// nondeterminism; it does not require deterministic input-only physics replay.
enum class RecoveryGrade : std::uint8_t { None,LocalRestart,PortableRestart,PortableExact };
struct CapabilityManifest {
    std::uint64_t format_version{1},protocol_version{1};
    DigestAlgorithm digest{DigestAlgorithm::Sha256};
    Fingerprint schemas{},simulation{};
    std::uint32_t maximum_encoded_bytes{65536},maximum_decoded_bytes{65536};
    std::uint32_t history_ticks{120};
    Capability capabilities{}; RecoveryGrade recovery{};
};
struct AdmissionRequirements {
    std::uint64_t protocol_version{1};
    Fingerprint schemas{},simulation{};
    std::uint32_t encoded_bytes{65536},decoded_bytes{65536},history_ticks{};
    Capability capabilities{}; RecoveryGrade recovery{};
    bool successor{},state_transfer_authorized{};
};
struct AdmittedCapabilities {
    std::uint32_t maximum_encoded_bytes{},maximum_decoded_bytes{},history_ticks{};
    Capability common{}; RecoveryGrade recovery{};
};
inline Status validate_capability_manifest(const CapabilityManifest& c) noexcept {
    if(c.format_version!=1 || c.protocol_version!=1 || c.digest!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    if(!known_fingerprint(c.schemas) || !known_fingerprint(c.simulation))return fail(Error::NotReady);
    if(!c.maximum_encoded_bytes || c.maximum_encoded_bytes>65536 || !c.maximum_decoded_bytes || c.maximum_decoded_bytes>65536 || c.history_ticks>4096 ||
        (std::uint64_t(c.capabilities)&~std::uint64_t{127})!=0 || static_cast<unsigned>(c.recovery)>3)return fail(Error::InvalidArgument);
    if(has_capability(c.capabilities,Capability::Prediction) && (!has_capability(c.capabilities,Capability::DeterministicReplay) || !c.history_ticks))return fail(Error::InvalidArgument);
    if(has_capability(c.capabilities,Capability::CanonicalHistory) && !c.history_ticks)return fail(Error::InvalidArgument);
    if(c.recovery>=RecoveryGrade::PortableRestart && !has_capability(c.capabilities,Capability::PortableRestore))return fail(Error::InvalidArgument);
    if(c.recovery==RecoveryGrade::PortableExact && !has_capability(c.capabilities,Capability::CompletePostState))return fail(Error::InvalidArgument);
    return {};
}
// This validates declarations and bounds, not their truth. Manifest producers
// must derive implemented/qualified flags from executable qualification receipts.
inline Result<AdmittedCapabilities> admit_capabilities(const CapabilityManifest& local,const CapabilityManifest& remote,const AdmissionRequirements& r) noexcept {
    if(auto valid=validate_capability_manifest(local);!valid)return fail(valid.error());
    if(auto valid=validate_capability_manifest(remote);!valid)return fail(valid.error());
    if(!r.protocol_version || !known_fingerprint(r.schemas) || !known_fingerprint(r.simulation) || !r.encoded_bytes || !r.decoded_bytes ||
        (std::uint64_t(r.capabilities)&~std::uint64_t{127})!=0 || static_cast<unsigned>(r.recovery)>3)return fail(Error::InvalidArgument);
    if(local.protocol_version!=r.protocol_version || remote.protocol_version!=r.protocol_version)return fail(Error::Unsupported);
    if(local.schemas!=r.schemas || remote.schemas!=r.schemas || local.simulation!=r.simulation || remote.simulation!=r.simulation)return fail(Error::IncompatibleSchema);
    if(r.successor && !r.state_transfer_authorized)return fail(Error::PermissionDenied);
    // A successor must recover the entire authorized gameplay state. Restart-only
    // grades cannot be substituted for either migration publication profile.
    if(r.successor && r.recovery!=RecoveryGrade::PortableExact)return fail(Error::Unsupported);
    const auto common=static_cast<Capability>(std::uint64_t(local.capabilities)&std::uint64_t(remote.capabilities));
    const auto encoded=std::min(local.maximum_encoded_bytes,remote.maximum_encoded_bytes);
    const auto decoded=std::min(local.maximum_decoded_bytes,remote.maximum_decoded_bytes);
    const auto history=std::min(local.history_ticks,remote.history_ticks);
    if(!has_capability(common,r.capabilities) || encoded<r.encoded_bytes || decoded<r.decoded_bytes || history<r.history_ticks || local.recovery<r.recovery || remote.recovery<r.recovery)return fail(Error::Unsupported);
    return AdmittedCapabilities{encoded,decoded,history,common,r.recovery};
}
} // namespace superpos
