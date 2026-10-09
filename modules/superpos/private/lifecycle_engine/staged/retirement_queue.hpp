// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/result.hpp>
#include <thread>
#include <cstddef>
#include <atomic>
#include <cstdint>

namespace superpos_egp::lifecycle_engine {
// An admitted owner reserves its embedded node while allocation may still
// fail. Destructor transfer only relinks that node: no callbacks, allocation,
// reference resurrection, or dependency destruction occur in transfer().
class RetirementQueue {
public:
    struct Node {
        Node() noexcept=default;
        Node(const Node&)=delete;Node& operator=(const Node&)=delete;
        Node(Node&&)=delete;Node& operator=(Node&&)=delete;
        bool is_queued() const noexcept{return queued;}
        bool is_reserved() const noexcept{return queue!=nullptr;}
    private:
        friend class RetirementQueue;
        RetirementQueue* queue{};std::uint64_t queue_identity{};
        void* context{};
        superpos::Status (*progress)(void*,bool) noexcept{};
        void (*dispose)(void*) noexcept{};
        Node* next{};
        bool queued{},processing{};
    };
    explicit RetirementQueue(std::size_t capacity=1024) noexcept:identity_(identity()),capacity_(capacity){}
    RetirementQueue(const RetirementQueue&)=delete;RetirementQueue& operator=(const RetirementQueue&)=delete;
    RetirementQueue(RetirementQueue&&)=delete;RetirementQueue& operator=(RetirementQueue&&)=delete;
    superpos::Status reserve(Node& node) noexcept {
        if(!owner())return superpos::fail(superpos::Error::PermissionDenied);
        if(node.queue || node.queued || node.processing)return superpos::fail(superpos::Error::Busy);
        if(!identity_)return superpos::fail(superpos::Error::CounterExhausted);
        if(reserved_==capacity_)return superpos::fail(superpos::Error::CapacityExceeded);
        node.queue=this;node.queue_identity=identity_;++reserved_;return {};
    }
    superpos::Status release(Node& node) noexcept {
        if(!owner())return superpos::fail(superpos::Error::PermissionDenied);
        if(node.queue!=this || node.queue_identity!=identity_)return superpos::fail(superpos::Error::InvalidArgument);
        if(node.queued || node.processing)return superpos::fail(superpos::Error::Busy);
        node.queue=nullptr;node.queue_identity=0;--reserved_;return {};
    }
    superpos::Status transfer(Node& node,void* context,superpos::Status (*progress)(void*,bool) noexcept,void (*dispose)(void*) noexcept) noexcept {
        if(!owner())return superpos::fail(superpos::Error::PermissionDenied);
        if(node.queue!=this || node.queue_identity!=identity_ || node.queued || node.processing || !context || !progress || !dispose)return superpos::fail(superpos::Error::InvalidArgument);
        node.context=context;node.progress=progress;node.dispose=dispose;node.queued=true;
        node.next=head_;head_=&node;++pending_;return {};
    }
    superpos::Status drain(bool shutdown=false) noexcept {
        if(!owner())return superpos::fail(superpos::Error::PermissionDenied);
        if(draining_)return superpos::fail(superpos::Error::Busy);
        draining_=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{draining_};
        auto* node=head_;head_=nullptr;superpos::Error error=superpos::Error::Busy;
        while(node){auto* next=node->next;node->next=nullptr;node->processing=true;
            auto progressed=node->progress(node->context,shutdown);
            node->processing=false;
            if(progressed){auto dispose=node->dispose;auto* context=node->context;
                node->queued=false;node->queue=nullptr;node->queue_identity=0;--pending_;--reserved_;dispose(context);
            }else{error=progressed.error();node->next=head_;head_=node;}
            node=next;
        }
        return pending_?superpos::fail(error):superpos::Status{};
    }
    std::size_t pending() const noexcept{return pending_;}
    std::size_t reserved() const noexcept{return reserved_;}
private:
    static std::uint64_t identity() noexcept {
        static std::atomic<std::uint64_t> next{1};auto current=next.load(std::memory_order_relaxed);
        for(;;){if(current==UINT64_MAX)return 0;if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))return current;}
    }
    const std::uint64_t identity_;
    bool owner() const noexcept{return thread_==std::this_thread::get_id();}
    std::thread::id thread_{std::this_thread::get_id()};
    Node* head_{};std::size_t capacity_{},reserved_{},pending_{};bool draining_{};
};
}
