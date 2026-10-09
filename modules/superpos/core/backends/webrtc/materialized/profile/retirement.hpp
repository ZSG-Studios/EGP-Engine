// Original Superpos association retirement accounting. MIT licensed.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace superpos { class Allocator; }
namespace rtc::impl::retirement { class Token; }
namespace superpos::rtc_profile {
template<class T,class...Args>std::shared_ptr<T> make_backed(const rtc::impl::retirement::Token&,superpos::Allocator&,Args&&...);
}
namespace rtc::impl::retirement {
inline constexpr std::size_t capacity=528;
enum class Status { Ready, Full, Exhausted, Closed, WrongThread };
struct Key {std::uint64_t domain{},generation{};std::uint32_t slot{};};
struct Snapshot {bool valid{},sealed{},quiescent{};std::uint64_t pins{},activities{};};
class Domain;
class Activity;
class Failure final:public std::exception {
public:const char* what()const noexcept override{return "RTC_RETIREMENT_CLOSED";}
};
class Token {
    friend class Domain;friend class Activity;
    Domain* domain_{};Key key_{};
    Token(Domain* domain,Key key)noexcept:domain_(domain),key_(key){}
public:
    Token()noexcept=default;
    Token(const Token&)noexcept;
    Token& operator=(const Token&)noexcept;
    Token(Token&& other)noexcept:domain_(std::exchange(other.domain_,nullptr)),key_(other.key_){}
    Token& operator=(Token&& other)noexcept;
    ~Token(){reset();}
    explicit operator bool()const noexcept{return domain_!=nullptr;}
    Key key()const noexcept{return key_;}
    // Immutable borrowed host context; token pins retain Domain lifetime.
    superpos::Allocator* backing_allocator()const noexcept;
    void reset()noexcept;
    bool seal()const noexcept;
    Snapshot snapshot()const noexcept;
    bool quiescent()const noexcept{return snapshot().quiescent;}
    Activity enter()const noexcept;
};
// A single counted operation, movable but never duplicated. Object deletion
// hands this count to a copyable shared_ptr deleter, invoked exactly once.
class Activity {
    friend class Token;
    Token token_;bool active_{};
    explicit Activity(Token&& token)noexcept:token_(std::move(token)),active_(true){}
public:
    Activity()noexcept=default;
    Activity(const Activity&)=delete;Activity& operator=(const Activity&)=delete;
    Activity(Activity&& other)noexcept:token_(std::move(other.token_)),active_(std::exchange(other.active_,false)){}
    Activity& operator=(Activity&& other)noexcept{if(this!=&other){reset();token_=std::move(other.token_);active_=std::exchange(other.active_,false);}return *this;}
    ~Activity(){reset();}
    explicit operator bool()const noexcept{return active_;}
    void reset()noexcept;
    // Only the shared_ptr factory uses this; the returned deleter must finish.
    Token handoff()noexcept{if(!active_)std::terminate();active_=false;return std::move(token_);}
};
class Domain {
    friend class Token;friend class Activity;
    superpos::Allocator* const backing_{};
    struct Slot {std::uint64_t generation{},pins{},activities{};bool sealed{};};
    std::array<Slot,capacity> slots_{};
    mutable std::mutex mutex_;
    const std::thread::id owner_=std::this_thread::get_id();
    inline static std::atomic<std::uint64_t> next_domain_{1};
    const std::uint64_t identity_;
    const std::uint64_t generation_limit_;
    static std::uint64_t identity()noexcept{
        auto n=next_domain_.load(std::memory_order_relaxed);
        while(n&&n!=UINT64_MAX){if(next_domain_.compare_exchange_weak(n,n+1,std::memory_order_relaxed))return n;}
        std::terminate();
    }
    bool valid(Key k)const noexcept{return k.domain==identity_&&k.slot<capacity&&k.generation&&slots_[k.slot].generation==k.generation&&slots_[k.slot].pins;}
    Slot& checked(Key k)noexcept{if(!valid(k))std::terminate();return slots_[k.slot];}
    void pin(Key k)noexcept{std::lock_guard lock(mutex_);auto& s=checked(k);if(s.pins==UINT64_MAX)std::terminate();++s.pins;}
    void unpin(Key k)noexcept{std::lock_guard lock(mutex_);auto& s=checked(k);if(--s.pins==0&&s.activities)std::terminate();}
    bool start(Key k)noexcept{std::lock_guard lock(mutex_);auto& s=checked(k);if(s.sealed)return false;if(s.activities==UINT64_MAX||s.pins==UINT64_MAX)std::terminate();++s.activities;++s.pins;return true;}
    void finish(Key k)noexcept{std::lock_guard lock(mutex_);auto& s=checked(k);if(!s.activities)std::terminate();--s.activities;}
public:
    struct Reserved {Status status{Status::Closed};Token token;};
    // A smaller limit supports deterministic terminal-counter tests. Native
    // ProcessorHost always uses the default full-width generation range.
    explicit Domain(std::uint64_t generation_limit=UINT64_MAX)noexcept:identity_(identity()),generation_limit_(generation_limit){}
    // This parent outlives all objects, weak control blocks and active frees.
    Domain(superpos::Allocator& backing,std::uint64_t generation_limit=UINT64_MAX)noexcept:backing_(&backing),identity_(identity()),generation_limit_(generation_limit){}
    Domain(const Domain&)=delete;Domain& operator=(const Domain&)=delete;
    ~Domain(){if(!empty())std::terminate();}
    Reserved reserve()noexcept{
        if(owner_!=std::this_thread::get_id())return {Status::WrongThread,{}};
        std::lock_guard lock(mutex_);bool future_capacity=false;
        for(std::size_t i=0;i<capacity;++i){auto& s=slots_[i];
            future_capacity|=s.generation<generation_limit_;
            if(s.pins)continue;
            if(s.activities)std::terminate();
            if(s.generation==generation_limit_)continue;
            ++s.generation;s.pins=1;s.sealed=false;
            return {Status::Ready,Token(this,{identity_,s.generation,static_cast<std::uint32_t>(i)})};}
        return {future_capacity?Status::Full:Status::Exhausted,{}};
    }
    Snapshot inspect(Key k)const noexcept{
        std::lock_guard lock(mutex_);if(!valid(k))return {};
        const auto& s=slots_[k.slot];return {true,s.sealed,s.sealed&&!s.activities,s.pins,s.activities};
    }
    bool empty()const noexcept{std::lock_guard lock(mutex_);for(const auto& s:slots_)if(s.pins||s.activities)return false;return true;}
    // Called exactly once by the shared_ptr deleter, after complete delete.
    static void deleted(const Token& token)noexcept{if(!token.domain_)std::terminate();token.domain_->finish(token.key_);}
};
inline superpos::Allocator* Token::backing_allocator()const noexcept{return domain_?domain_->backing_:nullptr;}
inline Token::Token(const Token& other)noexcept:domain_(other.domain_),key_(other.key_){if(domain_)domain_->pin(key_);}
inline Token& Token::operator=(const Token& other)noexcept{if(this!=&other){Token copy(other);*this=std::move(copy);}return *this;}
inline Token& Token::operator=(Token&& other)noexcept{if(this!=&other){reset();domain_=std::exchange(other.domain_,nullptr);key_=other.key_;}return *this;}
inline void Token::reset()noexcept{if(auto* d=std::exchange(domain_,nullptr))d->unpin(key_);}
inline bool Token::seal()const noexcept{if(!domain_)return false;std::lock_guard lock(domain_->mutex_);domain_->checked(key_).sealed=true;return true;}
inline Snapshot Token::snapshot()const noexcept{return domain_?domain_->inspect(key_):Snapshot{};}
inline Activity Token::enter()const noexcept{return domain_&&domain_->start(key_)?Activity(Token(domain_,key_)):Activity{};}
inline void Activity::reset()noexcept{if(std::exchange(active_,false))token_.domain_->finish(token_.key_);token_.reset();}

template<class T>struct Delete {
    Token token;
    void operator()(T* object)const noexcept{
        delete object; // includes all bases/members and operator delete
        Domain::deleted(token);
    }
};
template<class T,class Allocator,class...Args>
std::shared_ptr<T> make_owned_with_allocator(const Token& token,const Allocator& allocator,Args&&...args){
    if(!token)throw Failure();
    auto activity=token.enter();if(!activity)throw Failure();
    auto* object=new T(std::forward<Args>(args)...); // unwind ends before Activity decrement
    Delete<T> deleter{activity.handoff()};
    // The standard shared_ptr constructor calls deleter(object) on control-block
    // allocation/copy failure. Deleter copies pin the slot, not the live count.
    return std::shared_ptr<T>(object,std::move(deleter),allocator);
}
template<class T,class...Args>std::shared_ptr<T> make_owned(const Token& token,Args&&...args){
    if(!token)return std::make_shared<T>(std::forward<Args>(args)...); // legacy untracked caller
    auto* backing=token.backing_allocator();
    if(!backing)throw Failure(); // Tracked runtime cannot silently escape its ledger.
    return ::superpos::rtc_profile::make_backed<T>(token,*backing,std::forward<Args>(args)...);
}
}

// Definitions follow the complete retirement types; direct include is also safe.
#include "backing_allocator.hpp"
