// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
namespace superpos::service::pairing {
enum class IceServerKind : std::uint8_t {Stun,TurnUdp,TurnTcp,TurnTls};
struct IceServerConfig {
    IceServerKind kind{IceServerKind::Stun};
    std::string_view host;
    std::uint16_t port{3478};
    std::string_view username,password;
};
// initialize() takes an immutable, owned copy. The libjuice profile admits at
// most one STUN and two UDP TURN servers. TCP/TLS TURN fail as Unsupported;
// they are never silently ignored. Host/credential maxima are 253/256 bytes.
struct NativeIceConfig {
    std::span<const IceServerConfig> servers;
    // Advertise relay candidates and require a relayed selected path at activation.
    // This does not constrain subsequent ICE route selection or provide a privacy guarantee.
    bool relay_candidates_only{};
    std::uint16_t port_begin{1024},port_end{65535};
};
struct NativePathInfo {bool local_relay{},remote_relay{};};
}
