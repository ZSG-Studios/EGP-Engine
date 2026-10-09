// SPDX-License-Identifier: MIT
#include "superpos/relay_dtls.hpp"
#include <algorithm>
#include <cstdlib>
namespace superpos::relay {
namespace {struct Busy{bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};}
RelayedDtls::RelayedDtls(Allocator& a,Clock& c,RelayClient& r,AuthProvider& auth,DtlsConfig config,std::span<const std::byte> address) noexcept:
    allocator_(a),clock_(c),relay_(r),auth_(auth),config_(config){
    config_.udp_payload_ceiling=maximum_inner;
    if(valid_range(address.data(),address.size())&&!address.empty()&&address.size()<=address_.size()){
        std::copy(address.begin(),address.end(),address_.begin());address_size_=address.size();
    }
}
RelayedDtls::~RelayedDtls(){if(owner_!=std::this_thread::get_id()||busy_)std::abort();dtls_.reset();}
TransportCapabilities RelayedDtls::capabilities() const noexcept {return {true,true,true,false,DtlsAssociation::maximum_frame_bytes,37};}
bool RelayedDtls::ready() const noexcept {if(owner_!=std::this_thread::get_id()||busy_)return false;auto state=relay_.state();return state&&*state==ClientState::Ready&&dtls_&&dtls_->ready();}
Status RelayedDtls::advance() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);Busy guard(busy_);
    auto relay=relay_.advance();if(!relay&&relay.error()!=Error::Busy)return relay;
    auto state=relay_.state();if(!state)return fail(state.error());if(*state!=ClientState::Ready)return fail(Error::Busy);
    if(!dtls_){if(!address_size_)return fail(Error::InvalidArgument);auto made=DtlsAssociation::create(allocator_,clock_,relay_,auth_,config_,std::span(address_).first(address_size_));if(!made)return fail(made.error());dtls_.emplace(std::move(*made));}
    auto native=dtls_->advance();auto flushed=relay_.advance();if(!flushed&&flushed.error()!=Error::Busy)return flushed;return native;
}
Status RelayedDtls::send(std::span<const std::byte> bytes) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);
    if(!valid_range(bytes.data(),bytes.size())||overlap(bytes.data(),bytes.size(),this,sizeof(*this)))return fail(Error::InvalidArgument);
    Busy guard(busy_);if(!dtls_)return fail(Error::NotReady);return dtls_->send(bytes);
}
Result<std::size_t> RelayedDtls::receive(std::span<std::byte> bytes) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);
    if(!valid_range(bytes.data(),bytes.size())||overlap(bytes.data(),bytes.size(),this,sizeof(*this)))return fail(Error::InvalidArgument);
    Busy guard(busy_);if(!dtls_)return fail(Error::NotReady);return dtls_->receive(bytes);
}
}
