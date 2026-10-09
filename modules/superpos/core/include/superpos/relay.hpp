// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/relay_wire.hpp"
#include "superpos/udp.hpp"
#include "superpos/platform_clock.hpp"
#include <thread>

namespace superpos::relay {
struct PrincipalScope {std::uint64_t match{},session{},authority_epoch{},actor{},actor_epoch{},permissions{};};
struct SidePermit {PrincipalScope scope;Key upstream{},downstream{};};
struct RoutePermit {
    RouteId id{};std::uint64_t generation{},expires_us{};
    std::uint32_t slot{};
    std::array<SidePermit,2> sides{};
    std::uint32_t bytes_per_second{65536},packets_per_second{128},handshakes_per_second{8};
};
struct Limits {std::size_t routes{1024},queued_datagrams{256},per_side_queue{4};std::uint32_t packets_per_advance{8},global_bytes_per_second{8u<<20},global_handshakes_per_second{128};};
struct Statistics {std::uint64_t accepted{},forwarded{},refused{},expired{},revoked{},queued_bytes{},owned_bytes{};};
// These classes do not validate or renew trusted time. The host owns that act.
class GuardedTime {
    ClockSource& source_;ContinuityGuard& guard_;
public:
    GuardedTime(ClockSource& source,ContinuityGuard& guard) noexcept:source_(source),guard_(guard){}
    Result<ClockObservation> now() noexcept {return guard_.observe(source_);}
};
class RelayServer final {
    struct Route;struct Frame;
    Allocator& allocator_;UdpListener& socket_;MacProvider& crypto_;GuardedTime& clock_;
    const std::thread::id owner_{std::this_thread::get_id()};
    Limits limits_;Route* routes_{};Frame* frames_{};Key cookie_key_{};
    Statistics statistics_{};bool busy_{},ready_{};
    std::uint64_t window_us_{};std::uint32_t window_bytes_{},window_handshakes_{};
    Status entry() const noexcept;
    bool aliases(const void*,std::size_t) const noexcept;
    void retire(std::size_t,bool) noexcept;
    Status enqueue(std::size_t,std::uint8_t,Kind,std::span<const std::byte>,const IpEndpoint&,std::uint64_t,std::uint64_t) noexcept;
    Status process(const ReceivedDatagram&,std::span<const std::byte>,const ClockObservation&) noexcept;
public:
    RelayServer(Allocator&,UdpListener&,MacProvider&,GuardedTime&,Limits={}) noexcept;
    ~RelayServer();
    RelayServer(const RelayServer&)=delete;RelayServer& operator=(const RelayServer&)=delete;
    RelayServer(RelayServer&&)=delete;RelayServer& operator=(RelayServer&&)=delete;
    Status reserve() noexcept;
    // Permit.slot must equal the explicit index. Greater generation and
    // independently fresh keys required even after retire; IDs may be replaced.
    Status install(std::size_t,const RoutePermit&) noexcept;
    Status revoke(std::size_t,std::uint64_t generation) noexcept;
    Status advance() noexcept;
    Result<Statistics> statistics() const noexcept;
};
enum class ClientState {Unreserved,Idle,Handshaking,Ready,Failed};
class RelayClient final:public DatagramIO {
    struct Storage;
    Allocator& allocator_;DatagramIO* socket_;MacProvider& crypto_;GuardedTime& clock_;
    const std::thread::id owner_{std::this_thread::get_id()};Storage* storage_{};
    ClientState state_{ClientState::Unreserved};bool busy_{};
    Status entry() const noexcept;
    bool aliases(const void*,std::size_t) const noexcept;
    void fail_closed() noexcept;
    Status send_control(Kind,const ClockObservation&) noexcept;
public:
    RelayClient(Allocator&,DatagramIO&,MacProvider&,GuardedTime&) noexcept;
    ~RelayClient();
    RelayClient(const RelayClient&)=delete;RelayClient& operator=(const RelayClient&)=delete;
    RelayClient(RelayClient&&)=delete;RelayClient& operator=(RelayClient&&)=delete;
    Status reserve() noexcept;
    Status configure(const RouteId&,std::uint64_t generation,std::uint32_t slot,std::uint8_t side,const SidePermit&,std::uint64_t expires_us) noexcept;
    // Caller supplies a live socket already connected to the same trusted relay.
    // Starts address proof without resetting route keys or replay/counter state.
    Status rebind(DatagramIO&) noexcept;
    Status advance() noexcept;
    Result<ClientState> state() const noexcept;
    // BIO-safe: no crypto, time, network or allocator callback. Busy accepts nothing.
    Result<std::size_t> send(std::span<const std::byte>) noexcept override;
    Result<std::size_t> receive(std::span<std::byte>) noexcept override;
};
}
