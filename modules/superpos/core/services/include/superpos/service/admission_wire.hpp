// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace superpos::service::admission {
inline constexpr std::size_t challenge_bytes = 80;
inline constexpr std::size_t request_bytes = 144;
inline constexpr std::size_t authenticated_request_bytes = request_bytes - 32;
inline constexpr std::uint64_t known_permissions = 15;
// HMAC-SHA256, full32-byte tag. Domain excludes any C-string terminator.
inline constexpr std::string_view proof_domain = "superpos-control-admission-v1";
// Values are full-width identities. Epoch zero is valid; identity zero is not.
// Nonce issuance, expiry and authorization belong to the owning admission host.
struct Challenge {
    std::uint64_t match{}, session{}, authority_epoch{}, connection_incarnation{}, allowed_permissions{};
    std::array<std::byte,32> nonce{};
    bool operator==(const Challenge&) const = default;
};
struct Request {
    Challenge challenge{};
    std::uint64_t actor{}, actor_epoch{}, requested_permissions{};
    std::array<std::byte,32> proof{};
    bool operator==(const Request&) const = default;
};
// Fixed little-endian canonical records. Encoders validate entirely before
// publishing; decoders reject any trailing bytes, reserved bits or unknown scope.
// Output/input may alias: encoding stages the complete small record on stack.
Status encode_challenge(const Challenge&,std::span<std::byte>) noexcept;
Result<Challenge> decode_challenge(std::span<const std::byte>) noexcept;
Status encode_request(const Request&,std::span<std::byte>) noexcept;
Result<Request> decode_request(std::span<const std::byte>) noexcept;
// Equality of the issued challenge is a precondition to secret verification.
// This is structural validation only; neither a nonce nor a nonzero tag proves
// authentication. Verify a cryptographic MAC of proof_domain and the canonical
// first authenticated_request_bytes, then independently authorize the principal.
Status matches_issued(const Request&,const Challenge&) noexcept;
}
