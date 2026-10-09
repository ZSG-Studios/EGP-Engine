// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/relay.hpp"
#include "superpos/dtls.hpp"
#include <optional>
namespace superpos::relay {
// Host initializes one crypto runtime and supplies its existing allocation
// account scope around native DTLS methods. advance never invokes relay crypto
// from a native DTLS callback: the two pumps run sequentially.
class RelayedDtls final:public TransportProvider {
    Allocator& allocator_;Clock& clock_;RelayClient& relay_;AuthProvider& auth_;
    DtlsConfig config_;std::array<std::byte,32> address_{};std::size_t address_size_{};
    std::optional<DtlsAssociation> dtls_;bool busy_{};
    const std::thread::id owner_{std::this_thread::get_id()};
public:
    RelayedDtls(Allocator&,Clock&,RelayClient&,AuthProvider&,DtlsConfig,std::span<const std::byte>) noexcept;
    ~RelayedDtls();
    RelayedDtls(const RelayedDtls&)=delete;RelayedDtls& operator=(const RelayedDtls&)=delete;
    RelayedDtls(RelayedDtls&&)=delete;RelayedDtls& operator=(RelayedDtls&&)=delete;
    TransportCapabilities capabilities() const noexcept override;
    bool ready() const noexcept override;
    Status advance() noexcept override;
    Status send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
};
}
