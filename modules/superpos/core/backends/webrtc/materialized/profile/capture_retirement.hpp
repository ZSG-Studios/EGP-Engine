// Original Superpos experimental capture-retirement layer. MIT licensed.
#pragma once
#include "task_admission.hpp"
#include "retirement.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

namespace superpos::processor_admission {
namespace ta = ::superpos::task_admission;
using Admission = ta::Admission;
using TaskClass = ta::TaskClass;
using AssociationRole = ta::AssociationRole;
using Wait = ta::Wait;
using Charge = ta::Charge;
struct Owner { std::uint32_t slot{}; std::uint64_t generation{},instance{}; };

// Four prototype threads: frozen Scheduler Data/Control/Lifecycle plus capture
// reclamation. Declared bytes do not measure an arbitrary capture's heap graph.
template<std::size_t Owners=8,std::size_t DataCells=16,std::size_t ControlCells=8,
         std::size_t InlineBytes=256,std::size_t DataBytes=65536,
         std::uint64_t GenerationLimit=UINT64_MAX,std::uint64_t CounterLimit=UINT64_MAX,
         std::size_t LifecycleBytes=4096>
class CaptureScheduler {
    using Tasks=ta::Scheduler<Owners,DataCells,ControlCells,InlineBytes,DataBytes,
                              GenerationLimit,CounterLimit,LifecycleBytes>;
    static constexpr std::size_t Cells=DataCells+ControlCells+Owners;
    static constexpr std::size_t None=std::numeric_limits<std::size_t>::max();
    enum class Phase : std::uint8_t { Free,Reserved,Queued,Running,Reclaim,Reclaiming };
    enum class StrandPhase : std::uint8_t { Free,Live,Retiring };
public:
    using Task=ta::InlineTask<InlineBytes>;
    using Limits=typename Tasks::Limits;
    struct Metrics {
        std::size_t occupied{},payload_bytes{},owners{};
        std::array<std::size_t,3> class_payload{};
        std::uint64_t executed{},exceptions{},reclaimed{};
        bool counter_saturated{};
    };
private:
    struct Cell {
        std::atomic<Phase> phase{Phase::Free};
        std::uint64_t generation{};
        std::size_t owner{},bytes{};
        TaskClass kind{};
        Task task;
    };
    struct Strand {
        rtc::impl::retirement::Activity retirement;
        StrandPhase phase{StrandPhase::Free};
        std::uint64_t generation{};
        ta::Owner scheduler{};
        Limits limits{};
        std::array<std::size_t,3> counts{},bytes{};
    };
    Tasks tasks_;
    const std::uint64_t instance_=ta::detail::acquire_identity(ta::detail::next_instance);
    std::array<Cell,Cells> cells_{};
    std::array<Strand,Owners> owners_{};
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    std::atomic<bool> closing_{},done_{};
    std::atomic<std::uint64_t> executed_{},exceptions_{},reclaimed_{};
    std::atomic<bool> saturated_{};
    Metrics metrics_{};
    std::mutex sleep_mutex_;
    std::condition_variable wake_;
    std::thread lifecycle_;
    inline static thread_local CaptureScheduler* locked_here_{};
    inline static thread_local CaptureScheduler* lifecycle_here_{};
    class Guard {
        CaptureScheduler* pool_{};
    public:
        Guard(CaptureScheduler* p,bool wait) noexcept {
            if(!wait) {if(p->gate_.test_and_set(std::memory_order_acquire))return;}
            else while(p->gate_.test_and_set(std::memory_order_acquire))std::this_thread::yield();
            pool_=p;locked_here_=p;
        }
        ~Guard(){if(pool_){locked_here_=nullptr;pool_->gate_.clear(std::memory_order_release);}}
        explicit operator bool()const noexcept{return pool_!=nullptr;}
    };
    bool valid(Owner o) const noexcept {
        return o.instance==instance_&&o.slot<Owners&&owners_[o.slot].generation==o.generation&&
               owners_[o.slot].phase!=StrandPhase::Free;
    }
    void count(std::atomic<std::uint64_t>& value) noexcept {
        auto old=value.load(std::memory_order_relaxed);
        for(;;){if(old==CounterLimit){saturated_.store(true,std::memory_order_relaxed);return;}
            if(value.compare_exchange_weak(old,old+1,std::memory_order_relaxed))return;}
    }
    struct Wrapper {
        CaptureScheduler* pool{};std::size_t cell{};std::uint64_t generation{};
        void operator()() noexcept {
            auto& c=pool->cells_[cell];
            if(c.generation!=generation)std::terminate();
            auto expected=Phase::Queued;
            if(!c.phase.compare_exchange_strong(expected,Phase::Running,std::memory_order_acq_rel))std::terminate();
            try {c.task.invoke();}catch(...){pool->count(pool->exceptions_);}
            pool->count(pool->executed_);
            c.phase.store(Phase::Reclaim,std::memory_order_release);pool->wake_.notify_all();
        }
    };
    static_assert(Tasks::Task::template supported<Wrapper>);
    void reclaim_loop() noexcept {
        lifecycle_here_=this;
        for(;;){
            for(std::size_t i=0;i<Cells;++i){
                auto& c=cells_[i];auto expected=Phase::Reclaim;
                if(!c.phase.compare_exchange_strong(expected,Phase::Reclaiming,std::memory_order_acq_rel))continue;
                // A final owning capture may destroy its Processor here. It can
                // retire/wait for Scheduler tasks, but not wait for this reset.
                c.task.reset();
                {
                    Guard lock(this,true);auto& s=owners_[c.owner];const auto lane=std::size_t(c.kind);
                    --s.counts[lane];s.bytes[lane]-=c.bytes;
                    --metrics_.occupied;metrics_.payload_bytes-=c.bytes;metrics_.class_payload[lane]-=c.bytes;
                    c.phase.store(Phase::Free,std::memory_order_release);
                }
                count(reclaimed_);wake_.notify_all();
            }
            bool empty=true;
            {
                Guard lock(this,true);
                for(auto& s:owners_){
                    if(s.phase==StrandPhase::Retiring&&s.counts==std::array<std::size_t,3>{}&&
                       tasks_.wait_retired(s.scheduler,std::chrono::steady_clock::now())==Wait::Complete){
                        s.retirement.reset(); // after every capture destructor and scheduler retirement
                        s.phase=StrandPhase::Free;--metrics_.owners;
                    }
                    if(s.phase!=StrandPhase::Free)empty=false;
                }
                if(metrics_.occupied)empty=false;
            }
            if(closing_.load(std::memory_order_acquire)&&empty)break;
            std::unique_lock lock(sleep_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(1));
        }
        lifecycle_here_=nullptr;done_.store(true,std::memory_order_release);wake_.notify_all();
    }
public:
    class Reservation {
        friend class CaptureScheduler;
        CaptureScheduler* pool_{};std::size_t cell_{None};std::uint64_t generation_{},instance_{};
        typename Tasks::Reservation task_;
        Reservation(CaptureScheduler* p,std::size_t i,typename Tasks::Reservation t) noexcept
            :pool_(p),cell_(i),generation_(p->cells_[i].generation),instance_(p->instance_),task_(std::move(t)){}
    public:
        Reservation() noexcept=default;
        Reservation(const Reservation&)=delete;Reservation& operator=(const Reservation&)=delete;
        Reservation(Reservation&& other) noexcept {swap(other);}
        Reservation& operator=(Reservation&& other) noexcept {if(this!=&other){cancel();swap(other);}return *this;}
        ~Reservation(){cancel();}
        explicit operator bool()const noexcept{return pool_!=nullptr;}
        Admission commit(Task& caller) noexcept {
            if(!pool_||!caller)return Admission::Unsupported;
            if(pool_->instance_!=instance_)return Admission::Closed;
            auto& c=pool_->cells_[cell_];
            if(c.generation!=generation_||c.phase.load(std::memory_order_acquire)!=Phase::Reserved)return Admission::Closed;
            c.task=std::move(caller); // All credits exist. User moves/destructors outside gate.
            Wrapper wrapper{pool_,cell_,generation_};typename Tasks::Task scheduled;
            if(scheduled.assign(wrapper)!=Admission::Accepted)std::terminate();
            c.phase.store(Phase::Queued,std::memory_order_release);
            if(task_.commit(scheduled)!=Admission::Accepted)std::terminate();
            pool_=nullptr;return Admission::Accepted;
        }
        void cancel() noexcept {
            if(!pool_)return;
            if(pool_->instance_!=instance_) {pool_=nullptr;return;}
            auto* p=pool_;auto& c=p->cells_[cell_];
            if(c.generation!=generation_||c.phase.load(std::memory_order_acquire)!=Phase::Reserved)std::terminate();
            task_.cancel();c.phase.store(Phase::Reclaim,std::memory_order_release);
            pool_=nullptr;p->wake_.notify_all();
        }
    private:
        void swap(Reservation& other) noexcept {
            std::swap(pool_,other.pool_);std::swap(cell_,other.cell_);std::swap(generation_,other.generation_);
            std::swap(instance_,other.instance_);std::swap(task_,other.task_);
        }
    };
    struct Registered {Admission status{Admission::Closed};Owner owner{};};
    struct Reserved {Admission status{Admission::Closed};Reservation ticket{};};
    CaptureScheduler():lifecycle_([this]{reclaim_loop();}){}
    ~CaptureScheduler(){
        if(on_capture_lifecycle_thread()||tasks_.on_worker_thread())std::terminate();
        begin_shutdown();(void)tasks_.wait_shutdown(std::chrono::steady_clock::time_point::max());
        if(lifecycle_.joinable())lifecycle_.join();
    }
    CaptureScheduler(const CaptureScheduler&)=delete;CaptureScheduler& operator=(const CaptureScheduler&)=delete;
    bool on_capture_lifecycle_thread()const noexcept{return lifecycle_here_==this;}
    bool on_scheduler_worker_thread()const noexcept{return tasks_.on_worker_thread();}
    bool in_admission_lock_on_this_thread()const noexcept{return locked_here_==this;}
#ifdef SUPERPOS_CAPTURE_TESTING
    // Deterministic fixture-only contention; absent from the RTC profile ABI.
    void test_hold_gate(std::atomic<bool>& entered,const std::atomic<bool>& release)noexcept {
        Guard lock(this,true);entered.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))std::this_thread::yield();
    }
