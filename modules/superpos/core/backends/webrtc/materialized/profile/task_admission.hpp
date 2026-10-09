// Original Superpos experimental task-admission primitive. MIT licensed.
// This standalone stage does not implement or qualify the RTC integration.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace superpos::task_admission {
enum class Admission : std::uint8_t { Accepted, Full, ByteLimit, Contended, Closed, Unsupported, Exhausted };
enum class TaskClass : std::uint8_t { Data, Control, Lifecycle };
enum class AssociationRole : std::uint8_t { Control, State };
enum class Wait : std::uint8_t { Complete, Timeout, WouldDeadlock, InvalidOwner };
struct Owner { std::uint32_t slot{}; std::uint64_t generation{}; std::uint64_t instance{}; };
struct Charge { std::size_t retained_bytes{}; }; // explicit task-owned payload capacity, not sizeof(capture)
namespace detail {
// Shared across all Scheduler template specializations in this linked program.
inline std::atomic<std::uint64_t> next_instance{1};
class IdentityExhausted final : public std::exception {
public: const char* what() const noexcept override { return "TASK_INSTANCE_EXHAUSTED"; }
};
inline std::uint64_t acquire_identity(std::atomic<std::uint64_t>& next) {
    auto value=next.load(std::memory_order_relaxed);
    for(;;) {
        if(value==0 || value==std::numeric_limits<std::uint64_t>::max()) throw IdentityExhausted();
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) return value;
    }
}
}

template<std::size_t Bytes = 256> class InlineTask {
    alignas(std::max_align_t) std::array<std::byte, Bytes> bytes_{};
    void (*invoke_)(void*){};
    void (*move_)(void*, void*) noexcept{};
    void (*destroy_)(void*) noexcept{};
public:
    InlineTask() noexcept = default;
    InlineTask(const InlineTask&) = delete;
    InlineTask& operator=(const InlineTask&) = delete;
    InlineTask(InlineTask&& other) noexcept { move_from(other); }
    InlineTask& operator=(InlineTask&& other) noexcept { if(this != &other) { reset(); move_from(other); } return *this; }
    ~InlineTask() { reset(); }
    template<class F> static constexpr bool supported = sizeof(std::remove_cvref_t<F>) <= Bytes &&
        alignof(std::remove_cvref_t<F>) <= alignof(std::max_align_t) &&
        std::is_nothrow_move_constructible_v<std::remove_cvref_t<F>> &&
        std::is_nothrow_destructible_v<std::remove_cvref_t<F>> && std::is_invocable_v<std::remove_cvref_t<F>&>;
    // F is already constructed. No copy, allocation, or ownership change on failure.
    template<class F> Admission assign(F& callable) noexcept {
        using T = std::remove_cvref_t<F>;
        if constexpr(!supported<T>) return Admission::Unsupported;
        else {
            if(invoke_) return Admission::Unsupported;
            ::new(static_cast<void*>(bytes_.data())) T(std::move(callable));
            invoke_ = [](void* p) { (*static_cast<T*>(p))(); };
            move_ = [](void* dst, void* src) noexcept { ::new(dst) T(std::move(*static_cast<T*>(src))); };
            destroy_ = [](void* p) noexcept { static_cast<T*>(p)->~T(); };
            return Admission::Accepted;
        }
    }
    explicit operator bool() const noexcept { return invoke_ != nullptr; }
    void invoke() { invoke_(bytes_.data()); }
    void reset() noexcept { if(destroy_) destroy_(bytes_.data()); invoke_ = nullptr; move_ = nullptr; destroy_ = nullptr; }
private:
    void move_from(InlineTask& other) noexcept {
        if(!other.invoke_) return;
        other.move_(bytes_.data(), other.bytes_.data());
        invoke_ = other.invoke_; move_ = other.move_; destroy_ = other.destroy_;
        other.reset();
    }
};

// Provisional fixed defaults. Declared payload credits do not account an arbitrary heap graph.
// Workers are fixed: data, control, lifecycle/reclamation. Construction may throw;
// all admission/commit operations below are nonallocating and nonblocking.
template<std::size_t Owners = 16, std::size_t DataSlots = 32, std::size_t ControlSlots = 16,
         std::size_t InlineBytes = 256, std::size_t TaskByteLimit = 65536,
         std::uint64_t GenerationLimit = std::numeric_limits<std::uint64_t>::max(),
         std::uint64_t CounterLimit = std::numeric_limits<std::uint64_t>::max(),
         std::size_t LifecycleBytesPerOwner = 4096>
