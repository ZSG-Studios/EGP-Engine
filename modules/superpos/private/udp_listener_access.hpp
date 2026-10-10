// SPDX-License-Identifier: MIT
#pragma once
// PRIVATE C++23: lets a Session association attach to a listener's shared
// socket. The returned port must be destroyed before the listener's UdpMux;
// the Session's network pins the listener for exactly that reason.
#include <superpos/udp_mux.hpp>
class SuperposUdpListener;
struct SuperposUdpListenerAccess {
    static superpos::Result<superpos::UdpMuxPort> attach(SuperposUdpListener &, std::uint64_t connection_id) noexcept;
    static superpos::Result<superpos::UdpMuxStatistics> statistics(const SuperposUdpListener &) noexcept;
};
