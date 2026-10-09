// Original experimental RTC Processor profile. MIT licensed.
#pragma once
#include "capture_retirement.hpp"
#include "profile_limits.hpp"
#include <atomic>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#ifdef RTC_SUPERPOS_PROCESSOR_DIAGNOSTICS
#include <cstdio>
#endif
namespace rtc::impl {
namespace pa=::superpos::processor_admission;
using ProcessorPool=pa::CaptureScheduler<profile_limits::processor_owners,
    profile_limits::data_tasks,profile_limits::control_tasks,512,1024*1024>;
ProcessorPool* active_processor_pool() noexcept;
class ProcessorFailure final:public std::exception {
    pa::Admission code_;
public:explicit ProcessorFailure(pa::Admission code=pa::Admission::Closed)noexcept:code_(code){}
    pa::Admission code()const noexcept{return code_;}
    const char* what()const noexcept override{return "BOUNDED_PROCESSOR_UNAVAILABLE";}
};
class Processor {
    ProcessorPool* pool_{};pa::Owner owner_{};pa::TaskClass lane_{};
    std::atomic<pa::Admission> failure_{pa::Admission::Accepted};
public:
    using Reserved=ProcessorPool::Reserved;
    Processor(std::size_t=0,bool control=false,retirement::Token group={});
    ~Processor();
    Processor(const Processor&)=delete;Processor& operator=(const Processor&)=delete;
    void join();
    pa::Admission status()const noexcept{return failure_.load(std::memory_order_acquire);}
    pa::TaskClass lane()const noexcept{return lane_;}
    pa::Admission note(pa::Admission status,bool coalesced=false)noexcept {
        // Contended is an explicit transient rejection. Coalesced SCTP work
        // remains dirty for the owner's fixed host poll; other failures stick.
        if(status!=pa::Admission::Accepted&&!(coalesced&&status==pa::Admission::Contended)){auto expected=pa::Admission::Accepted;failure_.compare_exchange_strong(expected,status);}
        return status;
    }
    Reserved try_reserve(pa::TaskClass kind,pa::Charge charge={},bool coalesced=false)noexcept {
        if(kind!=pa::TaskClass::Lifecycle&&status()!=pa::Admission::Accepted)return {status(),{}};
        auto result=pool_->try_reserve(owner_,kind,charge);
        // A coalesced lifecycle retry can meet its own still-reclaiming fixed
        // cell. Keep the close intent; expose Full without making it sticky.
        if(!(coalesced&&kind==pa::TaskClass::Lifecycle&&result.status==pa::Admission::Full))note(result.status,coalesced);
        return result;
    }
    // A fixed mailbox already owns this work. Full/Contended rejects only the
    // scheduling attempt, without losing work or poisoning the association.
    Reserved try_reserve_retained_control() noexcept {
        if(status()!=pa::Admission::Accepted)return {status(),{}};
        auto result=pool_->try_reserve(owner_,pa::TaskClass::Control,{});
        if(result.status!=pa::Admission::Full&&result.status!=pa::Admission::Contended)note(result.status);
        return result;
    }
    template<class F,class...Args>struct Invocation {
        Processor* processor;std::decay_t<F> function;std::tuple<std::decay_t<Args>...> arguments;
        void operator()(){try{std::apply([this](auto&...a){std::invoke(function,a...);},arguments);}
            catch(...){processor->note(pa::Admission::Unsupported);throw;}}
    };
    template<class F,class...Args>static constexpr bool supported=ProcessorPool::Task::template supported<Invocation<F,Args...>>&&
        std::is_nothrow_constructible_v<std::decay_t<F>,F&&>&&
        std::is_nothrow_constructible_v<std::tuple<std::decay_t<Args>...>,Args&&...>;
    template<class F,class...Args>pa::Admission commit(Reserved& reserved,F&& f,Args&&...args)noexcept {
        if(reserved.status!=pa::Admission::Accepted)return note(reserved.status);
        if constexpr(!supported<F,Args...>)return note(pa::Admission::Unsupported);
        else {
            Invocation<F,Args...> invocation{this,std::forward<F>(f),{std::forward<Args>(args)...}};
            ProcessorPool::Task task;if(task.assign(invocation)!=pa::Admission::Accepted)std::terminate();
            return note(reserved.ticket.commit(task));
        }
    }
    template<class F,class...Args>pa::Admission enqueue(F&& f,Args&&...args)noexcept {
        if constexpr(!supported<F,Args...>)return note(pa::Admission::Unsupported);
        else {auto reserved=try_reserve(lane_);
#ifdef RTC_SUPERPOS_PROCESSOR_DIAGNOSTICS
            if(reserved.status!=pa::Admission::Accepted){std::printf("RTC_PROCESSOR_TASK %u %u %zu %u\n",owner_.slot,unsigned(lane_),sizeof(Invocation<F,Args...>),unsigned(reserved.status));std::fflush(stdout);}
#endif
            return commit(reserved,std::forward<F>(f),std::forward<Args>(args)...);}
    }
};

// Admission must precede construction of a transport's lower-provider owner.
// Otherwise a rejected Processor member unwinds Transport and stops the live
// lower transport before the caller can retry the transient rejection.
class TransportProcessorAdmission {
protected:
    Processor mProcessor;
    explicit TransportProcessorAdmission(bool control,retirement::Token group={}):mProcessor(0,control,std::move(group)){}
};
}
