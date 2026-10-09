#include "net_core.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
static std::atomic<int64_t> live_allocations{0};
void *operator new(std::size_t size) {
    void *pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    ++live_allocations; return pointer;
}
void operator delete(void *pointer) noexcept { if (pointer) { --live_allocations; std::free(pointer); } }
void operator delete(void *pointer, std::size_t) noexcept { ::operator delete(pointer); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { ::operator delete(pointer); }
int main() {
    using namespace egp::net;
    Options options; options.allow_insecure_loopback = true; options.max_players = options.max_entities = 1;
    Session server(options), client(options);
    if (server.listen(0,"127.0.0.1") != Result::Ok || client.connect_loopback("127.0.0.1",server.statistics().local_port) != Result::Ok) return 2;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (client.state() != "Connected" && std::chrono::steady_clock::now() < deadline) {
        server.pump(); client.pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (client.state() != "Connected") return 3;
    const auto peer = server.peers()[0].id;
    const auto before = live_allocations.load();
    // Long-lived peers stream many entities even when only one entity is alive at a time.
    for (int i = 0; i < 1024; ++i) {
        uint64_t handle = 0;
        if (server.spawn(1,{},-1,handle) != Result::Ok || server.set_visible(handle,peer,false) != Result::Ok || server.despawn(handle) != Result::Ok) return 4;
    }
    const auto retained = live_allocations.load() - before;
    std::cout << "EGP_INTEREST_RETAINED_ALLOCATIONS=" << retained << std::endl;
    return retained == 0 ? 0 : 1;
}
