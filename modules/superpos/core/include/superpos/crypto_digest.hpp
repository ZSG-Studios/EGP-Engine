#pragma once
#include "capability.hpp"
#include "dtls.hpp"
namespace superpos {
// Optional PSA backend. The caller selects standalone or borrowed initialization;
// this provider never initializes/frees a second engine crypto runtime.
class PsaSha256 final : public CryptographicDigest {
    explicit PsaSha256(bool) noexcept {}
public:
    static Result<PsaSha256> create(CryptoRuntime&) noexcept;
    DigestAlgorithm algorithm() const noexcept override { return DigestAlgorithm::Sha256; }
    Status hash(std::span<const std::byte>,std::span<std::byte,32>) noexcept override;
};
}
