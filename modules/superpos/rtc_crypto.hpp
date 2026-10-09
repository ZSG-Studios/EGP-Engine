// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/rtc/pairing.hpp"

namespace superpos_egp {
// Private C++23 implementation. The engine must keep its initialized PSA runtime
// alive until the network host and this borrower have stopped. This class never
// initializes/finalizes PSA or changes its allocator. Its source is compiled
// against the exact engine PSA configuration, not the standalone service's.
class BorrowedPsaCrypto final : public superpos::service::pairing::ProofVerifier,
        public superpos::CryptographicDigest, public superpos::LeaseNonceProvider {
    superpos::service::admission::CredentialProvider &credentials;
    const std::thread::id owner = std::this_thread::get_id();
    bool entered = false, stopped = false;
    superpos::Status available() const noexcept;
public:
    explicit BorrowedPsaCrypto(superpos::service::admission::CredentialProvider &provider) noexcept : credentials(provider) {}
    ~BorrowedPsaCrypto();
    BorrowedPsaCrypto(const BorrowedPsaCrypto &) = delete;
    BorrowedPsaCrypto &operator=(const BorrowedPsaCrypto &) = delete;
    superpos::DigestAlgorithm algorithm() const noexcept override { return superpos::DigestAlgorithm::Sha256; }
    superpos::Status hash(std::span<const std::byte>, std::span<std::byte, 32>) noexcept override;
    superpos::Status fresh(std::span<std::byte, 32>) noexcept override;
    superpos::Result<superpos::service::admission::Grant> verify(
            const superpos::service::admission::Request &,
            const superpos::service::admission::Challenge &) noexcept override;
    superpos::Result<superpos::service::admission::Request> sign(
            const superpos::service::admission::Request &, std::span<const std::byte, 32>) noexcept;
    superpos::Status stop() noexcept;
};
}
