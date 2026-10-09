#include "superpos/crypto_nonce.hpp"
#include <psa/crypto.h>
#include <mbedtls/platform_util.h>
#include <cstring>
namespace superpos {
Result<PsaLeaseNonce> PsaLeaseNonce::create(CryptoRuntime& runtime) noexcept {
    if(auto initialized=runtime.initialize();!initialized)return fail(initialized.error());
    return PsaLeaseNonce(true);
}
Status PsaLeaseNonce::fresh(std::span<std::byte,32> output) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);
    std::array<unsigned char,32> bytes{};
    const auto status=psa_generate_random(bytes.data(),bytes.size());
    struct Clear {std::array<unsigned char,32>& bytes;~Clear(){mbedtls_platform_zeroize(bytes.data(),bytes.size());}} clear{bytes};
    if(status!=PSA_SUCCESS){
        if(status==PSA_ERROR_INSUFFICIENT_MEMORY)return fail(Error::OutOfMemory);
        if(status==PSA_ERROR_BAD_STATE)return fail(Error::NotReady);
        if(status==PSA_ERROR_NOT_SUPPORTED)return fail(Error::Unsupported);
        return fail(Error::AuthenticationFailed);
    }
    bool nonzero=false;for(auto byte:bytes)nonzero=nonzero||byte!=0;
    if(!nonzero)return fail(Error::AuthenticationFailed);
    std::memcpy(output.data(),bytes.data(),bytes.size());return {};
}
}
