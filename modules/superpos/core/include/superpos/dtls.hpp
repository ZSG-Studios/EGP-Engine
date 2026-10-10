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
struct DtlsStatistics {
 // Records whose datagram the local path refuses as oversize (EMSGSIZE). They
 // are consumed like an in-network drop; the association remains usable.
 std::uint64_t path_oversize_drops{};
 std::uint64_t path_challenges_sent{},path_responses_sent{},path_promotions{};
 std::uint64_t path_challenge_failures{},stale_path_responses{};
};
// path_validation: both ends reserve 17-byte plaintext records tagged 0x16
// (challenge) and 0x17 (response) for address validation; the packet layer's
// frame tags never collide with them. Required for single-port servers.
struct DtlsConfig { bool server{}; PeerId identity{}; std::uint64_t handshake_timeout_ms{15000}; std::size_t udp_payload_ceiling{1200}; bool path_validation{}; };
// One authenticated association per peer. Over a fixed (connected) datagram
// path the address never changes. Over a path-aware IO (UdpMuxPort) a server
// may be created with an empty address_identity: cookies bind to the source
// the shared socket observed for the first handshake datagram, and the
// handshake's completion authenticates that address. Afterwards an
// authenticated record from a new source address makes it a candidate; with
// path_validation the server sends it a fresh 16-byte random challenge (three
// attempts, 500 ms doubling, within the port's amplification limit) and moves
// outbound traffic only when the encrypted echo arrives from that address.
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
 Result<DtlsStatistics> statistics() const noexcept;
};
}
