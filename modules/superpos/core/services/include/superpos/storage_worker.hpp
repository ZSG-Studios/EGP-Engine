#pragma once
#include "superpos/journal_executor.hpp"
#include <atomic>
#include <string_view>
namespace superpos::service {
enum class StorageWorkerPhase { Opening, Running, Closing, Stopped, Failed };
struct StorageWorkerConfig {
    JournalConfig journal{};
    JournalQueueConfig queue{};
    // Optional trusted checkpoint digest bound on the worker connection before
    // Running. It is then used only by the worker thread; it must outlive the
    // worker and must not be used concurrently by any other thread.
    CryptographicDigest* checkpoint_digest{};
};
// Windows/Linux native providers; others reject creation as Unsupported. Allocator/fault hook must be thread-safe and outlive
// this object. Owner serializes all public calls and queue producer/consumer work.
// Borrowed queue must not move or be recreated, destroyed, or worker-stepped by
// another dispatcher. Failed publishes an error, not proof of native thread exit.
// Destruction requests drain and joins; call request_stop/poll_join beforehand
// to avoid waiting inside destruction. OS/storage stalls cannot be force-killed.
class NativeStorageWorker {
    struct Impl; Impl* impl_{};
    explicit NativeStorageWorker(Impl*) noexcept;
    void destroy() noexcept;
public:
    static Result<NativeStorageWorker> create(Allocator&,std::string_view absolute_utf8_path,StorageWorkerConfig={}) noexcept;
    NativeStorageWorker(const NativeStorageWorker&)=delete;
    NativeStorageWorker& operator=(const NativeStorageWorker&)=delete;
    NativeStorageWorker(NativeStorageWorker&&) noexcept;
    NativeStorageWorker& operator=(NativeStorageWorker&&) noexcept;
    ~NativeStorageWorker();
    Result<StorageWorkerPhase> phase() const noexcept;
    Result<Error> failure() const noexcept;
    Result<JournalExecutor*> queue() noexcept;
    Status request_stop() noexcept;
    Status poll_join(std::uint32_t wait_ms=0) noexcept;
};
}
