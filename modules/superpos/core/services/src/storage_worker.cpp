#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "superpos/storage_worker.hpp"
#include <cstring>
#include <cstdlib>
#include <new>
#include <optional>
#include <thread>
#include <utility>
#include <cerrno>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#elif defined(__linux__)
#include <pthread.h>
#include <time.h>
#endif
namespace superpos::service {
struct NativeStorageWorker::Impl {
    Allocator* allocator{};
    std::thread::id owner{std::this_thread::get_id()};
    std::optional<JournalExecutor> queue;
    StorageWorkerConfig config{};
    char path[1024]{};
    std::atomic<StorageWorkerPhase> phase{StorageWorkerPhase::Opening};
    std::atomic_bool stop{},opened{};
    Error error{Error::None}; // Published by phase release; immutable thereafter.
#ifdef _WIN32
    HANDLE thread{};
#elif defined(__linux__)
    pthread_t thread{};
    bool started{};
#endif
    static void idle() noexcept {
#ifdef _WIN32
        Sleep(1);
#elif defined(__linux__)
        timespec delay{0,1000000};(void)nanosleep(&delay,nullptr);
#endif
    }
    static void run(Impl& self) noexcept {
        {
            auto store=SqliteJournal::open(*self.allocator,self.path,self.config.journal);
            if(!store){self.error=store.error();self.phase.store(StorageWorkerPhase::Failed,std::memory_order_release);return;}
            if(self.config.checkpoint_digest)if(auto bound=store->bind_checkpoint_digest(*self.config.checkpoint_digest);!bound){self.error=bound.error();self.phase.store(StorageWorkerPhase::Failed,std::memory_order_release);return;}
            self.opened.store(true,std::memory_order_release);
            self.phase.store(StorageWorkerPhase::Running,std::memory_order_release);
            for(;;){
                // Stop publishes the owner's admission seal and all earlier
                // Queued jobs. The decisive empty observation must follow that
                // acquire; an earlier empty result cannot prove a drained queue.
                const bool stopping=self.stop.load(std::memory_order_acquire);
                auto step=self.queue->worker_step(*store);
                if(!step){self.error=step.error();self.phase.store(StorageWorkerPhase::Failed,std::memory_order_release);return;}
                if(!*step){
                    if(stopping)break;idle();
                }
            }
        } // Close the owner-thread SQLite connection before completion publication.
        self.phase.store(StorageWorkerPhase::Stopped,std::memory_order_release);
    }
#ifdef _WIN32
    static unsigned __stdcall entry(void* context) noexcept {run(*static_cast<Impl*>(context));return 0;}
#elif defined(__linux__)
    static void* entry(void* context) noexcept {run(*static_cast<Impl*>(context));return nullptr;}
#endif
};
NativeStorageWorker::NativeStorageWorker(Impl* p) noexcept:impl_(p){}
NativeStorageWorker::NativeStorageWorker(NativeStorageWorker&& other) noexcept:impl_(std::exchange(other.impl_,nullptr)){}
NativeStorageWorker& NativeStorageWorker::operator=(NativeStorageWorker&& other) noexcept {
    if(this!=&other){destroy();impl_=std::exchange(other.impl_,nullptr);}return *this;
}
NativeStorageWorker::~NativeStorageWorker(){destroy();}
void NativeStorageWorker::destroy() noexcept {
    if(!impl_)return;
    // Never release shared storage without the owner/join lifetime proof.
    if(std::this_thread::get_id()!=impl_->owner)std::abort();
    (void)impl_->queue->stop_admission();impl_->stop.store(true,std::memory_order_release);
#ifdef _WIN32
    if(impl_->thread){if(WaitForSingleObject(impl_->thread,INFINITE)!=WAIT_OBJECT_0)std::abort();CloseHandle(impl_->thread);}
#elif defined(__linux__)
    if(impl_->started&&pthread_join(impl_->thread,nullptr))std::abort();
#endif
    auto* allocator=impl_->allocator;impl_->~Impl();allocator->deallocate(impl_);impl_=nullptr;
}
Result<NativeStorageWorker> NativeStorageWorker::create(Allocator& allocator,std::string_view path,StorageWorkerConfig config) noexcept {
#if !defined(_WIN32) && !defined(__linux__)
    return fail(Error::Unsupported);
#else
    if(path.empty()||path.size()>=1024||path.find('\0')!=std::string_view::npos)return fail(Error::InvalidArgument);
#ifdef _WIN32
    const bool drive=path.size()>=3&&((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))&&path[1]==':'&&(path[2]=='/'||path[2]=='\\');
    const bool unc=path.size()>4&&path[0]=='\\'&&path[1]=='\\';
    if(!drive&&!unc)return fail(Error::InvalidArgument);
#else
    if(path[0]!='/')return fail(Error::InvalidArgument);
#endif
    auto* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);if(!memory)return fail(Error::OutOfMemory);
    auto* impl=new(memory) Impl;impl->allocator=&allocator;impl->config=config;std::memcpy(impl->path,path.data(),path.size());
    auto queue=JournalExecutor::create(allocator,config.queue);
    if(!queue){impl->~Impl();allocator.deallocate(impl);return fail(queue.error());}
    impl->queue.emplace(std::move(*queue));
    NativeStorageWorker result(impl);
#ifdef _WIN32
    // Fallible CRT-aware native launch; no throwing std::thread constructor.
    const auto handle=_beginthreadex(nullptr,512*1024,&Impl::entry,impl,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if(!handle)return fail(Error::Io);impl->thread=reinterpret_cast<HANDLE>(handle);
#else
    pthread_attr_t attributes;
    if(pthread_attr_init(&attributes))return fail(Error::Io);
    const auto configured=pthread_attr_setstacksize(&attributes,512*1024);
    const auto launched=configured?configured:pthread_create(&impl->thread,&attributes,&Impl::entry,impl);
    (void)pthread_attr_destroy(&attributes);
    if(launched)return fail(Error::Io);impl->started=true;
#endif
    return result;
#endif
}
Result<StorageWorkerPhase> NativeStorageWorker::phase() const noexcept {
    if(!impl_)return fail(Error::NotReady);if(std::this_thread::get_id()!=impl_->owner)return fail(Error::PermissionDenied);
    auto phase=impl_->phase.load(std::memory_order_acquire);
    if(impl_->stop.load(std::memory_order_acquire)&&(phase==StorageWorkerPhase::Opening||phase==StorageWorkerPhase::Running))return StorageWorkerPhase::Closing;return phase;
}
Result<Error> NativeStorageWorker::failure() const noexcept {
    auto state=phase();if(!state)return fail(state.error());if(*state!=StorageWorkerPhase::Failed)return Error::None;return impl_->error;
}
Result<JournalExecutor*> NativeStorageWorker::queue() noexcept {
    auto state=phase();if(!state)return fail(state.error());
    if(*state==StorageWorkerPhase::Failed||!impl_->opened.load(std::memory_order_acquire))return fail(Error::NotReady);return &*impl_->queue;
}
Status NativeStorageWorker::request_stop() noexcept {
    auto state=phase();if(!state)return fail(state.error());auto seal=impl_->queue->stop_admission();if(!seal)return seal;
    impl_->stop.store(true,std::memory_order_release);return {};
}
Status NativeStorageWorker::poll_join(std::uint32_t wait_ms) noexcept {
    auto state=phase();if(!state)return fail(state.error());if(wait_ms>1000)return fail(Error::InvalidArgument);
#ifdef _WIN32
    if(!impl_->thread)return {};const auto wait=WaitForSingleObject(impl_->thread,wait_ms);
    if(wait==WAIT_TIMEOUT)return fail(Error::Busy);if(wait!=WAIT_OBJECT_0)return fail(Error::Io);
    if(!CloseHandle(impl_->thread))return fail(Error::Io);impl_->thread=nullptr;return {};
#elif defined(__linux__)
    if(!impl_->started)return {};
    timespec beginning{};
    if(wait_ms&&clock_gettime(CLOCK_MONOTONIC,&beginning))return fail(Error::Io);
    if(wait_ms&&(beginning.tv_sec<0||beginning.tv_sec>std::numeric_limits<time_t>::max()-2))return fail(Error::Overflow);
    for(;;){
        const auto joined=pthread_tryjoin_np(impl_->thread,nullptr);
        if(!joined){impl_->started=false;return {};}
        if(joined!=EBUSY)return fail(Error::Io);
        if(!wait_ms)return fail(Error::Busy);
        timespec current{};if(clock_gettime(CLOCK_MONOTONIC,&current))return fail(Error::Io);
        if(current.tv_sec<beginning.tv_sec||(current.tv_sec==beginning.tv_sec&&current.tv_nsec<beginning.tv_nsec))return fail(Error::Io);
        if(current.tv_sec>beginning.tv_sec+1)return fail(Error::Busy);
        const auto elapsed=(current.tv_sec-beginning.tv_sec)*1000000000LL+current.tv_nsec-beginning.tv_nsec;
        if(elapsed>=static_cast<long long>(wait_ms)*1000000LL)return fail(Error::Busy);
        Impl::idle();
    }
#else
    return fail(Error::Unsupported);
#endif
}
}
