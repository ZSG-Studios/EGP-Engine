// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/service/rtc/adapter.hpp"
#include "superpos/transport.hpp"

namespace superpos::service::pairing {
// Borrows the Registry claim and NativeAdapter, which outlive this provider.
// The owner explicitly releases the claim after destroying its Session/provider.
// Revocation during any trusted native/clock callback fails subsequent IO closed.
// One pending immutable frame per carrier and one receive frame are charged in
// a single fallible Backend allocation; the wrapper does not allocate again.
class PairedTransport final : public TransportProvider {
    struct Impl;
    Impl* impl_{};
    Allocator* allocator_{};
    void destroy() noexcept;
public:
    PairedTransport() noexcept=default;
    ~PairedTransport();
    PairedTransport(const PairedTransport&)=delete;
    PairedTransport& operator=(const PairedTransport&)=delete;
    PairedTransport(PairedTransport&&) noexcept;
    PairedTransport& operator=(PairedTransport&&) noexcept;
    static Result<PairedTransport> create(Allocator&,Registry&,NativeAdapter&,OwnershipToken) noexcept;
    TransportCapabilities capabilities() const noexcept override;
    bool ready() const noexcept override;
    Status advance() noexcept override;
    Status send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
    Result<CarrierProgress> advance_frames() noexcept override;
    Status send_frame(std::span<const std::byte>,CarrierLane) noexcept override;
    Result<CarrierFrame> receive_frame(std::span<std::byte>) noexcept override;
};
}
