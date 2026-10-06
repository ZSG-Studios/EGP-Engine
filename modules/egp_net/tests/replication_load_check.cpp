#include "net_core.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace egp::net;
void check(bool good, const char *name) {
    if (!good) { std::cerr << "FAILED: " << name << std::endl; std::exit(1); }
}
int main() {
    constexpr int count = 8;
    Options options;
    options.max_players = count; options.max_entities = count;
    options.messages_per_second = 32; options.bytes_per_second = 65536;
    options.simulated_latency_ms = 100; options.simulated_jitter_ms = 25; options.simulated_loss = 3;
    Session server(options);
    std::array<std::unique_ptr<Session>, count> clients;
    std::array<int64_t, count> owners{};
    std::array<uint64_t, count> handles{};
    for (auto &client : clients) client = std::make_unique<Session>(options);
    auto payload = [](int index, uint64_t tick) {
        std::vector<uint8_t> data(Session::MaxStateBytes, uint8_t(0xA0 + index));
        for (int i = 0; i < 8; ++i) data[i] = uint8_t(tick >> (8 * i));
        return data;
    };
    auto until = [&](auto done, int milliseconds = 20000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            check(server.pump() == Result::Ok, "server pump");
            for (auto &client : clients) {
                check(client->pump() == Result::Ok, "client pump");
                check(client->statistics().rejected_messages == 0, "no valid WAN state rejected");
            }
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < end);
        return false;
    };
    auto admit = [&](int index) {
        std::vector<uint8_t> token;
        check(server.issue_token(20000 + index, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "fresh admission");
        check(clients[index]->connect_token(20000 + index, token) == Result::Ok, "encrypted join");
    };
    server.peer_connected = [&](int64_t peer) {
        for (const auto &record : server.peers()) {
            if (record.id != peer) continue;
            const int index = int(record.client_id - 20000);
            check(index >= 0 && index < count, "account identity");
            owners[index] = peer;
            if (!handles[index]) check(server.spawn(1, payload(index, 0), peer, handles[index]) == Result::Ok, "4 KiB owned spawn");
        }
    };
    server.simulation_tick = [&](uint64_t tick, bool) {
        for (int i = 0; i < count; ++i)
            if (handles[i]) check(server.update(handles[i], payload(i, tick)) == Result::Ok, "60 Hz 4 KiB revision");
    };
    check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
    for (int i = 0; i < count; ++i) admit(i);
    check(until([&] {
        for (auto &client : clients) if (client->state() != "Connected" || client->entities().size() != count) return false;
        return true;
    }), "all eight changing fragmented baselines complete under WAN impairment");
    const uint64_t cutoff = server.statistics().tick;
    check(until([&] {
        for (auto &client : clients)
            for (int i = 0; i < count; ++i) {
                const auto entity = client->entities().at(handles[i]);
                if (entity.tick <= cutoff) return false;
                check(entity.authority_peer == owners[i], "all owners replicated");
                check(entity.state == payload(i, entity.tick), "exact 4 KiB fragmented state integrity");
            }
        return true;
    }), "all entity revisions advance on every peer");
    server.simulation_tick = nullptr;
    const uint64_t final_tick = server.statistics().tick + 1;
    for (int i = 0; i < count; ++i) check(server.update(handles[i], payload(i, final_tick)) == Result::Ok, "final coalesced update");
    check(until([&] {
        for (auto &client : clients)
            for (int i = 0; i < count; ++i) if (client->entities().at(handles[i]).state != payload(i, final_tick)) return false;
        return true;
    }), "all final coalesced revisions converge");
    check(server.set_visible(handles[0], owners[1], false) == Result::Ok, "hide");
    check(until([&] { return clients[1]->entities().size() == count - 1; }), "interest removal");
    check(server.set_visible(handles[0], owners[1], true) == Result::Ok, "reenter");
    check(until([&] { return clients[1]->entities().size() == count && clients[1]->entities().at(handles[0]).state == payload(0, final_tick); }), "full state on reentry");
    const int64_t revoked = owners[0];
    check(clients[0]->stop() == Result::Ok, "owner disconnect");
    check(until([&] {
        if (server.peers().size() != count - 1) return false;
        for (int i = 1; i < count; ++i) if (clients[i]->entities().at(handles[0]).authority_peer != -1) return false;
        return true;
    }), "revocation reaches all seven remaining peers");
    admit(0);
    check(until([&] { return clients[0]->state() == "Connected" && clients[0]->entities().size() == count; }), "fresh fragmented reconnect baseline");
    check(owners[0] != revoked, "fresh connection generation");
    for (auto &client : clients) check(client->entities().at(handles[0]).authority_peer == -1, "revoked authority does not transfer");
    for (auto &client : clients) check(client->stop() == Result::Ok, "client cleanup");
    check(server.stop() == Result::Ok && server.peers().empty() && server.entities().empty(), "server cleanup");
    std::cout << "EGP_REPLICATION_LOAD_CHECKS=passed clients=8 entities=8 state_bytes=4096 updates_hz=60 latency_ms=100 jitter_ms=25 loss_percent=3 reconnects=1" << std::endl;
}
