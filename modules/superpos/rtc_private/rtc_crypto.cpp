// SPDX-License-Identifier: MIT
#include "rtc_crypto.hpp"
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <algorithm>
#include <cstdlib>

namespace superpos_egp {
using namespace superpos;
namespace a = superpos::service::admission;
namespace {
struct Enter {
    bool &flag;
    explicit Enter(bool &value) noexcept : flag(value) { flag = true; }
    ~Enter() { flag = false; }
};
struct Secret {
    std::array<std::byte, 32> bytes{};
    ~Secret() { mbedtls_platform_zeroize(bytes.data(), bytes.size()); }
};
Error convert(psa_status_t status) noexcept {
    switch (status) {
        case PSA_ERROR_INVALID_SIGNATURE: return Error::AuthenticationFailed;
        case PSA_ERROR_INSUFFICIENT_MEMORY: return Error::OutOfMemory;
        case PSA_ERROR_NOT_SUPPORTED: return Error::Unsupported;
        case PSA_ERROR_BAD_STATE: return Error::NotReady;
        default: return Error::Io;
    }
}
constexpr auto mac_algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);
using Transcript = std::array<std::byte, a::proof_domain.size() + a::authenticated_request_bytes>;
Result<Transcript> encode(const a::Request &request) noexcept {
    std::array<std::byte, a::request_bytes> wire{};
    if (auto status = a::encode_request(request, wire); !status) { return fail(status.error()); }
    Transcript bytes{};
    for (size_t i = 0; i < a::proof_domain.size(); ++i) { bytes[i] = std::byte(a::proof_domain[i]); }
    std::copy_n(wire.begin(), a::authenticated_request_bytes, bytes.begin() + a::proof_domain.size());
    return bytes;
}
Result<psa_key_id_t> import_key(std::span<const std::byte, 32> bytes, psa_key_usage_t usage) noexcept {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_algorithm(&attributes, mac_algorithm);
    psa_set_key_usage_flags(&attributes, usage);
    psa_key_id_t key{};
    const auto status = psa_import_key(&attributes, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), &key);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) { return fail(convert(status)); }
    return key;
}
void destroy_key(psa_key_id_t key) noexcept {
    // Keep ownership until actual destruction succeeds. Never drop an unknown
    // live key handle or finalize a runtime belonging to the engine.
    if (psa_destroy_key(key) != PSA_SUCCESS) { std::abort(); }
}
}
Status BorrowedPsaCrypto::available() const noexcept {
    if (std::this_thread::get_id() != owner) { return fail(Error::PermissionDenied); }
    if (entered) { return fail(Error::Busy); }
    if (stopped) { return fail(Error::NotReady); }
    return {};
}
BorrowedPsaCrypto::~BorrowedPsaCrypto() {
    if (std::this_thread::get_id() != owner || entered) { std::abort(); }
}
Status BorrowedPsaCrypto::stop() noexcept {
    if (std::this_thread::get_id() != owner) { return fail(Error::PermissionDenied); }
    if (entered) { return fail(Error::Busy); }
    stopped = true;
    return {};
}
Status BorrowedPsaCrypto::hash(std::span<const std::byte> input, std::span<std::byte, 32> output) noexcept {
    if (auto status = available(); !status) { return status; }
    Enter guard(entered);
    std::array<std::byte, 32> digest{};
    size_t written{};
    const auto status = psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const unsigned char *>(input.data()), input.size(),
            reinterpret_cast<unsigned char *>(digest.data()), digest.size(), &written);
    if (status != PSA_SUCCESS) { return fail(convert(status)); }
    if (written != digest.size()) { return fail(Error::Io); }
    std::copy(digest.begin(), digest.end(), output.begin());
    return {};
}
Status BorrowedPsaCrypto::fresh(std::span<std::byte, 32> output) noexcept {
    if (auto status = available(); !status) { return status; }
    Enter guard(entered);
    Secret bytes;
    const auto status = psa_generate_random(reinterpret_cast<unsigned char *>(bytes.bytes.data()), bytes.bytes.size());
    if (status != PSA_SUCCESS) { return fail(convert(status)); }
    std::copy(bytes.bytes.begin(), bytes.bytes.end(), output.begin());
    return {};
}
Result<a::Request> BorrowedPsaCrypto::sign(const a::Request &input, std::span<const std::byte, 32> source) noexcept {
    if (auto status = available(); !status) { return fail(status.error()); }
    Enter guard(entered);
    a::Request request = input;
    Secret secret; std::copy(source.begin(), source.end(), secret.bytes.begin());
    auto transcript = encode(request); if (!transcript) { return fail(transcript.error()); }
    auto key = import_key(secret.bytes, PSA_KEY_USAGE_SIGN_MESSAGE); if (!key) { return fail(key.error()); }
    std::array<std::byte, 32> tag{}; size_t written{};
    const auto status = psa_mac_compute(*key, mac_algorithm, reinterpret_cast<const unsigned char *>(transcript->data()), transcript->size(),
            reinterpret_cast<unsigned char *>(tag.data()), tag.size(), &written);
    destroy_key(*key);
    if (status != PSA_SUCCESS) { return fail(convert(status)); }
    if (written != tag.size()) { return fail(Error::Io); }
    request.proof = tag;
    return request;
}
Result<a::Grant> BorrowedPsaCrypto::verify(const a::Request &input, const a::Challenge &issued) noexcept {
    if (auto status = available(); !status) { return fail(status.error()); }
    Enter guard(entered);
    const a::Request request = input; const a::Challenge challenge = issued;
    if (auto status = a::matches_issued(request, challenge); !status) { return fail(status.error()); }
    auto transcript = encode(request); if (!transcript) { return fail(transcript.error()); }
    Secret secret; a::CredentialScope scope{};
    if (auto status = credentials.resolve(request, secret.bytes, scope); !status) { return fail(status.error()); }
    auto key = import_key(secret.bytes, PSA_KEY_USAGE_VERIFY_MESSAGE); if (!key) { return fail(key.error()); }
    const auto status = psa_mac_verify(*key, mac_algorithm, reinterpret_cast<const unsigned char *>(transcript->data()), transcript->size(),
            reinterpret_cast<const unsigned char *>(request.proof.data()), request.proof.size());
    destroy_key(*key);
    if (status != PSA_SUCCESS) { return fail(convert(status)); }
    const auto &c = request.challenge;
    if (scope.match != c.match || scope.session != c.session || scope.authority_epoch != c.authority_epoch ||
            scope.actor != request.actor || scope.actor_epoch != request.actor_epoch || (scope.permissions & ~a::known_permissions) ||
            (request.requested_permissions & ~scope.permissions)) { return fail(Error::PermissionDenied); }
    return a::Grant{c.match, c.session, c.authority_epoch, c.connection_incarnation, request.actor, request.actor_epoch, request.requested_permissions};
}
}