#endif
    Registered try_register(AssociationRole role,Limits limits={},const rtc::impl::retirement::Token& group={}) noexcept {
        auto retirement=group.enter();
        if(group&&!retirement)return {Admission::Closed,{}};
        Guard lock(this,false);if(!lock)return {Admission::Contended,{}};
        if(closing_.load(std::memory_order_acquire))return {Admission::Closed,{}};
        std::size_t selected=None;bool exhausted=false;
        for(std::size_t i=0;i<Owners;++i){auto& s=owners_[i];if(s.phase!=StrandPhase::Free)continue;
            if(s.generation==GenerationLimit){exhausted=true;continue;}selected=i;break;}
        if(selected==None)return {exhausted?Admission::Exhausted:Admission::Full,{}};
        auto noop=[]{};typename Tasks::Task pin;
        if(pin.assign(noop)!=Admission::Accepted)std::terminate();
        auto registered=tasks_.try_register(role,limits,pin);
        if(registered.status!=Admission::Accepted)return {registered.status,{}};
        auto& s=owners_[selected];++s.generation;s.scheduler=registered.owner;s.limits=limits;s.retirement=std::move(retirement);s.phase=StrandPhase::Live;
        ++metrics_.owners;return {Admission::Accepted,{std::uint32_t(selected),s.generation,instance_}};
    }
    Reserved try_reserve(Owner owner,TaskClass kind,Charge charge={}) noexcept {
        if(kind!=TaskClass::Data&&kind!=TaskClass::Control&&kind!=TaskClass::Lifecycle)return {Admission::Unsupported,{}};
        if(owner.instance!=instance_)return {Admission::Closed,{}};
        Guard lock(this,false);if(!lock)return {Admission::Contended,{}};
        if(closing_.load(std::memory_order_acquire)||!valid(owner)||owners_[owner.slot].phase!=StrandPhase::Live)return {Admission::Closed,{}};
        auto& s=owners_[owner.slot];const auto lane=std::size_t(kind);
        const std::array<std::size_t,3> global_bytes{DataBytes,DataBytes/4+1,Owners*LifecycleBytes};
        const std::array<std::size_t,3> owner_bytes{s.limits.data_bytes,s.limits.control_bytes,LifecycleBytes};
        const std::array<std::size_t,3> owner_count{s.limits.data,s.limits.control,1};
        if(charge.retained_bytes>global_bytes[lane]-metrics_.class_payload[lane]||charge.retained_bytes>owner_bytes[lane]-s.bytes[lane])return {Admission::ByteLimit,{}};
        if(s.counts[lane]>=owner_count[lane])return {Admission::Full,{}};
        const auto first=kind==TaskClass::Data?0:kind==TaskClass::Control?DataCells:DataCells+ControlCells+owner.slot;
        const auto end=kind==TaskClass::Data?DataCells:kind==TaskClass::Control?DataCells+ControlCells:first+1;
        std::size_t selected=None;bool exhausted=false;
        for(auto i=first;i<end;++i){auto& c=cells_[i];if(c.phase.load(std::memory_order_acquire)!=Phase::Free)continue;
            if(c.generation==GenerationLimit){exhausted=true;continue;}selected=i;break;}
        if(selected==None)return {exhausted?Admission::Exhausted:Admission::Full,{}};
        auto reserved=tasks_.try_reserve(s.scheduler,kind,charge);
        if(reserved.status!=Admission::Accepted)return {reserved.status,{}};
        auto& c=cells_[selected];++c.generation;c.owner=owner.slot;c.kind=kind;c.bytes=charge.retained_bytes;
        ++s.counts[lane];s.bytes[lane]+=c.bytes;++metrics_.occupied;metrics_.payload_bytes+=c.bytes;metrics_.class_payload[lane]+=c.bytes;
        c.phase.store(Phase::Reserved,std::memory_order_release);
        return {Admission::Accepted,Reservation(this,selected,std::move(reserved.ticket))};
    }
    Admission try_post(Owner owner,TaskClass kind,Charge charge,Task& task) noexcept {
        if(!task)return Admission::Unsupported;auto reserved=try_reserve(owner,kind,charge);
        return reserved.status==Admission::Accepted?reserved.ticket.commit(task):reserved.status;
    }
    Admission try_retire(Owner owner) noexcept {
        Guard lock(this,false);if(!lock)return Admission::Contended;if(!valid(owner))return Admission::Closed;
        auto& s=owners_[owner.slot];if(s.phase==StrandPhase::Retiring)return Admission::Accepted;
        auto status=tasks_.try_retire(s.scheduler);if(status!=Admission::Accepted)return status;
        s.phase=StrandPhase::Retiring;wake_.notify_all();return Admission::Accepted;
    }
    // Safe from capture destructor: current capture may still be Reclaiming.
    Wait wait_tasks_retired(Owner owner,std::chrono::steady_clock::time_point deadline) noexcept {
        if(tasks_.on_worker_thread())return Wait::WouldDeadlock;
        ta::Owner underlying;
        {Guard lock(this,true);if(owner.instance!=instance_||owner.slot>=Owners||!owner.generation||owner.generation>owners_[owner.slot].generation)return Wait::InvalidOwner;
         if(owner.generation<owners_[owner.slot].generation)return Wait::Complete;
         underlying=owners_[owner.slot].scheduler;}
        return tasks_.wait_retired(underlying,deadline);
    }
    Wait wait_retired(Owner owner,std::chrono::steady_clock::time_point deadline) noexcept {
        if(on_capture_lifecycle_thread()||tasks_.on_worker_thread())return Wait::WouldDeadlock;
        for(;;){
            {Guard lock(this,true);if(owner.instance!=instance_||owner.slot>=Owners||!owner.generation||owner.generation>owners_[owner.slot].generation)return Wait::InvalidOwner;
             if(owner.generation<owners_[owner.slot].generation)return Wait::Complete;
             if(owners_[owner.slot].phase==StrandPhase::Free)return Wait::Complete;}
            if(std::chrono::steady_clock::now()>=deadline)return Wait::Timeout;
            std::unique_lock lock(sleep_mutex_);wake_.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)));
        }
    }
    void begin_shutdown() noexcept {
        {Guard lock(this,true);closing_.store(true,std::memory_order_release);
         for(auto& s:owners_)if(s.phase==StrandPhase::Live)s.phase=StrandPhase::Retiring;}
        tasks_.begin_shutdown();wake_.notify_all();
    }
    Wait wait_shutdown(std::chrono::steady_clock::time_point deadline) noexcept {
        if(on_capture_lifecycle_thread()||tasks_.on_worker_thread())return Wait::WouldDeadlock;
        if(tasks_.wait_shutdown(deadline)!=Wait::Complete)return Wait::Timeout;
        while(!done_.load(std::memory_order_acquire)){
            if(std::chrono::steady_clock::now()>=deadline)return Wait::Timeout;
            std::unique_lock lock(sleep_mutex_);wake_.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(1)));
        }
        if(lifecycle_.joinable())lifecycle_.join();return Wait::Complete;
    }
    Metrics snapshot() noexcept {
        Guard lock(this,true);auto result=metrics_;result.executed=executed_.load();result.exceptions=exceptions_.load();
        result.reclaimed=reclaimed_.load();result.counter_saturated=saturated_.load();return result;
    }
};
}
