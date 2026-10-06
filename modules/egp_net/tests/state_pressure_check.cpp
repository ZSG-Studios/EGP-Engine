#include "net_core.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace egp::net;
void check(bool good, const char *name) {
    if (!good) {
        std::cerr << "FAILED: " << name << std::endl;
        std::exit(1);
    }
}
template <typename Predicate>
bool until(Session &server, Session &client, Predicate done, int milliseconds = 2500) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        server.pump();
        client.pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
int main() {
    Options options;
    options.allow_insecure_loopback = true;
    options.max_players = 1;
    options.max_entities = 2;
    options.simulated_latency_ms = 100;
    Session server(options), client(options);
    check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
    check(client.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "join");
    check(until(server, client, [&] { return client.state() == "Connected"; }), "baseline");
    uint64_t root = 0, owned = 0;
    check(server.spawn(1, {0}, -1, root) == Result::Ok, "root spawn");
    server.simulation_tick = [&](uint64_t tick, bool) {
        check(server.update(root, {uint8_t(tick), uint8_t(tick >> 8)}) == Result::Ok, "fixed-tick update");
    };
    check(until(server, client, [&] { return server.statistics().tick >= 180; }, 4000), "three seconds of 60 Hz pressure");
    const int64_t peer = server.peers()[0].id;
    check(server.spawn(2, {42}, peer, owned) == Result::Ok, "late owned spawn");
    check(until(server, client, [&] { return client.entities().count(owned); }), "late owned entity bypasses stale revision backlog");
    check(client.entities().at(owned).authority_peer == peer, "late ownership replicated");
    check(client.statistics().server_tick >= client.entities().at(root).tick, "statistics track accepted entity ticks");
    server.simulation_tick = nullptr;
    const std::vector<uint8_t> final_state{77, 88, 99};
    check(server.update(root, final_state) == Result::Ok, "final update");
    check(until(server, client, [&] { return client.entities().at(root).state == final_state; }), "latest coalesced revision arrives promptly");
    check(server.set_visible(root, peer, false) == Result::Ok, "hide");
    check(until(server, client, [&] { return !client.entities().count(root); }), "hide clears queued state");
    check(server.set_visible(root, peer, true) == Result::Ok, "show");
    check(until(server, client, [&] { return client.entities().count(root) && client.entities().at(root).state == final_state; }), "reentry restores latest revision");
    check(server.update(root, {1}) == Result::Ok, "enqueue before destruction");
    server.pump();
    check(server.despawn(root) == Result::Ok, "despawn pending state");
    check(until(server, client, [&] { return !client.entities().count(root); }), "pending state cannot revive despawned entity");
    client.stop();
    server.stop();
    std::cout << "EGP_STATE_PRESSURE_CHECKS=passed" << std::endl;
}
