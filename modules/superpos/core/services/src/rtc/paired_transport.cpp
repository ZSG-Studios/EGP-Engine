// SPDX-License-Identifier: MIT
#include "superpos/service/rtc/paired_transport.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace superpos::service::pairing {
namespace {
constexpr std::size_t frame_bytes=960;
struct Enter {
    bool& active;
    explicit Enter(bool& a)noexcept:active(a){active=true;}
    ~Enter(){active=false;}
};
bool overlaps(const void* bytes,std::size_t size,const void* object,std::size_t extent)noexcept {
    if(!size)return false;
    const auto start=reinterpret_cast<std::uintptr_t>(bytes),base=reinterpret_cast<std::uintptr_t>(object);
    if(!bytes||size>std::numeric_limits<std::uintptr_t>::max()-start)return true;
    return start<base?base-start<size:start-base<extent;
}
}
struct PairedTransport::Impl {
    Registry& registry;
    NativeAdapter& native;
    OwnershipToken token;
    Token pair;
    std::array<std::uint64_t,2> associations;
    const std::thread::id owner=std::this_thread::get_id();
    struct Frame {std::array<std::byte,frame_bytes> bytes{};std::size_t size{};};
    std::array<Frame,2> pending;
    Frame received;
    CarrierLane received_lane=CarrierLane::Single;
    std::size_t next_receive{};
    Error failure=Error::None;
    bool active{};
    Impl(Registry& r,NativeAdapter& n,const Binding& b)noexcept:
        registry(r),native(n),token(b.ownership),pair(b.offer.pair),
        associations{b.control_association,b.state_association}{}
    bool own()const noexcept{return std::this_thread::get_id()==owner&&!active;}
    Status fail_closed(Error e)noexcept {if(e!=Error::Busy&&failure==Error::None)failure=e;return fail(e);}
    Status validate()noexcept {
        if(failure!=Error::None)return fail(failure);
        auto binding=registry.checked_binding(token);
        if(!binding)return fail_closed(binding.error());
        if(binding->offer.pair!=pair||binding->control_association!=associations[0]||
           binding->state_association!=associations[1])return fail_closed(Error::StaleGeneration);
        for(std::size_t i=0;i<2;++i){
            auto info=native.info(associations[i]);if(!info)return fail_closed(info.error());
            if(info->pair!=pair||info->role!=(i?CarrierLane::State:CarrierLane::Control))return fail_closed(Error::AuthenticationFailed);
            if(info->phase!=NativePhase::Bound)return fail_closed(Error::NotReady);
        }
        return {};
    }
    Status flush(std::size_t lane)noexcept {
        auto& frame=pending[lane];if(!frame.size)return {};
        if(auto state=validate();!state)return state;
        auto sent=native.try_send(associations[lane],std::span<const std::byte>(frame.bytes).first(frame.size));
        if(!sent)return fail_closed(sent.error());
        // Native true means accepted even when SCTP buffered it. Clear before
        // checking claim revocation so a later retry cannot duplicate this frame.
        if(*sent)frame.size=0;
        if(auto state=validate();!state)return state;
        return *sent?Status{}:Status(fail(Error::Busy));
    }
};
void PairedTransport::destroy()noexcept {
    if(!impl_)return;
    if(!impl_->own())std::abort();
    auto* old=std::exchange(impl_,nullptr);auto* allocator=std::exchange(allocator_,nullptr);
    std::destroy_at(old);allocator->deallocate(old);
}
PairedTransport::~PairedTransport(){destroy();}
PairedTransport::PairedTransport(PairedTransport&& other)noexcept {
    if(other.impl_&&!other.impl_->own())std::abort();
    impl_=std::exchange(other.impl_,nullptr);allocator_=std::exchange(other.allocator_,nullptr);
}
PairedTransport& PairedTransport::operator=(PairedTransport&& other)noexcept {
    if(this==&other)return *this;
    if(other.impl_&&!other.impl_->own())std::abort();destroy();
    impl_=std::exchange(other.impl_,nullptr);allocator_=std::exchange(other.allocator_,nullptr);return *this;
}
Result<PairedTransport> PairedTransport::create(Allocator& allocator,Registry& registry,NativeAdapter& native,OwnershipToken token)noexcept {
    auto binding=registry.checked_binding(token);if(!binding)return fail(binding.error());
    if(!binding->control_association||!binding->state_association||binding->control_association==binding->state_association)return fail(Error::AuthenticationFailed);
    void* memory=allocator.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);
    if(!memory)return fail(Error::OutOfMemory);
    PairedTransport result;result.impl_=std::construct_at(static_cast<Impl*>(memory),registry,native,*binding);result.allocator_=&allocator;
    // Allocation is a trusted callback and may revoke the claim.
    if(auto state=result.impl_->validate();!state)return fail(state.error());
    return result;
}
TransportCapabilities PairedTransport::capabilities()const noexcept {
    if(!impl_||!impl_->own())return {};
    return {true,true,true,false,frame_bytes,0,true};
}
bool PairedTransport::ready()const noexcept {
    if(!impl_||!impl_->own())return false;Enter enter(impl_->active);
    return bool(impl_->validate());
}
Status PairedTransport::advance()noexcept {
    auto result=advance_frames();if(!result)return fail(result.error());
    return result->control_writable&&result->state_writable?Status{}:Status(fail(Error::Busy));
}
Status PairedTransport::send(std::span<const std::byte>)noexcept{return fail(Error::Unsupported);}
Result<std::size_t> PairedTransport::receive(std::span<std::byte>)noexcept{return fail(Error::Unsupported);}
Result<CarrierProgress> PairedTransport::advance_frames()noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own())return fail(Error::PermissionDenied);Enter enter(impl_->active);
    if(auto state=impl_->validate();!state)return fail(state.error());
    auto progress=impl_->native.poll();
    if(auto state=impl_->validate();!state)return fail(state.error());
    if(!progress){impl_->fail_closed(progress.error());return fail(progress.error());}
    // A blocked data lane must not prevent progress on control (or vice versa).
    for(std::size_t i=0;i<2;++i){auto flushed=impl_->flush(i);if(!flushed&&flushed.error()!=Error::Busy)return fail(flushed.error());}
    return CarrierProgress{impl_->pending[0].size==0,impl_->pending[1].size==0};
}
Status PairedTransport::send_frame(std::span<const std::byte> bytes,CarrierLane lane)noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own())return fail(Error::PermissionDenied);Enter enter(impl_->active);
    if(lane!=CarrierLane::Control&&lane!=CarrierLane::State)return fail(Error::InvalidArgument);
    if(bytes.empty())return fail(Error::InvalidArgument);if(bytes.size()>frame_bytes)return fail(Error::CapacityExceeded);
    if(overlaps(bytes.data(),bytes.size(),this,sizeof(*this))||overlaps(bytes.data(),bytes.size(),impl_,sizeof(Impl)))return fail(Error::InvalidArgument);
    const auto index=lane==CarrierLane::Control?0u:1u;auto& frame=impl_->pending[index];
    if(frame.size)return fail(Error::CapacityExceeded);
    if(impl_->failure!=Error::None)return fail(impl_->failure);
    std::copy(bytes.begin(),bytes.end(),frame.bytes.begin());frame.size=bytes.size();
    // Every Busy send owns the exact original payload and lane.
    return impl_->flush(index);
}
Result<CarrierFrame> PairedTransport::receive_frame(std::span<std::byte> bytes)noexcept {
    if(!impl_)return fail(Error::NotReady);if(!impl_->own())return fail(Error::PermissionDenied);Enter enter(impl_->active);
    if(overlaps(bytes.data(),bytes.size(),this,sizeof(*this))||overlaps(bytes.data(),bytes.size(),impl_,sizeof(Impl)))return fail(Error::InvalidArgument);
    if(auto state=impl_->validate();!state)return fail(state.error());
    auto& frame=impl_->received;
    if(!frame.size){
        for(std::size_t n=0;n<2;++n){
            const auto index=(impl_->next_receive+n)%2;
            auto read=impl_->native.receive_frame(impl_->associations[index],std::span<std::byte,frame_bytes>(frame.bytes));
            if(read){
                if(!*read||*read>frame_bytes){impl_->fail_closed(Error::ProtocolViolation);return fail(Error::ProtocolViolation);}
                frame.size=*read;impl_->received_lane=index?CarrierLane::State:CarrierLane::Control;impl_->next_receive=1-index;
            }
            if(auto state=impl_->validate();!state)return fail(state.error());
            if(!read){if(read.error()==Error::NotReady||read.error()==Error::Busy)continue;impl_->fail_closed(read.error());return fail(read.error());}
            break;
        }
    }
    // TransportProvider uses Busy for an empty live carrier. NotReady is a
    // terminal/unavailable provider state to Session::pump.
    if(!frame.size)return fail(Error::Busy);
    if(bytes.size()<frame.size)return fail(Error::CapacityExceeded);
    std::copy_n(frame.bytes.begin(),frame.size,bytes.begin());const auto size=std::exchange(frame.size,0);
    return CarrierFrame{size,impl_->received_lane};
}
}
