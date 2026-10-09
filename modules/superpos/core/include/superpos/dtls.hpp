#pragma once
#include "transport.hpp"
namespace superpos {
enum class CryptoOwnership { Standalone, Borrowed };
class CryptoRuntime {
 bool owns_{};
public:
 // Startup runs on one owner thread. Standalone initializes process globals;
 // neither mode tears them down, because associations/host code may share them.
 explicit CryptoRuntime(CryptoOwnership) noexcept;
 ~CryptoRuntime();
 CryptoRuntime(const CryptoRuntime&)=delete;
 Status initialize() noexcept;
};
struct DtlsConfig { bool server{}; PeerId identity{}; std::uint64_t handshake_timeout_ms{15000}; std::size_t udp_payload_ceiling{1200}; };
// One authenticated association per peer. Socket routing/address validation is
// below this provider; this initial profile does not permit address migration.
class DtlsAssociation final : public TransportProvider {
 struct Impl; Impl* impl_{}; Allocator* allocator_{};
public:
 static constexpr std::size_t maximum_frame_bytes = 960;
 DtlsAssociation() noexcept=default;
 ~DtlsAssociation();
 DtlsAssociation(const DtlsAssociation&)=delete;
 DtlsAssociation(DtlsAssociation&&) noexcept;
 DtlsAssociation& operator=(DtlsAssociation&&) noexcept;
 // address_identity is the canonical remote endpoint supplied by trusted socket
 // routing (family/address/port), not a client-provided account/identity string.
 // Clock, IO, auth provisioning, and allocator outlive the association.
 static Result<DtlsAssociation> create(Allocator&,Clock&,DatagramIO&,AuthProvider&,DtlsConfig,std::span<const std::byte> address_identity) noexcept;
 TransportCapabilities capabilities() const noexcept override;
 Status advance() noexcept override;
 bool ready() const noexcept override;
 Status send(std::span<const std::byte>) noexcept override;
 Result<std::size_t> receive(std::span<std::byte>) noexcept override;
};
}