class Scheduler {
    static_assert(Owners > 0 && DataSlots > 0 && ControlSlots > 0 && GenerationLimit > 0 && CounterLimit > 0 && LifecycleBytesPerOwner > 0);
    static_assert(Owners <= std::numeric_limits<std::uint32_t>::max());
    static_assert(TaskByteLimit <= std::numeric_limits<std::size_t>::max()/4 && LifecycleBytesPerOwner <= std::numeric_limits<std::size_t>::max()/Owners/4);
    static constexpr std::size_t None = std::numeric_limits<std::size_t>::max();
    static constexpr std::size_t Records = DataSlots + ControlSlots + Owners;
    enum class RecordState : std::uint8_t { Free, Reserved, Queued, Running, Cancelled };
    enum class OwnerState : std::uint8_t { Free, Initializing, Live, Retiring, Finalizing, Exhausted };
public:
    using Task = InlineTask<InlineBytes>;
    struct Limits { std::size_t data{8}; std::size_t control{8}; std::size_t data_bytes{16384}; std::size_t control_bytes{4096}; };
    struct Metrics {
        std::uint64_t admitted{}, completed{}, exceptions{}, rejected{}, retired{};
        std::size_t occupied{}, running{}, payload_bytes{}, owners{}, high_water{};
        std::array<std::size_t,3> class_payload{};
        bool counter_saturated{};
    };
    class Reservation {
        friend class Scheduler;
        Scheduler* pool_{}; std::size_t index_{None}; std::uint64_t generation_{},instance_{};
        Reservation(Scheduler* p, std::size_t i, std::uint64_t g) noexcept : pool_(p), index_(i), generation_(g),instance_(p->instance_) {}
    public:
        Reservation() noexcept = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept { swap(other); }
        Reservation& operator=(Reservation&& other) noexcept { if(this != &other) { cancel(); swap(other); } return *this; }
        ~Reservation() { cancel(); }
        explicit operator bool() const noexcept { return pool_ != nullptr; }
        // Only this reservation owns the Reserved record. Publication does not lock.
        Admission commit(Task& task) noexcept {
            if(!pool_ || !task) return Admission::Unsupported;
            if(pool_->instance_!=instance_) return Admission::Closed;
            auto& r = pool_->records_[index_];
            if(r.generation != generation_ || r.state.load(std::memory_order_acquire) != RecordState::Reserved)
                return Admission::Closed;
            r.task = std::move(task); // deliberately outside scheduler lock
            r.state.store(RecordState::Queued, std::memory_order_release);
            auto* p = pool_; pool_ = nullptr; p->wake_.notify_all();
            return Admission::Accepted;
        }
        void cancel() noexcept {
            if(!pool_) return;
            if(pool_->instance_!=instance_) { pool_=nullptr; return; }
            auto& r = pool_->records_[index_];
            // This ticket prevents reuse until cancellation is consumed by a worker.
            r.state.store(RecordState::Cancelled, std::memory_order_release);
            auto* p = pool_; pool_ = nullptr; p->wake_.notify_all();
        }
    private:
        void swap(Reservation& other) noexcept { std::swap(pool_,other.pool_); std::swap(index_,other.index_); std::swap(generation_,other.generation_); std::swap(instance_,other.instance_); }
    };
    struct Registered { Admission status{Admission::Closed}; Owner owner{}; };
    struct Reserved { Admission status{Admission::Closed}; Reservation ticket{}; };
private:
    struct Record {
        std::atomic<RecordState> state{RecordState::Free};
        std::uint64_t generation{};
        std::size_t owner{}, next{None}, payload_bytes{};
        TaskClass kind{};
        Task task;
    };
    struct Strand {
        OwnerState state{OwnerState::Free}; std::uint64_t generation{};
        std::atomic<bool> initialized{false};
        AssociationRole role{}; Limits limits{};
        std::size_t head{None}, tail{None}, data{}, control{};
        std::array<std::size_t,3> class_payload{};
        bool running{}; Task lifetime_pin;
        // One fixed owner entry is its permanent readiness/continuation token.
    };
    std::array<Record, Records> records_{};
    const std::uint64_t instance_=detail::acquire_identity(detail::next_instance);
    std::array<Strand, Owners> strands_{};
    std::array<std::thread,3> workers_{};
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::mutex sleep_mutex_;
    std::condition_variable wake_;
    bool accepting_{true};
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> workers_done_{0};
    Metrics metrics_{};
    inline static thread_local Scheduler* locked_here_{};
    inline static thread_local Scheduler* worker_here_{};
    inline static thread_local TaskClass worker_lane_{};
    struct Guard {
        Scheduler* p{};
        explicit Guard(Scheduler* q, bool wait) noexcept {
            if(wait) { while(q->gate_.test_and_set(std::memory_order_acquire)) std::this_thread::yield(); p=q; }
            else if(!q->gate_.test_and_set(std::memory_order_acquire)) p=q;
            if(p) locked_here_=p;
        }
        ~Guard() { if(p) { locked_here_=nullptr; p->gate_.clear(std::memory_order_release); } }
        explicit operator bool() const noexcept { return p != nullptr; }
    };
    void increment(std::uint64_t& n) noexcept { if(n < CounterLimit) ++n; else metrics_.counter_saturated=true; }
    bool valid(Owner o) const noexcept {
        return o.instance==instance_ && o.slot < Owners && strands_[o.slot].generation == o.generation &&
            strands_[o.slot].state == OwnerState::Live && strands_[o.slot].initialized.load(std::memory_order_acquire);
    }
    void rejected() noexcept { increment(metrics_.rejected); }
    void append(Strand& s, std::size_t index) noexcept {
        if(s.tail != None) records_[s.tail].next=index; else s.head=index;
        s.tail=index;
    }
    void remove_head(Strand& s) noexcept {
        s.head=records_[s.head].next; if(s.head==None) s.tail=None;
    }
    void release_record(std::size_t index) noexcept {
        auto& r=records_[index]; auto& s=strands_[r.owner];
        if(r.kind==TaskClass::Data) --s.data;
        if(r.kind==TaskClass::Control) --s.control;
        const auto lane=static_cast<std::size_t>(r.kind);
        s.class_payload[lane]-=r.payload_bytes; metrics_.class_payload[lane]-=r.payload_bytes;
        metrics_.payload_bytes-=r.payload_bytes;
        --metrics_.occupied;
        r.state.store(RecordState::Free,std::memory_order_release);
    }
    struct Work { std::size_t record{None}, retire_owner{None}; bool exit{}; };
    Work take(TaskClass lane,std::size_t& cursor) noexcept {
        Guard lock(this,true);
        for(std::size_t n=0;n<Owners;++n) {
            const std::size_t i=(cursor+n)%Owners; auto& s=strands_[i];
            if((s.state!=OwnerState::Live && s.state!=OwnerState::Retiring) ||
               !s.initialized.load(std::memory_order_acquire) || s.running) continue;
            // Cancelled reservations have no constructed capture; any worker may retire them.
            while(s.head!=None && records_[s.head].state.load(std::memory_order_acquire)==RecordState::Cancelled) {
                auto k=s.head; remove_head(s); release_record(k);
            }
            if(s.head!=None) {
                auto k=s.head; auto& r=records_[k];
                if(r.kind!=lane || r.state.load(std::memory_order_acquire)!=RecordState::Queued) continue;
                remove_head(s); s.running=true; ++metrics_.running;
                r.state.store(RecordState::Running,std::memory_order_release);
                cursor=(i+1)%Owners; return {k,None,false};
            }
            if(lane==TaskClass::Lifecycle && s.state==OwnerState::Retiring) {
                s.state=OwnerState::Finalizing; cursor=(i+1)%Owners; return {None,i,false};
            }
        }
        return {None,None,stopping_.load(std::memory_order_acquire) && metrics_.occupied==0 && metrics_.running==0 && no_retiring()};
    }
    bool no_retiring() const noexcept {
        for(const auto& s:strands_) if(s.state==OwnerState::Retiring || s.state==OwnerState::Finalizing) return false;
        return true;
    }
    void worker(TaskClass lane) noexcept {
        worker_here_=this; worker_lane_=lane; std::size_t cursor{};
        for(;;) {
            const Work work=take(lane,cursor);
            if(work.exit) break;
            if(work.record!=None) {
                auto& r=records_[work.record];
                Task task=std::move(r.task); // user move/destruction occurs outside lock
                bool threw=false; try { task.invoke(); } catch(...) { threw=true; }
                task.reset(); // last-owner release occurs before credit reuse, outside lock
                {
                    Guard lock(this,true); auto& s=strands_[r.owner];
                    s.running=false; --metrics_.running; release_record(work.record);
                    increment(metrics_.completed); if(threw) increment(metrics_.exceptions);
                }
                // Remaining local work is already represented by the permanent owner token.
                wake_.notify_all(); continue;
            }
            if(work.retire_owner!=None) {
                auto& s=strands_[work.retire_owner];
                Task pin=std::move(s.lifetime_pin); pin.reset(); // dedicated reclamation thread
                {
                    Guard lock(this,true); --metrics_.owners; increment(metrics_.retired);
                    s.initialized.store(false,std::memory_order_release);
                    s.state=s.generation==GenerationLimit ? OwnerState::Exhausted : OwnerState::Free;
                }
                wake_.notify_all(); continue;
            }
            // Notifications are hints; the bounded scan prevents lost-wakeup stranding.
            std::unique_lock lock(sleep_mutex_); wake_.wait_for(lock,std::chrono::milliseconds(2));
        }
        worker_here_=nullptr; workers_done_.fetch_add(1,std::memory_order_release); wake_.notify_all();
    }
public:
    Scheduler() {
        try {
            for(std::size_t i=0;i<workers_.size();++i) workers_[i]=std::thread([this,i]{worker(static_cast<TaskClass>(i));});
        } catch(...) { stopping_.store(true); wake_.notify_all(); for(auto& w:workers_) if(w.joinable()) w.join(); throw; }
    }
    Scheduler(const Scheduler&)=delete; Scheduler& operator=(const Scheduler&)=delete;
    ~Scheduler() {
        // Destruction is an external lifecycle action. Never detach live workers.
        if(worker_here_==this) std::terminate();
        begin_shutdown(); for(auto& w:workers_) if(w.joinable()) w.join();
        // Pins of owners not explicitly retired are still released outside the lock.
    }
    bool in_scheduler_lock_on_this_thread() const noexcept { return locked_here_==this; }
    bool on_worker_thread() const noexcept { return worker_here_==this; }
    bool on_worker_class(TaskClass lane) const noexcept { return worker_here_==this && worker_lane_==lane; }
    Registered try_register(AssociationRole role,Limits limits,Task& lifetime_pin) noexcept {
        if(!lifetime_pin || limits.data==0 || limits.control==0 || limits.data>DataSlots || limits.control>ControlSlots || limits.data_bytes>TaskByteLimit || limits.control_bytes>TaskByteLimit/4+1)
            return {Admission::Unsupported,{}};
        std::size_t index=None; std::uint64_t generation{};
        {
            Guard lock(this,false); if(!lock) return {Admission::Contended,{}};
            if(!accepting_) { rejected(); return {Admission::Closed,{}}; }
            bool exhausted=false;
            for(std::size_t i=0;i<Owners;++i) {
                auto& s=strands_[i]; if(s.state==OwnerState::Exhausted) exhausted=true;
                if(s.state!=OwnerState::Free) continue;
                if(s.generation==GenerationLimit) { s.state=OwnerState::Exhausted; exhausted=true; continue; }
                index=i; generation=++s.generation; s.state=OwnerState::Live;
                s.role=role; s.limits=limits; ++metrics_.owners; break;
            }
            if(index==None) { rejected(); return {exhausted?Admission::Exhausted:Admission::Full,{}}; }
        }
        auto& s=strands_[index]; s.lifetime_pin=std::move(lifetime_pin);
        s.initialized.store(true,std::memory_order_release); wake_.notify_all();
        return {Admission::Accepted,{static_cast<std::uint32_t>(index),generation,instance_}};
    }
    Reserved try_reserve(Owner owner,TaskClass kind,Charge charge) noexcept {
        if(kind!=TaskClass::Data && kind!=TaskClass::Control && kind!=TaskClass::Lifecycle) return {Admission::Unsupported,{}};
        if(owner.instance!=instance_) return {Admission::Closed,{}};
        Guard lock(this,false); if(!lock) return {Admission::Contended,{}};
        if(!accepting_ || !valid(owner)) { rejected(); return {Admission::Closed,{}}; }
        auto& s=strands_[owner.slot];
        const auto lane=static_cast<std::size_t>(kind);
        const std::array<std::size_t,3> global_limit{TaskByteLimit,TaskByteLimit/4+1,Owners*LifecycleBytesPerOwner};
        const std::array<std::size_t,3> owner_limit{s.limits.data_bytes,s.limits.control_bytes,LifecycleBytesPerOwner};
        if(charge.retained_bytes > global_limit[lane]-metrics_.class_payload[lane] || charge.retained_bytes > owner_limit[lane]-s.class_payload[lane]) {
            rejected(); return {Admission::ByteLimit,{}};
        }
        if((kind==TaskClass::Data && s.data>=s.limits.data) || (kind==TaskClass::Control && s.control>=s.limits.control)) {
            rejected(); return {Admission::Full,{}};
        }
        const std::size_t first=kind==TaskClass::Data?0:kind==TaskClass::Control?DataSlots:DataSlots+ControlSlots+owner.slot;
        const std::size_t end=kind==TaskClass::Data?DataSlots:kind==TaskClass::Control?DataSlots+ControlSlots:first+1;
        std::size_t index=None; bool exhausted=false;
        for(auto i=first;i<end;++i) {
            auto& r=records_[i]; if(r.state.load(std::memory_order_relaxed)!=RecordState::Free) continue;
            if(r.generation==GenerationLimit) { exhausted=true; continue; } index=i; break;
        }
        if(index==None) { rejected(); return {exhausted?Admission::Exhausted:Admission::Full,{}}; }
        auto& r=records_[index]; ++r.generation; r.owner=owner.slot; r.kind=kind; r.next=None; r.payload_bytes=charge.retained_bytes;
        r.state.store(RecordState::Reserved,std::memory_order_release); append(s,index);
        if(kind==TaskClass::Data) ++s.data; if(kind==TaskClass::Control) ++s.control;
        s.class_payload[lane]+=charge.retained_bytes; metrics_.class_payload[lane]+=charge.retained_bytes;
        metrics_.payload_bytes+=charge.retained_bytes;
        ++metrics_.occupied; if(metrics_.occupied>metrics_.high_water) metrics_.high_water=metrics_.occupied;
        increment(metrics_.admitted);
        return {Admission::Accepted,Reservation(this,index,r.generation)};
    }
    Admission try_post(Owner owner,TaskClass kind,Charge charge,Task& task) noexcept {
        if(!task) return Admission::Unsupported;
        auto reservation=try_reserve(owner,kind,charge);
        if(reservation.status!=Admission::Accepted) return reservation.status;
        return reservation.ticket.commit(task);
    }
    Admission try_retire(Owner owner) noexcept {
        if(owner.instance!=instance_) return Admission::Closed;
        Guard lock(this,false); if(!lock) return Admission::Contended;
        if(!valid(owner)) return Admission::Closed;
        strands_[owner.slot].state=OwnerState::Retiring; wake_.notify_all(); return Admission::Accepted;
    }
    Wait wait_retired(Owner owner,std::chrono::steady_clock::time_point deadline) noexcept {
        if(owner.instance!=instance_) return Wait::InvalidOwner;
        if(worker_here_==this) return Wait::WouldDeadlock;
        for(;;) {
            {
                Guard lock(this,true);
                if(owner.slot>=Owners || strands_[owner.slot].generation!=owner.generation ||
                    strands_[owner.slot].state==OwnerState::Free || strands_[owner.slot].state==OwnerState::Exhausted) return Wait::Complete;
            }
            if(std::chrono::steady_clock::now()>=deadline) return Wait::Timeout;
            std::unique_lock lock(sleep_mutex_); wake_.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(2)));
        }
    }
    void begin_shutdown() noexcept {
        { Guard lock(this,true); accepting_=false; stopping_.store(true,std::memory_order_release);
          for(auto& s:strands_) if(s.state==OwnerState::Live) s.state=OwnerState::Retiring; }
        wake_.notify_all();
    }
    Wait wait_shutdown(std::chrono::steady_clock::time_point deadline) noexcept {
        if(worker_here_==this) return Wait::WouldDeadlock;
        while(workers_done_.load(std::memory_order_acquire)!=workers_.size()) {
            if(std::chrono::steady_clock::now()>=deadline) return Wait::Timeout;
            std::unique_lock lock(sleep_mutex_); wake_.wait_until(lock,std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(2)));
        }
        for(auto& w:workers_) if(w.joinable()) w.join(); return Wait::Complete;
    }
    Metrics snapshot() noexcept { Guard lock(this,true); return metrics_; }
    static constexpr std::size_t fixed_storage_bytes=sizeof(std::array<Record,Records>)+sizeof(std::array<Strand,Owners>);
};
} // namespace superpos::task_admission
