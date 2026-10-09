#include "net_core.h"
#include <array>
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
int main(int argc, char **argv) {
    Options options;
    options.max_players = 2;
    options.max_entities = 64;
    options.messages_per_second = 32;
    options.bytes_per_second = 8192;
    Options receiver = options;
    receiver.messages_per_second = argc > 1 && std::string(argv[1]) == "--symmetric" ? 32 : 1000;
    Session server(options), first(receiver), second(receiver);
    std::array<Session *, 2> clients{&first, &second};
    std::array<int64_t, 2> owners{};
    std::array<std::vector<uint64_t>, 2> handles;
    auto until = [&](auto done, int milliseconds = 5000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            check(server.pump() == Result::Ok, "server pump");
            for (auto *client : clients) check(client->pump() == Result::Ok, "client pump");
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < end);
        return false;
    };
    auto admit = [&](int index) {
        std::vector<uint8_t> token;
        check(server.issue_token(10000 + index, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "fresh encrypted admission");
        check(clients[index]->connect_token(10000 + index, token) == Result::Ok, "encrypted join");
    };
    server.peer_connected = [&](int64_t peer) {
        for (const auto &record : server.peers()) {
            if (record.id != peer) continue;
            const int index = int(record.client_id - 10000);
            check(index >= 0 && index < 2, "account identity");
            owners[index] = peer;
            if (!handles[index].empty()) return;
            for (int entity = 0; entity < 32; ++entity) {
                uint64_t handle = 0;
                check(server.spawn(1, {uint8_t(index), uint8_t(entity), 0, 0}, peer, handle) == Result::Ok, "owned spawn");
                handles[index].push_back(handle);
            }
        }
    };
    server.simulation_tick = [&](uint64_t tick, bool) {
        for (int index = 0; index < 2; ++index)
            for (int entity = 0; entity < int(handles[index].size()); ++entity)
                check(server.update(handles[index][entity], {uint8_t(index), uint8_t(entity), uint8_t(tick), uint8_t(tick >> 8)}) == Result::Ok, "continuous 60 Hz update");
    };
    check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
    admit(0);
    admit(1);
    check(until([&] {
        return first.state() == "Connected" && second.state() == "Connected" && first.entities().size() == 64 && second.entities().size() == 64;
    }), "both rate-limited baselines complete while all entities keep changing");
    for (int index = 0; index < 2; ++index)
        for (uint64_t handle : handles[index])
            for (auto *client : clients)
                check(client->entities().at(handle).authority_peer == owners[index], "all owned entities replicated");
    const uint64_t tick = server.statistics().tick;
    check(until([&] {
        for (auto *client : clients)
            for (const auto &record : client->entities())
                if (record.second.tick <= tick) return false;
        return true;
    }), "every entity advances under the minimum outgoing message budget");
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (uint64_t handle : handles[0]) check(server.set_visible(handle, owners[1], false) == Result::Ok, "interest hide");
        check(until([&] { return second.entities().size() == 32; }), "hidden entities removed under pressure");
        for (uint64_t handle : handles[0]) check(!second.entities().count(handle), "no hidden membership");
        for (uint64_t handle : handles[0]) check(server.set_visible(handle, owners[1], true) == Result::Ok, "interest reentry");
        check(until([&] { return second.entities().size() == 64; }), "all reentered entities arrive under pressure");
        const int64_t revoked = owners[0];
        check(first.stop() == Result::Ok, "disconnect original owner");
        check(until([&] {
            if (server.peers().size() != 1) return false;
            const auto records = second.entities();
            for (uint64_t handle : handles[0]) if (records.at(handle).authority_peer != -1) return false;
            return true;
        }), "ownership revocation reaches the other client under pressure");
        admit(0);
        check(until([&] { return first.state() == "Connected" && first.entities().size() == 64; }), "fresh reconnect baseline completes under pressure");
        check(owners[0] != revoked, "reused transport slot receives a fresh peer identity");
        for (auto *client : clients)
            for (uint64_t handle : handles[0])
                check(client->entities().at(handle).authority_peer == -1, "reconnected account does not inherit revoked authority");
    }
    server.simulation_tick = nullptr;
    for (auto *client : clients) check(client->stop() == Result::Ok, "client cleanup");
    check(server.stop() == Result::Ok, "server cleanup");
    check(server.entities().empty() && server.peers().empty(), "world and peers clear on stop");
    std::cout << "EGP_FAIRNESS_CHECKS=passed entities=64 clients=2 reconnects=2 interest_cycles=2 messages_per_second=32" << std::endl;
}
