// SPDX-License-Identifier: MIT
#include "superpos/relay_psa.hpp"
#include <psa/crypto.h>
#include <algorithm>
#include <cstdlib>
namespace superpos::relay {
namespace {
struct Busy {bool& value;explicit Busy(bool& v):value(v){value=true;}~Busy(){value=false;}};
template<std::size_t N>struct Scratch{std::array<std::byte,N> bytes{};~Scratch(){wipe(bytes.data(),N);}};
Error error(psa_status_t status) noexcept {return status==PSA_ERROR_INSUFFICIENT_MEMORY?Error::OutOfMemory:Error::AuthenticationFailed;}
struct KeyOwner{mbedtls_svc_key_id_t key{};~KeyOwner(){if(!mbedtls_svc_key_id_is_null(key)&&psa_destroy_key(key)!=PSA_SUCCESS)std::abort();}};
Status import(std::span<const std::byte,32> bytes,psa_key_usage_t usage,KeyOwner& out) noexcept {
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;psa_set_key_type(&attributes,PSA_KEY_TYPE_HMAC);psa_set_key_bits(&attributes,256);
    psa_set_key_usage_flags(&attributes,usage);psa_set_key_algorithm(&attributes,PSA_ALG_HMAC(PSA_ALG_SHA_256));
    auto result=psa_import_key(&attributes,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),&out.key);psa_reset_key_attributes(&attributes);
    return result==PSA_SUCCESS?Status{}:Status(fail(error(result)));
}
}
Status PsaRelayMac::initialize() noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);if(ready_)return fail(Error::Busy);
    Busy guard(busy_);Scratch<1> probe;
    if(psa_generate_random(reinterpret_cast<unsigned char*>(probe.bytes.data()),1)!=PSA_SUCCESS)return fail(Error::NotReady);
    ready_=true;return {};
}
PsaRelayMac::~PsaRelayMac(){if(owner_!=std::this_thread::get_id()||busy_)std::abort();}
Status PsaRelayMac::sign(std::span<const std::byte,32> key,std::span<const std::byte> message,std::span<std::byte,32> output) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);if(!ready_)return fail(Error::NotReady);
    if(message.size()>maximum_datagram||!valid_range(message.data(),message.size())||!valid_range(key.data(),key.size())||!valid_range(output.data(),output.size())||
       overlap(output.data(),output.size(),key.data(),key.size())||overlap(output.data(),output.size(),message.data(),message.size())||overlap(output.data(),output.size(),this,sizeof(*this))||
       overlap(output.data(),output.size(),&runtime_,sizeof(runtime_))||overlap(key.data(),key.size(),this,sizeof(*this))||overlap(message.data(),message.size(),this,sizeof(*this)))return fail(Error::InvalidArgument);
    Scratch<32> copied_key,tag;Scratch<maximum_datagram> copied;std::copy(key.begin(),key.end(),copied_key.bytes.begin());std::copy(message.begin(),message.end(),copied.bytes.begin());Busy busy(busy_);
    KeyOwner imported;if(auto value=import(copied_key.bytes,PSA_KEY_USAGE_SIGN_MESSAGE,imported);!value)return value;
    std::size_t written{};auto result=psa_mac_compute(imported.key,PSA_ALG_HMAC(PSA_ALG_SHA_256),reinterpret_cast<const unsigned char*>(copied.bytes.data()),message.size(),reinterpret_cast<unsigned char*>(tag.bytes.data()),32,&written);
    if(result!=PSA_SUCCESS)return fail(error(result));if(written!=32)return fail(Error::ProtocolViolation);std::copy(tag.bytes.begin(),tag.bytes.end(),output.begin());return {};
}
Status PsaRelayMac::verify(std::span<const std::byte,32> key,std::span<const std::byte> message,std::span<const std::byte,32> tag) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);if(!ready_)return fail(Error::NotReady);
    if(message.size()>maximum_datagram||!valid_range(message.data(),message.size())||!valid_range(key.data(),key.size())||!valid_range(tag.data(),tag.size())||
       overlap(key.data(),key.size(),this,sizeof(*this))||overlap(message.data(),message.size(),this,sizeof(*this))||overlap(tag.data(),tag.size(),this,sizeof(*this)))return fail(Error::InvalidArgument);
    Scratch<32> copied_key,copied_tag;Scratch<maximum_datagram> copied;std::copy(key.begin(),key.end(),copied_key.bytes.begin());std::copy(tag.begin(),tag.end(),copied_tag.bytes.begin());std::copy(message.begin(),message.end(),copied.bytes.begin());Busy busy(busy_);
    KeyOwner imported;if(auto value=import(copied_key.bytes,PSA_KEY_USAGE_VERIFY_MESSAGE,imported);!value)return value;
    auto result=psa_mac_verify(imported.key,PSA_ALG_HMAC(PSA_ALG_SHA_256),reinterpret_cast<const unsigned char*>(copied.bytes.data()),message.size(),reinterpret_cast<const unsigned char*>(copied_tag.bytes.data()),32);
    return result==PSA_SUCCESS?Status{}:Status(fail(error(result)));
}
Status PsaRelayMac::random(std::span<std::byte> output) noexcept {
    if(owner_!=std::this_thread::get_id())return fail(Error::PermissionDenied);if(busy_)return fail(Error::Busy);if(!ready_)return fail(Error::NotReady);
    if(output.empty()||output.size()>64||!valid_range(output.data(),output.size())||overlap(output.data(),output.size(),this,sizeof(*this))||overlap(output.data(),output.size(),&runtime_,sizeof(runtime_)))return fail(Error::InvalidArgument);
    Busy busy(busy_);Scratch<64> bytes;
    auto result=psa_generate_random(reinterpret_cast<unsigned char*>(bytes.bytes.data()),output.size());if(result!=PSA_SUCCESS)return fail(error(result));std::copy_n(bytes.bytes.begin(),output.size(),output.begin());return {};
}
}
