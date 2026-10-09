#include "processor_host.hpp"
#include <rtc/peerconnection.hpp>
namespace rtc::impl {
struct ProcessorHost::Storage {
    retirement::Domain retirement; // first constructed, destroyed after scheduler
    explicit Storage(superpos::Allocator& backing):retirement(backing){}
    ProcessorPool pool;
    std::array<std::shared_ptr<rtc::PeerConnection>,profile_limits::associations> retained{};
};
namespace {std::atomic<ProcessorPool*> active{};}
ProcessorPool* active_processor_pool()noexcept{return active.load(std::memory_order_acquire);}
std::size_t ProcessorHost::metadata_bytes()noexcept{return sizeof(Storage);}
retirement::Domain::Reserved ProcessorHost::reserveRetirement()noexcept {
    if(owner_!=std::this_thread::get_id())return {retirement::Status::WrongThread,{}};
    if(draining_||closed_)return {retirement::Status::Closed,{}};
    return storage_->retirement.reserve();
}
ProcessorPool::Metrics ProcessorHost::metrics()noexcept{return storage_->pool.snapshot();}
ProcessorHost::ProcessorHost(superpos::Allocator& allocator):allocator_(allocator){
    if(active.load(std::memory_order_acquire))throw ProcessorFailure();
    auto* memory=allocator_.allocate(sizeof(Storage),alignof(Storage),superpos::MemoryDomain::Backend);
    if(!memory)throw ProcessorFailure(pa::Admission::ByteLimit);
    try{storage_=std::construct_at(static_cast<Storage*>(memory),allocator_);}
    catch(...){allocator_.deallocate(memory);throw;}
    ProcessorPool* expected=nullptr;
    if(!active.compare_exchange_strong(expected,&storage_->pool)){
        std::destroy_at(storage_);allocator_.deallocate(storage_);storage_=nullptr;
        throw ProcessorFailure();
    }
}
ProcessorHost::~ProcessorHost(){
    if(owner_!=std::this_thread::get_id()||!closed_)std::terminate();
    std::destroy_at(storage_);allocator_.deallocate(storage_);
}
pa::Admission ProcessorHost::retain(const std::shared_ptr<rtc::PeerConnection>& peer)noexcept {
    if(owner_!=std::this_thread::get_id()||draining_||closed_||!peer)return pa::Admission::Closed;
    for(auto& slot:storage_->retained)if(slot==peer)return pa::Admission::Accepted;
    for(auto& slot:storage_->retained)if(!slot){slot=peer;return pa::Admission::Accepted;}return pa::Admission::Full;
}
ProcessorHost::Release ProcessorHost::release(const std::shared_ptr<rtc::PeerConnection>& peer)noexcept {
    if(owner_!=std::this_thread::get_id())return Release::WrongThread;
    if(draining_||closed_)return Release::HostClosed;
    if(!peer)return Release::NotRetained;
    for(auto& slot:storage_->retained)if(slot==peer){
        if(peer->state()!=rtc::PeerConnection::State::Closed||!peer->superposCloseComplete())return Release::NotClosed;
        slot.reset();return Release::Released;
    }
    return Release::NotRetained;
}
ProcessorHost::Poll ProcessorHost::poll(std::size_t quantum)noexcept {
    Poll result;if(owner_!=std::this_thread::get_id()){result.wrong_thread=true;return result;}
    if(draining_||closed_)return result;quantum=std::min(quantum,storage_->retained.size());
    for(std::size_t n=0;n<quantum;++n){auto& peer=storage_->retained[cursor_];cursor_=(cursor_+1)%storage_->retained.size();
        if(!peer)continue;++result.scanned;auto status=peer->superposTaskPoll();if(status!=pa::Admission::Accepted){++result.failed;result.status=status;}}
    return result;
}
pa::Wait ProcessorHost::shutdown(std::chrono::steady_clock::time_point deadline)noexcept {
    if(owner_!=std::this_thread::get_id())return pa::Wait::InvalidOwner;
    if(closed_)return pa::Wait::Complete;
    // Explicit host-held owners are released only after the fixture/service has
    // stopped callbacks and observed Closed. Processor destructors retire tasks.
    if(!draining_){
        for(auto& peer:storage_->retained)if(peer&&(peer->state()!=rtc::PeerConnection::State::Closed||!peer->superposCloseComplete()))return pa::Wait::Timeout;
        // Once the scheduler is sealed, a timed-out wait must not reopen peer
        // retention or restart shutdown. A retry only observes the same drain.
        draining_=true;
        for(auto& peer:storage_->retained)peer.reset();
        storage_->pool.begin_shutdown();
    }
    auto result=storage_->pool.wait_shutdown(deadline);
    if(result==pa::Wait::Complete){
        // Observer tokens and weak control blocks also retain group slots. Do not
        // destroy the domain while external pins or direct object owners remain.
        if(!storage_->retirement.empty())return pa::Wait::Timeout;
        // Keep the unique host claimed until every old capture is quiescent.
        // Otherwise an old callback could discover a replacement scheduler.
        ProcessorPool* expected=&storage_->pool;
        if(!active.compare_exchange_strong(expected,nullptr))std::terminate();
        closed_=true;
    }
    return result;
}
}
