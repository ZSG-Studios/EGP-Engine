// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace egp::net {
enum class Result { Ok, Invalid, Busy, Unconfigured, Unauthorized, Full, NotFound, BadData, Failed };
struct Options {
    int tick_rate = 60, max_players = 32, max_entities = 1024;
    int messages_per_second = 1000, bytes_per_second = 4 * 1024 * 1024;
    int timeout_seconds = 5, token_lifetime_seconds = 30;
    std::string game_protocol = "egp-game-v1", simulation_fingerprint = "script-state-v1";
    bool allow_insecure_loopback = false;
    std::array<uint8_t, 32> private_key{};
    bool has_private_key = false;
    float simulated_loss = 0, simulated_latency_ms = 0, simulated_jitter_ms = 0;
};
struct Entity {
    uint64_t handle = 0, revision = 1, tick = 0;
    int kind = 0;
    int64_t authority_peer = -1;
    std::vector<uint8_t> state;
};
struct Peer { int64_t id; uint64_t client_id; float ping_ms; };
struct Statistics {
    uint64_t received_messages = 0, received_bytes = 0, rejected_messages = 0, tick = 0, server_tick = 0;
    int peers = 0, local_port = 0;
};
/// Engine-independent native replication and bounded Yojimbo message transport.
/// All operations belong to the constructing thread. Tokens and keys are not diagnostics.
class Session {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    static constexpr int MaxStateBytes = 4096, MaxReliableBytes = 4096, MaxUnreliableBytes = 900;
    explicit Session(const Options &options = Options());
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Result validation() const;
    Result listen(int port = 10515, const std::string &binding = "0.0.0.0");
    Result connect_loopback(const std::string &host, int port);
    Result connect_token(uint64_t client_id, const std::vector<uint8_t> &token, const std::string &binding = "0.0.0.0");
    Result issue_token(uint64_t client_id, const std::string &public_address, std::vector<uint8_t> &out);
    Result pump();
    Result stop();
    Result disconnect(int64_t peer);
    Result send(int64_t peer, const std::vector<uint8_t> &data, int channel = 0, int delivery = 2, bool application = false);
    Result spawn(int kind, const std::vector<uint8_t> &state, int64_t authority, uint64_t &handle);
    Result update(uint64_t handle, const std::vector<uint8_t> &state);
    Result despawn(uint64_t handle);
    Result set_visible(uint64_t handle, int64_t peer, bool visible);
    std::map<uint64_t, Entity> entities() const;
    std::vector<Peer> peers() const;
    Statistics statistics() const;
    std::string state() const;
    std::string fingerprint() const;
    bool is_pumping() const;
    std::function<void(const std::string &)> state_changed;
    std::function<void(int64_t)> peer_connected, peer_disconnected;
    std::function<void(int64_t, const std::vector<uint8_t> &)> application_received;
    std::function<void(int64_t, const std::vector<uint8_t> &, int, int)> packet_received;
    std::function<void(uint64_t, bool)> simulation_tick;
    std::function<void(const std::string &)> diagnostic;
};
}
