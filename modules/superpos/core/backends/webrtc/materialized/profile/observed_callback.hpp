// Original experimental atomic-presence adapter. MIT licensed.
#pragma once
#include <rtc/utils.hpp>
#include <atomic>
namespace rtc::impl {
template<class...Args>class observed_callback final:public synchronized_callback<Args...>{
    using Base=synchronized_callback<Args...>;
    std::atomic<bool> observed_{};
protected:
    void set(std::function<void(Args...)> callback)override{
        Base::set(std::move(callback));observed_.store(bool(this->callback),std::memory_order_release);
    }
public:
    using Base::operator=;
    observed_callback()=default;
    observed_callback(const observed_callback& other){Base::operator=(other);}
    observed_callback& operator=(const observed_callback& other){if(this!=&other)Base::operator=(other);return *this;}
    observed_callback(observed_callback&& other){*this=std::move(other);}
    observed_callback& operator=(observed_callback&& other){
        if(this!=&other){std::scoped_lock lock(this->mutex,other.mutex);
            set(std::exchange(other.callback,nullptr));other.observed_.store(false,std::memory_order_release);}
        return *this;
    }
    bool hasObserver()const noexcept{return observed_.load(std::memory_order_acquire);}
};
}
