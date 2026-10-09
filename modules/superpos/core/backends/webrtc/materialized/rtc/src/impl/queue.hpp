/**
 * Copyright (c) 2019 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_QUEUE_H
#define RTC_IMPL_QUEUE_H
#include "common.hpp"
#include <condition_variable>
#include <limits>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <vector>
#include <type_traits>
namespace rtc::impl {
// Superpos maintained runtime patch: fixed descriptor storage for bounded queues,
// immutable byte charges and nonblocking tryPush. Legacy unbounded construction
// remains available only outside the qualified Superpos backend profile.
template<typename T> class Queue {
public:
    using amount_function=std::function<size_t(const T&)>;
    Queue(size_t limit=0,amount_function func=nullptr,size_t byteLimit=0);
    ~Queue();
    void stop();bool running()const;bool empty()const;bool full()const;
    size_t size()const;size_t amount()const;size_t reservedBytes()const;
#if RTC_SUPERPOS_PROFILE
    template<typename F> size_t measure(F)const;
#endif
    void push(T);bool tryPush(T);optional<T> pop();optional<T> peek();optional<T> exchange(T);
private:
    struct Entry {optional<T> value;size_t charge{};};
    bool admits(size_t charge)const noexcept;
    void insert(T&& element,size_t charge);
    size_t count()const noexcept{return mLimit?mCount:mQueue->size();}
    Entry& front()noexcept{return mLimit?mRing[mHead]:mQueue->front();}
    const size_t mLimit,mByteLimit;
    size_t mAmount{},mHead{},mCount{};
    std::vector<Entry> mRing;
    optional<std::queue<Entry>> mQueue;
    amount_function mAmountFunction;
    bool mStopping{};
    mutable std::mutex mMutex;
    std::condition_variable mPushCondition;
};
template<typename T> Queue<T>::Queue(size_t limit,amount_function func,size_t byteLimit):
    mLimit(limit),mByteLimit(byteLimit),mRing(limit),mAmountFunction(func?std::move(func):amount_function([](const T&){return size_t{1};})) {
    if(!mLimit)mQueue.emplace();
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T>,
        "Runtime queues require nonthrowing transfer; allocating copies are private peek only");
}
template<typename T> Queue<T>::~Queue(){stop();}
template<typename T> bool Queue<T>::admits(size_t charge)const noexcept{
    if(mStopping||(mLimit&&mCount>=mLimit)||charge>std::numeric_limits<size_t>::max()-mAmount)return false;
    return !mByteLimit||(mAmount<=mByteLimit&&charge<=mByteLimit-mAmount);
}
template<typename T> void Queue<T>::insert(T&& element,size_t charge){
    if(mLimit){auto& entry=mRing[(mHead+mCount)%mLimit];entry.value.emplace(std::move(element));entry.charge=charge;++mCount;}
    else {mQueue->emplace(Entry{optional<T>(std::move(element)),charge});}
    // Descriptor admission/transfer completes before publishing a byte charge.
    mAmount+=charge;
}
template<typename T> void Queue<T>::stop(){std::lock_guard lock(mMutex);mStopping=true;mPushCondition.notify_all();}
template<typename T> bool Queue<T>::running()const{std::lock_guard lock(mMutex);return count()||!mStopping;}
template<typename T> bool Queue<T>::empty()const{std::lock_guard lock(mMutex);return !count();}
template<typename T> bool Queue<T>::full()const{std::lock_guard lock(mMutex);return (mLimit&&mCount>=mLimit)||(mByteLimit&&mAmount>=mByteLimit);}
template<typename T> size_t Queue<T>::size()const{std::lock_guard lock(mMutex);return count();}
template<typename T> size_t Queue<T>::amount()const{std::lock_guard lock(mMutex);return mAmount;}
template<typename T> size_t Queue<T>::reservedBytes()const{std::lock_guard lock(mMutex);return mRing.capacity()*sizeof(Entry);}
#if RTC_SUPERPOS_PROFILE
template<typename T> template<typename F> size_t Queue<T>::measure(F measure)const{
    static_assert(std::is_nothrow_invocable_r_v<size_t,F,const T&>);
    std::lock_guard lock(mMutex);
    if(!mLimit)throw std::logic_error("Bounded query requires fixed descriptors");
    size_t total=0;
    for(size_t i=0;i<mCount;++i){
        const auto value=measure(*mRing[(mHead+i)%mLimit].value);
        if(value>std::numeric_limits<size_t>::max()-total)throw std::length_error("Bounded query overflow");
        total+=value;
    }
    return total;
}
#endif
template<typename T> bool Queue<T>::tryPush(T element){
#if RTC_SUPERPOS_PROFILE
    std::unique_lock lock(mMutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
#else
    std::lock_guard lock(mMutex);
#endif
    const auto charge=mAmountFunction(element);if(!admits(charge))return false;
    insert(std::move(element),charge);return true;
}
template<typename T> void Queue<T>::push(T element){
    std::unique_lock lock(mMutex);const auto charge=mAmountFunction(element);
    if(mByteLimit&&charge>mByteLimit)throw std::length_error("Queue element exceeds byte admission limit");
    mPushCondition.wait(lock,[&]{return mStopping||admits(charge);});if(mStopping)return;
    insert(std::move(element),charge);
}
template<typename T> optional<T> Queue<T>::pop(){
    std::unique_lock lock(mMutex);if(!count())return nullopt;auto& entry=front();
    optional<T> element(std::move(*entry.value));mAmount-=entry.charge;
    if(mLimit){entry.value.reset();entry.charge=0;mHead=(mHead+1)%mLimit;--mCount;}else mQueue->pop();
    mPushCondition.notify_all();return element;
}
template<typename T> optional<T> Queue<T>::peek(){std::lock_guard lock(mMutex);return count()?optional<T>(*front().value):nullopt;}
template<typename T> optional<T> Queue<T>::exchange(T element){
    std::unique_lock lock(mMutex);if(!count()||mStopping)return nullopt;
    auto& entry=front();const auto charge=mAmountFunction(element),base=mAmount-entry.charge;
    if(charge>std::numeric_limits<size_t>::max()-base||(mByteLimit&&(base>mByteLimit||charge>mByteLimit-base)))return nullopt;
    std::swap(*entry.value,element);entry.charge=charge;mAmount=base+charge;
    mPushCondition.notify_all();return optional<T>(std::move(element));
}
}
#endif
