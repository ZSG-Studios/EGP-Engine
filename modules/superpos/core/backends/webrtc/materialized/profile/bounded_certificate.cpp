// SPDX-License-Identifier: MIT
#include "bounded_certificate.hpp"
#include "certificate_task.hpp"
#include "init.hpp"
#include "backing_allocator.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace rtc::impl::bounded_certificate {
namespace ta = ::superpos::task_admission;
namespace {
std::atomic<Runtime*> active{};
using Tasks=ta::Scheduler<1,8,8,256,16384>;
}
struct Runtime::State {
    enum class Phase : unsigned { Free,Reserved,Live,Retire,Abandoning };
    struct Cell { std::atomic<Phase> phase{Phase::Free}; std::atomic<Certificate*> value{}; retirement::Token lifetime,completion; };
    Runtime* runtime;
    superpos::Allocator& backing;
    retirement::Domain retirement;
    const std::thread::id owner=std::this_thread::get_id();
    // Retain global crypto/provider initialization before Generate executes,
    // independently of whether any PeerConnection already exists.
    const init_token provider_token=Init::Instance().token();
    Tasks tasks;
    ta::Owner task_owner{};
    std::array<Cell,16> cells{};
    std::atomic_flag retire_gate=ATOMIC_FLAG_INIT;
    alignas(Results) std::array<std::byte,sizeof(Results)> storage{};
    std::atomic<Results*> results{};
    std::thread lifecycle;
    std::thread::id lifecycle_id;
    std::mutex sleep_mutex;
    std::condition_variable wake;
    std::atomic<bool> stop{},done{};
    std::optional<Results::Ticket> warm;
    bool closed{};
    std::atomic<std::size_t> live{},deleted{},generated{},wrong_thread{};
    explicit State(Runtime* r,superpos::Allocator& parent):runtime(r),backing(parent),retirement(parent) {
        auto pin=[]{ };
        Tasks::Task lifetime;
        if(lifetime.assign(pin)!=ta::Admission::Accepted) throw Failure(cc::Error::Unsupported);
        auto registered=tasks.try_register(ta::AssociationRole::State,{8,8,16384,4096},lifetime);
        if(registered.status!=ta::Admission::Accepted) throw Failure(cc::task_error(registered.status));
        task_owner=registered.owner;
        try {
            lifecycle=std::thread([this]{run();});
            // Construction alone may wait. Callback/admission paths never wait.
            std::unique_lock lock(sleep_mutex);
            wake.wait(lock,[this]{return results.load(std::memory_order_acquire)!=nullptr;});
        } catch (...) {
            // A started lifecycle worker must finish before member destruction
            // and before Runtime returns the partially constructed backing.
            tasks.begin_shutdown();stop.store(true,std::memory_order_release);wake.notify_all();
            if(lifecycle.joinable())lifecycle.join();
            throw;
        }
    }
    ~State() { if(!done.load(std::memory_order_acquire)) std::terminate(); if(lifecycle.joinable()) lifecycle.join(); }
    void run() noexcept {
        lifecycle_id=std::this_thread::get_id();
        auto* pool=::new(storage.data()) Results;
        results.store(pool,std::memory_order_release); wake.notify_all();
        for(;;) {
            if(stop.load(std::memory_order_acquire)) pool->close();
            auto drained=pool->drain();
            for(auto& cell:cells) {
                if(cell.phase.load(std::memory_order_acquire)!=Phase::Retire) continue;
                auto* pointer=cell.value.exchange(nullptr,std::memory_order_acq_rel);
                if(!pointer) std::terminate();
                if(std::this_thread::get_id()!=lifecycle_id) wrong_thread.fetch_add(1);
                std::destroy_at(pointer); backing.deallocate(pointer);
                retirement::Domain::deleted(cell.completion);cell.completion.reset();cell.lifetime.reset();
                deleted.fetch_add(1,std::memory_order_release);
                live.fetch_sub(1,std::memory_order_release);
                cell.phase.store(Phase::Free,std::memory_order_release);
            }
            if(stop.load(std::memory_order_acquire) && drained.complete && live.load(std::memory_order_acquire)==0 && retirement.empty()) break;
            std::unique_lock lock(sleep_mutex);
            wake.wait_for(lock,std::chrono::milliseconds(1));
        }
        pool->~Results(); results.store(nullptr,std::memory_order_release);
        done.store(true,std::memory_order_release); wake.notify_all();
    }
    cc::Result<std::size_t> reserve_retirement() noexcept {
        if(retire_gate.test_and_set(std::memory_order_acquire)) return std::unexpected(cc::Error::Contended);
        std::optional<std::size_t> selected;
        for(std::size_t i=0;i<cells.size();++i) {
            auto expected=Phase::Free;
            if(cells[i].phase.compare_exchange_strong(expected,Phase::Reserved,std::memory_order_acq_rel)) { selected=i; break; }
        }
        retire_gate.clear(std::memory_order_release);
        if(!selected) return std::unexpected(cc::Error::Full);
        auto reserved=retirement.reserve();
        if(reserved.status!=retirement::Status::Ready){cells[*selected].phase.store(Phase::Free,std::memory_order_release);return std::unexpected(cc::Error::Full);}
        cells[*selected].lifetime=std::move(reserved.token);
        live.fetch_add(1,std::memory_order_release); return *selected;
    }
    void abandon(std::size_t slot) noexcept {
        auto expected=Phase::Reserved;
        if(!cells[slot].phase.compare_exchange_strong(expected,Phase::Abandoning,std::memory_order_acq_rel))std::terminate();
        cells[slot].lifetime.reset();
        live.fetch_sub(1,std::memory_order_release);
        cells[slot].phase.store(Phase::Free,std::memory_order_release);wake.notify_all();
    }
    void retire(std::size_t slot,Certificate* pointer) noexcept {
        auto& cell=cells[slot];
        if(cell.phase.load(std::memory_order_acquire)!=Phase::Live || !pointer) std::terminate();
        cell.value.store(pointer,std::memory_order_relaxed);
        cell.phase.store(Phase::Retire,std::memory_order_release); wake.notify_all();
    }
    struct Provider {
        State* state{}; std::size_t slot{};
        Provider(State* s,std::size_t i) noexcept:state(s),slot(i) {}
        Provider(Provider&& other) noexcept:state(std::exchange(other.state,nullptr)),slot(other.slot) {}
        ~Provider() { if(state) state->abandon(slot); }
        certificate_ptr operator()() {
            auto activity=state->cells[slot].lifetime.enter();if(!activity)throw retirement::Failure();
            superpos::rtc_profile::BackingAllocator<Certificate> allocator(state->backing);
            auto* certificate=allocator.allocate(1);
            try{std::construct_at(certificate,Certificate::Generate(CertificateType::Ecdsa,"libdatachannel"));}
            catch(...){allocator.deallocate(certificate,1);throw;}
            state->cells[slot].completion=activity.handoff();
            auto* saved=state; auto index=slot; state=nullptr;
            saved->cells[index].phase.store(Phase::Live,std::memory_order_release);
            // shared_ptr constructor failure invokes this deleter too. The fixed
            // retirement reservation exists before provider execution begins.
            certificate_ptr result(certificate,[saved,index](Certificate* p) noexcept {saved->retire(index,p);},
                superpos::rtc_profile::PinnedBackingAllocator<std::byte>(saved->backing,saved->cells[index].lifetime));
            saved->generated.fetch_add(1,std::memory_order_release); return result;
        }
    };
};
Runtime::Runtime(superpos::Allocator& backing):backing_(backing) {
    auto* memory=backing_.allocate(sizeof(State),alignof(State),superpos::MemoryDomain::Backend);
    if(!memory)throw Failure(cc::Error::OutOfMemory);
    try{state_=std::construct_at(static_cast<State*>(memory),this,backing_);}
    catch(...){backing_.deallocate(memory);throw;}
    Runtime* expected=nullptr;
    if(!active.compare_exchange_strong(expected,this,std::memory_order_acq_rel)) {
        (void)shutdown(std::chrono::steady_clock::now()+std::chrono::seconds(3));
        if(!state_->done.load(std::memory_order_acquire))std::terminate();
        std::destroy_at(state_);backing_.deallocate(state_);state_=nullptr;
        throw Failure(cc::Error::Full);
    }
}
Runtime::~Runtime() { if(!state_->done.load(std::memory_order_acquire)) std::terminate();std::destroy_at(state_);backing_.deallocate(state_); }
cc::Result<void> Runtime::prewarm(CertificateType type) noexcept {
    auto& s=*state_;
    if(std::this_thread::get_id()!=s.owner) return std::unexpected(cc::Error::WrongThread);
    if(s.closed) return std::unexpected(cc::Error::Closed);
    if(type!=CertificateType::Default && type!=CertificateType::Ecdsa) return std::unexpected(cc::Error::Unsupported);
    if(s.warm) return std::unexpected(cc::Error::Unsupported);
    auto slot=s.reserve_retirement(); if(!slot) return std::unexpected(slot.error());
    State::Provider provider(&s,*slot);
    auto result=cc::try_submit(s.tasks,s.task_owner,*s.results.load(std::memory_order_acquire),provider,128*1024);
    if(!result) return std::unexpected(result.error());
    s.warm.emplace(std::move(*result)); return {};
}
cc::Result<Ticket> Runtime::acquire(CertificateType type) noexcept {
    auto& s=*state_;
    if(std::this_thread::get_id()!=s.owner) return std::unexpected(cc::Error::WrongThread);
    if(s.closed) return std::unexpected(cc::Error::Closed);
    if(type!=CertificateType::Default && type!=CertificateType::Ecdsa) return std::unexpected(cc::Error::Unsupported);
    if(!s.warm) return std::unexpected(cc::Error::NotReady);
    auto ready=s.warm->poll(); if(!ready) return std::unexpected(ready.error());
    auto result=s.warm->clone(); if(!result) return std::unexpected(result.error());
    return Ticket(std::move(*result));
}
cc::Result<Ticket> Runtime::acquire_active(CertificateType type) noexcept {
    auto* runtime=active.load(std::memory_order_acquire);
    if(!runtime) return std::unexpected(cc::Error::Closed);
    // Activation/destruction are host-owner operations with external quiescence.
    return runtime->acquire(type);
}
cc::Result<void> Runtime::shutdown(std::chrono::steady_clock::time_point deadline) noexcept {
    auto& s=*state_;
    if(std::this_thread::get_id()!=s.owner) return std::unexpected(cc::Error::WrongThread);
    Runtime* expected=this; active.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
    if(!s.closed) {s.closed=true; s.warm.reset(); s.tasks.begin_shutdown();}
    if(s.tasks.wait_shutdown(deadline)!=ta::Wait::Complete) return std::unexpected(cc::Error::NotReady);
    s.stop.store(true,std::memory_order_release); s.wake.notify_all();
    while(!s.done.load(std::memory_order_acquire)) {
        if(std::chrono::steady_clock::now()>=deadline) return std::unexpected(cc::Error::NotReady);
        std::unique_lock lock(s.sleep_mutex); s.wake.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)));
    }
    if(s.lifecycle.joinable()) s.lifecycle.join(); return {};
}
Runtime::Metrics Runtime::metrics() const noexcept { auto& s=*state_; return {s.live.load(),s.deleted.load(),s.generated.load(),s.wrong_thread.load()}; }
} // namespace rtc::impl::bounded_certificate
