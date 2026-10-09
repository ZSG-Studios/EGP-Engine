// SPDX-License-Identifier: MIT
#include "superpos/service/admission_wire.hpp"
#include <algorithm>

namespace superpos::service::admission {
namespace {
bool valid(const Challenge& c) noexcept {
    return c.match && c.session && c.connection_incarnation && c.allowed_permissions &&
        !(c.allowed_permissions & ~known_permissions) &&
        std::any_of(c.nonce.begin(),c.nonce.end(),[](std::byte b){return b!=std::byte{};});
}
bool valid(const Request& r) noexcept {
    return valid(r.challenge) && r.actor && r.requested_permissions &&
        !(r.requested_permissions & ~r.challenge.allowed_permissions);
}
void header(std::span<std::byte> out,unsigned kind) noexcept {
    out[0]=std::byte{'S'};out[1]=std::byte{'P'};out[2]=std::byte{'A'};out[3]=std::byte{'C'};
    out[4]=std::byte{1};out[5]=std::byte(kind);out[6]=out[7]=std::byte{};
}
bool header_valid(std::span<const std::byte> in,unsigned kind) noexcept {
    return in[0]==std::byte{'S'} && in[1]==std::byte{'P'} && in[2]==std::byte{'A'} && in[3]==std::byte{'C'} &&
        in[4]==std::byte{1} && in[5]==std::byte(kind) && in[6]==std::byte{} && in[7]==std::byte{};
}
void put(std::span<std::byte> out,std::size_t offset,std::uint64_t v) noexcept {
    for(unsigned i=0;i!=8;++i)out[offset+i]=std::byte((v>>(i*8))&255);
}
std::uint64_t get(std::span<const std::byte> in,std::size_t offset) noexcept {
    std::uint64_t v{};for(unsigned i=0;i!=8;++i)v|=std::uint64_t(std::to_integer<unsigned>(in[offset+i]))<<(i*8);return v;
}
void challenge_body(const Challenge& c,std::span<std::byte> out) noexcept {
    put(out,8,c.match);put(out,16,c.session);put(out,24,c.authority_epoch);
    put(out,32,c.connection_incarnation);put(out,40,c.allowed_permissions);
    std::copy(c.nonce.begin(),c.nonce.end(),out.begin()+48);
}
Challenge challenge_body(std::span<const std::byte> in) noexcept {
    Challenge c{get(in,8),get(in,16),get(in,24),get(in,32),get(in,40),{}};
    std::copy_n(in.begin()+48,32,c.nonce.begin());return c;
}
}
Status encode_challenge(const Challenge& c,std::span<std::byte> out) noexcept {
    if(out.size()!=challenge_bytes)return fail(Error::InvalidArgument);
    if(!valid(c))return fail(Error::InvalidArgument);
    std::array<std::byte,challenge_bytes> bytes{};header(bytes,1);challenge_body(c,bytes);
    std::copy(bytes.begin(),bytes.end(),out.begin());return {};
}
Result<Challenge> decode_challenge(std::span<const std::byte> in) noexcept {
    if(in.size()!=challenge_bytes)return fail(Error::ProtocolViolation);
    if(!header_valid(in,1))return fail(Error::ProtocolViolation);
    auto c=challenge_body(in);if(!valid(c))return fail(Error::ProtocolViolation);return c;
}
Status encode_request(const Request& r,std::span<std::byte> out) noexcept {
    if(out.size()!=request_bytes || !valid(r))return fail(Error::InvalidArgument);
    std::array<std::byte,request_bytes> bytes{};header(bytes,2);challenge_body(r.challenge,bytes);
    put(bytes,80,r.actor);put(bytes,88,r.actor_epoch);put(bytes,96,r.requested_permissions);
    // Eight reserved bytes keep the authenticated request prefix extensible
    // without changing the fixed tag offset; version1 requires them all zero.
    std::copy(r.proof.begin(),r.proof.end(),bytes.begin()+authenticated_request_bytes);
    std::copy(bytes.begin(),bytes.end(),out.begin());return {};
}
Result<Request> decode_request(std::span<const std::byte> in) noexcept {
    if(in.size()!=request_bytes || !header_valid(in,2))return fail(Error::ProtocolViolation);
    for(std::size_t i=104;i<authenticated_request_bytes;++i)if(in[i]!=std::byte{})return fail(Error::ProtocolViolation);
    Request r{challenge_body(in),get(in,80),get(in,88),get(in,96),{}};
    std::copy_n(in.begin()+authenticated_request_bytes,32,r.proof.begin());
    if(!valid(r))return fail(Error::ProtocolViolation);return r;
}
Status matches_issued(const Request& r,const Challenge& c) noexcept {
    if(!valid(r) || !valid(c))return fail(Error::InvalidArgument);
    if(r.challenge!=c)return fail(Error::AuthenticationFailed);
    return {};
}
}
