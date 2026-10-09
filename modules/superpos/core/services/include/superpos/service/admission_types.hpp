// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/admission_wire.hpp"
namespace superpos::service::admission {
struct CredentialScope {
    std::uint64_t match{},session{},authority_epoch{},actor{},actor_epoch{},permissions{};
};
// Trusted provisioned store, never selected by incoming method/resource names.
// All borrowed objects outlive the complete host operation. The callback writes
// exactly32 secret bytes and its independently authorized scope; failure does
// not transfer ownership. The caller zeroizes secret scratch on every exit.
// It must not retain references to the request, output secret or output scope.
class CredentialProvider {
public:
    virtual ~CredentialProvider()=default;
    virtual Status resolve(const Request&,std::span<std::byte,32>,CredentialScope&) noexcept=0;
};
struct Grant {
    std::uint64_t match{},session{},authority_epoch{},connection_incarnation{},actor{},actor_epoch{},permissions{};
};
}
