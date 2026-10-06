#include "net_core.h"
#include "yojimbo.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace egp::net;
void check(bool good, const char *name) {
    if (!good) { std::cerr << "FAILED: " << name << std::endl; std::exit(1); }
}
void jitter_bursts() {
    Options receiver; receiver.allow_insecure_loopback = true;
    receiver.messages_per_second = 32; receiver.bytes_per_second = 8192;
    Options sender = receiver;
    sender.simulated_latency_ms = 1000; sender.simulated_jitter_ms = 600;
    Session server(receiver), client(sender);
    int received = 0, sent = 0;
    server.packet_received = [&](int64_t, const std::vector<uint8_t> &, int, int) { ++received; };
    check(server.listen(0, "127.0.0.1") == Result::Ok, "listen");
    check(client.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "connect");
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(12)) {
        check(server.pump() == Result::Ok && client.pump() == Result::Ok, "pump");
        if (client.state() == "Connected" && sent < 128) {
            while (sent < 128 && client.send(0, {uint8_t(sent)}, 0, 4) == Result::Ok) ++sent;
        }
        if (server.statistics().rejected_messages || client.state() == "Disconnected") break;
        if (received == 128) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "sent=" << sent << " received=" << received << " rejected=" << server.statistics().rejected_messages << " state=" << client.state() << std::endl;
    check(server.statistics().rejected_messages == 0 && client.state() == "Connected", "legal admitted bursts do not become receiver abuse under jitter");
    check(received == 128, "all zero-loss packets arrive");
    check(server.stop() == Result::Ok && client.stop() == Result::Ok, "jitter cleanup");
}

