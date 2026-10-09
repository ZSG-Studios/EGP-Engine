// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/relay_wire.hpp"
#include "superpos/dtls.hpp"
#include <thread>

namespace superpos::relay {
// Optional standard PSA HMAC-SHA256 backend. The host initializes exactly one
// runtime and supplies its existing allocator/account scope around these calls.
// This adapter never initializes/frees PSA globals. The borrowed runtime and
// its allocation policy must outlive this object. Serialize all use of shared
// native runtime state, including other providers, on the owning host thread.
// Engines may inject a different MacProvider using their own runtime instead.
class PsaRelayMac final:public MacProvider {
    CryptoRuntime& runtime_;
    const std::thread::id owner_{std::this_thread::get_id()};
    bool busy_{},ready_{};
public:
    explicit PsaRelayMac(CryptoRuntime& runtime) noexcept:runtime_(runtime){}
    ~PsaRelayMac();
    PsaRelayMac(const PsaRelayMac&)=delete;
    PsaRelayMac& operator=(const PsaRelayMac&)=delete;
    PsaRelayMac(PsaRelayMac&&)=delete;
    PsaRelayMac& operator=(PsaRelayMac&&)=delete;
    // Probes existing initialization without acquiring another global runtime.
    Status initialize() noexcept;
    Status sign(std::span<const std::byte,32>,std::span<const std::byte>,std::span<std::byte,32>) noexcept override;
    Status verify(std::span<const std::byte,32>,std::span<const std::byte>,std::span<const std::byte,32>) noexcept override;
    Status random(std::span<std::byte>) noexcept override;
};
}
