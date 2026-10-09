#pragma once
#include "superpos/dtls.hpp"
#include "superpos/lease.hpp"
#include <thread>
namespace superpos {
// Optional PSA CSPRNG provider for the qualified single-owner crypto profile.
// Startup honors CryptoRuntime's standalone/borrowed initialization contract.
// No engine globals are freed. Shared multi-owner crypto needs separate runtime
// threading qualification; this provider does not manufacture that capability.
class PsaLeaseNonce final : public LeaseNonceProvider {
    explicit PsaLeaseNonce(bool) noexcept {}
    std::thread::id owner_{std::this_thread::get_id()};
public:
    static Result<PsaLeaseNonce> create(CryptoRuntime&) noexcept;
    Status fresh(std::span<std::byte,32>) noexcept override;
};
}