void bounded_delivery(bool large) {
    Options receiver; receiver.allow_insecure_loopback = true;
    receiver.messages_per_second = 32;
    receiver.bytes_per_second = large ? 8192 : 4 * 1024 * 1024;
    Options sender = receiver;
    sender.messages_per_second = 1000; sender.bytes_per_second = 4 * 1024 * 1024;
    Session server(receiver), client(sender);
    std::array<int, 4> channels{};
    int applications = 0, received = 0;
    const int count = large ? 11 : 128;
    auto accept = [&](const std::vector<uint8_t> &data, int channel, bool application) {
        check(data.size() == size_t(large ? 4096 : 1), "payload size");
        check(data[0] == uint8_t(application ? 8 + applications : channel * (large ? 2 : 32) + channels[channel]), "per-channel reliable order and exact payload");
        if (large) check(data.back() == 0xA5, "full fragmented payload integrity");
        if (application) ++applications; else ++channels[channel];
        ++received;
    };
    server.packet_received = [&](int64_t, const std::vector<uint8_t> &data, int channel, int delivery) {
        check(delivery == 2, "reliable delivery"); accept(data, channel, false);
    };
    server.application_received = [&](int64_t, const std::vector<uint8_t> &data) { accept(data, 0, true); };
    check(server.listen(0, "127.0.0.1") == Result::Ok, "budget listen");
    check(client.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "budget connect");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(18);
    bool sent = false, first_window_checked = false;
    auto first_delivery = end;
    while (std::chrono::steady_clock::now() < end && received < count) {
        const int before = received;
        check(server.pump() == Result::Ok && client.pump() == Result::Ok, "bounded pump");
        check(received - before <= (large ? 1 : 32), "per-pump callback work is bounded");
        if (!sent && client.state() == "Connected") {
            for (int channel = 0; channel < 4; ++channel)
                for (int i = 0; i < (large ? 2 : 32); ++i) {
                    std::vector<uint8_t> payload(large ? 4096 : 1, 0xA5);
                    payload[0] = uint8_t(channel * (large ? 2 : 32) + i);
                    check(client.send(0, payload, channel, 2) == Result::Ok, "sender admits bounded reliable burst");
                }
            if (large)
                for (int i = 0; i < 3; ++i) {
                    std::vector<uint8_t> payload(4096, 0xA5); payload[0] = uint8_t(8 + i);
                    check(client.send(0, payload, 0, 2, true) == Result::Ok, "application admission");
                }
            sent = true;
        }
        if (received && first_delivery == end) first_delivery = std::chrono::steady_clock::now();
        if (received && std::chrono::steady_clock::now() - first_delivery < std::chrono::milliseconds(200))
            check(received <= (large ? 1 : 32), "incoming count/estimated-byte quota holds across repeated pumps");
        if (!large && received == 32 && !first_window_checked) {
            for (int n : channels) check(n > 0, "all four channels progress before the first receive quota fills");
            first_window_checked = true;
        }
        check(server.statistics().rejected_messages == 0 && (!sent || client.state() == "Connected"), "backpressure preserves valid connection");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(received == count, "queued traffic drains within the bounded deadline");
    if (!large) check(first_window_checked, "channel fairness was observed");
    check(server.statistics().received_messages == uint64_t(count), "statistics count delivered messages");
    check(server.statistics().received_bytes == uint64_t(count) * (large ? 4096 : 1), "statistics count payload bytes");
    check(server.stop() == Result::Ok && client.stop() == Result::Ok, "budget cleanup");
}

// An authenticated transport peer attempts server-only replication metadata.
// This exercises the public wire boundary rather than Session's local guards.
struct ForgedMeta : yojimbo::Message {
    int action = 0;
    uint64_t handle = 0, tick = 0;
    template <typename Stream> bool Serialize(Stream &stream) {
        serialize_int(stream, action, 0, 2);
        serialize_uint64(stream, handle); serialize_uint64(stream, tick);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
struct UnusedBlock : yojimbo::BlockMessage {
    template <typename Stream> bool Serialize(Stream &) { return true; }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};
YOJIMBO_MESSAGE_FACTORY_START(AttackFactory, 3);
YOJIMBO_DECLARE_MESSAGE_TYPE(0, ForgedMeta);
YOJIMBO_DECLARE_MESSAGE_TYPE(1, UnusedBlock);
YOJIMBO_DECLARE_MESSAGE_TYPE(2, UnusedBlock);
YOJIMBO_MESSAGE_FACTORY_FINISH();
struct AttackAdapter : yojimbo::Adapter {
    yojimbo::MessageFactory *CreateMessageFactory(yojimbo::Allocator &allocator) override {
        return YOJIMBO_NEW(allocator, AttackFactory, allocator);
    }
};
void unauthorized_wire() {
    Options options; options.messages_per_second = 32; options.bytes_per_second = 8192;
    Session server(options);
    check(server.listen(0, "127.0.0.1") == Result::Ok, "wire rejection listen");
    std::vector<uint8_t> token;
    check(server.issue_token(30000, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "authenticated attacker admission");
    yojimbo::ClientServerConfig config;
    config.protocolId = std::stoull(server.fingerprint(), nullptr, 16);
    config.numChannels = 10;
    for (int i = 0; i < 10; ++i) {
        config.channel[i].type = i >= 3 && i % 2 == 1 ? yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED : yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
        config.channel[i].maxBlockSize = config.channel[i].type == yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED ? 4097 : 901;
        config.channel[i].blockFragmentSize = 1000;
        config.channel[i].messageSendQueueSize = config.channel[i].messageReceiveQueueSize = 128;
        config.channel[i].maxMessagesPerPacket = 32;
    }
    AttackAdapter adapter;
    const auto clock = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    yojimbo::Client attacker(yojimbo::GetDefaultAllocator(), yojimbo::Address("127.0.0.1", uint16_t(0)), config, adapter, clock());
    check(attacker.Connect(30000, token.data()), "attacker token accepted by transport");
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool sent = false;
    while (std::chrono::steady_clock::now() < end && !server.statistics().rejected_messages) {
        check(server.pump() == Result::Ok, "server remains healthy after rejected wire traffic");
        attacker.AdvanceTime(clock()); attacker.ReceivePackets();
        if (!sent && attacker.IsConnected()) {
            auto *message = attacker.CreateMessage(0);
            check(message != nullptr, "forged message allocation");
            attacker.SendMessage(0, message); sent = true;
        }
        attacker.SendPackets();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::cout << "wire_sent=" << sent << " wire_rejected=" << server.statistics().rejected_messages << " wire_peers=" << server.peers().size() << " attacker_state=" << attacker.GetClientState() << std::endl;
    check(sent && server.statistics().rejected_messages == 1 && server.peers().empty(), "authenticated client replication metadata is rejected and peer removed");
    attacker.Disconnect();
    Session recovery(options);
    token.clear();
    check(server.issue_token(30001, "127.0.0.1:" + std::to_string(server.statistics().local_port), token) == Result::Ok, "recovery admission");
    check(recovery.connect_token(30001, token) == Result::Ok, "recovery join");
    const auto recovery_end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (recovery.state() != "Connected" && std::chrono::steady_clock::now() < recovery_end) {
        check(server.pump() == Result::Ok && recovery.pump() == Result::Ok, "recovery pump");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(recovery.state() == "Connected" && server.peers().size() == 1, "valid peer connects after wire rejection");
    check(recovery.stop() == Result::Ok && server.stop() == Result::Ok, "wire cleanup");
}

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--wire-only") { unauthorized_wire(); return 0; }
    jitter_bursts(); bounded_delivery(false); bounded_delivery(true); unauthorized_wire();
    std::cout << "EGP_RECEIVE_BUDGET_CHECKS=passed jitter_messages=128 reliable_messages=128 fragmented_messages=11 channels=4 application_channel=1 unauthorized_wire_rejections=1 recovery=1" << std::endl;
}
