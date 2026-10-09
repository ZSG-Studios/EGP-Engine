#include "module_memory.hpp"
#include <atomic>
#include <memory>
#include <thread>
namespace superpos_egp { namespace {
struct RootStorage {
    std::atomic<unsigned> phase{};
    alignas(superpos::BudgetAllocator) std::byte storage[sizeof(superpos::BudgetAllocator)];
};
// Root bookkeeping is fixed Backend storage, counted outside the heap ledger.
// Reduce the parent domain ceiling by this exact amount, including padding.
RootStorage root;
constexpr std::size_t ceiling=std::size_t(1280)<<20;
static_assert(sizeof(RootStorage)<(std::size_t(64)<<20));
superpos::MemoryPlan charged_plan() noexcept {
    superpos::MemoryPlan plan;
    plan.limits[unsigned(superpos::MemoryDomain::Backend)]-=sizeof(RootStorage);
    return plan;
}
}
superpos::BudgetAllocator& module_backing() noexcept {
    unsigned zero=0;
    if(root.phase.compare_exchange_strong(zero,1,std::memory_order_acq_rel)) {
        std::construct_at(reinterpret_cast<superpos::BudgetAllocator*>(root.storage),charged_plan());
        root.phase.store(2,std::memory_order_release);
    } else while(root.phase.load(std::memory_order_acquire)!=2) std::this_thread::yield();
    return *std::launder(reinterpret_cast<superpos::BudgetAllocator*>(root.storage));
}
superpos::Status module_memory_configuration() noexcept {
    auto declared=superpos::MemoryPlan{}.validate(ceiling);
    if(!declared)return declared;
    auto bounded=charged_plan().validate(ceiling-sizeof(RootStorage));
    if(!bounded)return bounded;
    return module_backing().configuration_status();
}
std::size_t module_static_bytes() noexcept { return sizeof(RootStorage); }
std::size_t module_aggregate_bytes() noexcept { return sizeof(RootStorage)+module_backing().total(); }
}
