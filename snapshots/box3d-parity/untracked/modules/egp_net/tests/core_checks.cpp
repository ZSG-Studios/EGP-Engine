#include "net_core.h"
#include "yojimbo.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace egp::net;
int checks = 0;
void check(bool good, const char *name) { if (!good) { std::cerr << "FAILED: " << name << std::endl; std::exit(1); } ++checks; }
template <typename Predicate> bool until(Session &server, Session &client, Predicate done, int milliseconds = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { server.pump(); client.pump(); if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    while (std::chrono::steady_clock::now() < end);
    return false;
}
int main() {
    yojimbo_set_assert_function([](const char *condition, const char *function, const char *file, int line) { std::cerr << file << ':' << line << " " << function << " assert " << condition << std::endl; std::exit(2); });
    Options bad; bad.tick_rate = 0; Session invalid(bad); check(invalid.validation() == Result::Invalid, "reject invalid configuration");
    Options excessive_players; excessive_players.max_players = yojimbo::MaxClients + 1;
    Session excessive(excessive_players); check(excessive.validation() == Result::Invalid, "reject player count beyond Yojimbo capacity");
    Options o; o.allow_insecure_loopback = true; o.max_entities = 2;
    Session server(o), client(o);
    check(server.listen(0, "0.0.0.0") == Result::Invalid, "development listener confined to loopback");
    check(client.connect_loopback("192.0.2.1", 1234) == Result::Unauthorized, "development joins confined to loopback");
    check(server.listen(0, "127.0.0.1") == Result::Ok, "listen on ephemeral UDP port");
    check(client.connect_loopback("127.0.0.1", server.statistics().local_port) == Result::Ok, "start client");
    check(until(server, client, [&] { return client.state() == "Connected" && server.peers().size() == 1; }), "authenticated UDP baseline");
    auto peer = server.peers()[0].id;
    uint64_t entity = 0, second = 0, excess = 0;
    check(server.spawn(7, {1,2,3}, peer, entity) == Result::Ok, "server spawn");
    check(until(server, client, [&] { return client.entities().count(entity); }), "replicate entity");
    check(client.entities().at(entity).authority_peer == peer, "replicate authority");
    check(client.update(entity, {9}) == Result::Unauthorized, "client cannot mutate authoritative state");
    check(server.update(entity, {4,5}) == Result::Ok, "update state");
    check(until(server, client, [&] { return client.entities().at(entity).state == std::vector<uint8_t>({4,5}); }), "revision replication");
    check(server.spawn(8, {}, -1, second) == Result::Ok && server.spawn(9, {}, -1, excess) == Result::Full, "entity admission budget");
    check(server.set_visible(entity, peer, false) == Result::Ok, "interest hide");
    check(until(server, client, [&] { return !client.entities().count(entity); }), "hidden entity removed");
    server.set_visible(entity, peer, true);
    check(until(server, client, [&] { return client.entities().count(entity); }), "interest reentry baseline");
    int packets = 0; server.packet_received = [&](int64_t source, const std::vector<uint8_t> &data, int channel, int delivery) {
        check(source == peer && data == std::vector<uint8_t>({42}) && channel >= 0 && channel < 4 && (delivery == 2 || delivery == 4), "packet metadata"); ++packets;
    };
    for (int channel=0; channel<4; ++channel) for (int delivery : {2,4}) check(client.send(0,{42},channel,delivery) == Result::Ok, "raw enqueue");
    check(until(server, client, [&] { return packets == 8; }), "all native raw channels");
    check(client.send(0,{},4,2) == Result::Invalid && client.send(0,{},0,1) == Result::Invalid, "unsupported channel or delivery rejected");
    check(client.send(0,std::vector<uint8_t>(901),0,4) == Result::Full, "unreliable size budget");
    Result wrong_thread = Result::Ok; std::thread worker([&] { wrong_thread = server.update(entity, {}); }); worker.join();
    check(wrong_thread == Result::Busy, "owner thread enforcement");
    client.stop(); check(until(server, client, [&] { return server.peers().empty(); }), "disconnect observed");
    check(server.entities().at(entity).authority_peer == -1, "ownership revoked");
    client.connect_loopback("127.0.0.1", server.statistics().local_port);
    check(until(server, client, [&] { return client.state() == "Connected" && client.entities().size() == 2; }), "reconnect baseline");
    check(server.peers()[0].id != peer && client.entities().at(entity).authority_peer == -1, "reused slot cannot inherit authority");
    server.despawn(second); check(until(server, client, [&] { return !client.entities().count(second); }), "despawn replication");
    client.stop(); server.stop();
    Options secure; Session secure_server(secure), secure_client(secure);
    check(secure_server.listen(0,"0.0.0.0") == Result::Ok, "secure wildcard server start");
    std::vector<uint8_t> token;
    check(secure_server.issue_token(1001,"127.0.0.1:" + std::to_string(secure_server.statistics().local_port),token) == Result::Ok && token.size() == 2048, "issue encrypted connect token");
    check(secure_client.connect_token(1001,token) == Result::Ok, "secure token join");
    check(until(secure_server,secure_client,[&]{return secure_client.state()=="Connected";}), "secure encrypted UDP connection");
    check(secure_server.peers()[0].client_id == 1001, "authenticated client identity");
    secure_client.stop(); secure_server.stop();
    Options loss = o; loss.simulated_loss = 15; loss.simulated_latency_ms = 30; loss.simulated_jitter_ms = 10;
    Session lossy_server(loss), lossy_client(loss);
    check(lossy_server.listen(0,"127.0.0.1") == Result::Ok, "loss simulator listener");
    lossy_client.connect_loopback("127.0.0.1",lossy_server.statistics().local_port);
    check(until(lossy_server,lossy_client,[&]{return lossy_client.state()=="Connected";},8000), "baseline under loss/latency/jitter");
    uint64_t large = 0; std::vector<uint8_t> block(4096,77);
    lossy_server.spawn(1,block,-1,large);
    check(until(lossy_server,lossy_client,[&]{return lossy_client.entities().count(large) && lossy_client.entities().at(large).state==block;},8000), "fragmented state under packet loss");
    check(lossy_server.update(large,std::vector<uint8_t>(4097))==Result::Full,"state size bound");
    int applications = 0;
    lossy_server.application_received = [&](int64_t, const std::vector<uint8_t> &data) { check(data==block,"application block contents"); ++applications; lossy_server.stop(); };
    check(lossy_client.send(0,block,0,2,true)==Result::Ok,"large application enqueue");
    check(until(lossy_server,lossy_client,[&]{return applications==1 && lossy_server.state()=="Stopped";},8000),"stop safely inside receive callback");
    lossy_client.stop();
    Options rate=o;rate.messages_per_second=32;
    Session limited(rate), limited_client(rate); limited.listen(0,"127.0.0.1"); limited_client.connect_loopback("127.0.0.1",limited.statistics().local_port);
    check(until(limited,limited_client,[&]{return limited_client.state()=="Connected";}),"budget fixture connection");
    for(int i=0;i<32;++i) check(limited_client.send(0,{1})==Result::Ok,"bounded application enqueue");
    check(limited_client.send(0,{1})==Result::Busy,"outgoing per-second rate bound");
    limited.peer_disconnected = [&](int64_t) { limited.stop(); };
    check(limited.disconnect(limited.peers()[0].id)==Result::Ok && limited.state()=="Stopped","stop safely inside immediate disconnect callback");
    limited_client.stop();limited.stop();
    Session ipv6_server(o),ipv6_client(o);
    check(ipv6_server.listen(0,"::1")==Result::Ok,"IPv6 loopback listener");
    check(ipv6_client.connect_loopback("::1",ipv6_server.statistics().local_port)==Result::Ok,"IPv6 join");
    check(until(ipv6_server,ipv6_client,[&]{return ipv6_client.state()=="Connected";}),"IPv6 replicated baseline");
    ipv6_client.stop();ipv6_server.stop();
    Session ipv6_secure_server(secure),ipv6_secure_client(secure);
    check(ipv6_secure_server.listen(0,"::1")==Result::Ok,"IPv6 secure listener");
    std::vector<uint8_t> ipv6_token;
    check(ipv6_secure_server.issue_token(2002,"[::1]:"+std::to_string(ipv6_secure_server.statistics().local_port),ipv6_token)==Result::Ok,"IPv6 secure token");
    check(ipv6_secure_client.connect_token(2002,ipv6_token,"::")==Result::Ok,"IPv6 secure join");
    check(until(ipv6_secure_server,ipv6_secure_client,[&]{return ipv6_secure_client.state()=="Connected";}),"IPv6 secure baseline");
    ipv6_secure_client.stop();ipv6_secure_server.stop();
    Options incompatible = o; incompatible.game_protocol = "different-game"; incompatible.timeout_seconds = 1;
    Session incompatible_client(incompatible), compatibility_server(o);
    compatibility_server.listen(0,"127.0.0.1"); incompatible_client.connect_loopback("127.0.0.1",compatibility_server.statistics().local_port);
    check(until(compatibility_server,incompatible_client,[&]{return incompatible_client.state()=="Disconnected";},2500),"protocol mismatch rejected");
    check(compatibility_server.peers().empty(),"mismatched protocol creates no authority");
    compatibility_server.stop();
    Options rejection = secure; rejection.timeout_seconds = 1;
    Session rejection_server(rejection),rejection_client(rejection);
    rejection_server.listen(0,"127.0.0.1");
    std::vector<uint8_t> corrupted;
    rejection_server.issue_token(3003,"127.0.0.1:"+std::to_string(rejection_server.statistics().local_port),corrupted);
    corrupted[500] ^= 0x80;
    check(rejection_client.connect_token(3003,corrupted)==Result::Ok,"corrupted token begins asynchronous validation");
    check(until(rejection_server,rejection_client,[&]{return rejection_client.state()=="Disconnected";},2500),"corrupted token authentication rejected");
    check(rejection_server.peers().empty(),"tampered token creates no peer");
    rejection_client.stop();rejection_server.stop();
    std::cout << "EGP_NATIVE_NETWORK_CHECKS=" << checks << std::endl;
}
