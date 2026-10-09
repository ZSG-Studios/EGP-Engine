#include "superpos/crypto_digest.hpp"
#include <psa/crypto.h>
#include <mbedtls/platform_util.h>
#include <array>
#include <cstring>
namespace superpos {
Result<PsaSha256> PsaSha256::create(CryptoRuntime& runtime) noexcept {
    if(auto status=runtime.initialize();!status)return fail(status.error());
    return PsaSha256(true);
}
Status PsaSha256::hash(std::span<const std::byte> input,std::span<std::byte,32> output) noexcept {
    std::array<unsigned char,32> temporary{};std::size_t size{};
    const auto status=psa_hash_compute(PSA_ALG_SHA_256,
        reinterpret_cast<const unsigned char*>(input.data()),input.size(),temporary.data(),temporary.size(),&size);
    if(status==PSA_SUCCESS && size==temporary.size()) {
        std::memcpy(output.data(),temporary.data(),temporary.size());
        mbedtls_platform_zeroize(temporary.data(),temporary.size());return {};
    }
    mbedtls_platform_zeroize(temporary.data(),temporary.size());
    if(status==PSA_ERROR_INSUFFICIENT_MEMORY)return fail(Error::OutOfMemory);
    if(status==PSA_ERROR_BAD_STATE)return fail(Error::NotReady);
    if(status==PSA_ERROR_NOT_SUPPORTED)return fail(Error::Unsupported);
    return fail(Error::AuthenticationFailed);
}
}
