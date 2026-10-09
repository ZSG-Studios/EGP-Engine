#pragma once
#include "superpos/result.hpp"
#include "superpos/sqlite_journal.hpp"
#include <sqlite3.h>
#include <cstdint>
namespace superpos::detail {
// One private VFS per store. All operations delegate to the qualified host VFS;
// WAL writes/truncations are refused before crossing the explicit byte ceiling.
struct BoundedVfs {
    sqlite3_vfs table{};
    sqlite3_vfs* source{};
    std::uint64_t limit{};
    char name[64]{};
    bool registered{};
    StorageFaultInjector* fault_injector{};
    Status initialize(std::uint64_t) noexcept;
    ~BoundedVfs();
};
}
