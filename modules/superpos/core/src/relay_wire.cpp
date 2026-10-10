// SPDX-License-Identifier: MIT
#include "superpos/relay_wire.hpp"
#include <algorithm>
#include <limits>
namespace superpos::relay {
void wipe(void* p,std::size_t size) noexcept {auto* out=static_cast<volatile unsigned char*>(p);while(size--)*out++=0;}
bool valid_range(const void* p,std::size_t n) noexcept {return !n||(p&&n<=std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(p));}
bool overlap(const void* a,std::size_t an,const void* b,std::size_t bn) noexcept {
    if(!valid_range(a,an)||!valid_range(b,bn))return true;if(!an||!bn)return false;
    auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);return x<y?y-x<an:x-y<bn;
}
bool nonzero(std::span<const std::byte> bytes) noexcept {unsigned value=0;for(auto b:bytes)value|=std::to_integer<unsigned>(b);return value!=0;}
void put64(std::byte* p,std::uint64_t n) noexcept {for(unsigned i=0;i<8;++i)p[i]=std::byte(n>>(56-8*i));}
std::uint64_t get64(const std::byte* p) noexcept {std::uint64_t n=0;for(unsigned i=0;i<8;++i)n=(n<<8)|std::to_integer<unsigned>(p[i]);return n;}
void put32(std::byte* p,std::uint32_t n) noexcept {for(unsigned i=0;i<4;++i)p[i]=std::byte(n>>(24-8*i));}
std::uint32_t get32(const std::byte* p) noexcept {std::uint32_t n=0;for(unsigned i=0;i<4;++i)n=(n<<8)|std::to_integer<unsigned>(p[i]);return n;}
namespace {
bool kind_valid(Kind kind) noexcept {return kind>=Kind::Hello&&kind<=Kind::Data;}
constexpr std::array<std::byte,4> magic{std::byte{'S'},std::byte{'P'},std::byte{'R'},std::byte{'1'}};
}
Result<Packet> decode(std::span<const std::byte> bytes) noexcept {
    if(!valid_range(bytes.data(),bytes.size())||bytes.size()<header_bytes+tag_bytes||bytes.size()>maximum_datagram)return fail(Error::ProtocolViolation);
    if(!std::equal(magic.begin(),magic.end(),bytes.begin())||bytes[4]!=std::byte{1}||bytes[7]!=std::byte{})return fail(Error::ProtocolViolation);
    for(std::size_t i=46;i<48;++i)if(bytes[i]!=std::byte{})return fail(Error::ProtocolViolation);
    Header header;header.kind=static_cast<Kind>(std::to_integer<unsigned>(bytes[5]));header.side=std::to_integer<std::uint8_t>(bytes[6]);
    std::copy_n(bytes.begin()+8,16,header.route.begin());header.generation=get64(bytes.data()+24);header.sequence=get64(bytes.data()+32);header.slot=get32(bytes.data()+42);
    const auto size=(std::to_integer<std::size_t>(bytes[40])<<8)|std::to_integer<std::size_t>(bytes[41]);
    if(!kind_valid(header.kind)||header.side>1||header.slot>=maximum_routes||!nonzero(header.route)||!header.generation||!header.sequence||size!=bytes.size()-header_bytes-tag_bytes)return fail(Error::ProtocolViolation);
    if((header.kind==Kind::Data&&(size==0||size>maximum_inner))||(header.kind!=Kind::Data&&size!=handshake_payload))return fail(Error::ProtocolViolation);
    return Packet{header,bytes.subspan(header_bytes,size),bytes.first(header_bytes+size),bytes.last(tag_bytes)};
}
Result<std::size_t> encode(const Header& header,std::span<const std::byte> payload,std::span<std::byte> out) noexcept {
    if(!kind_valid(header.kind)||header.side>1||header.slot>=maximum_routes||!nonzero(header.route)||!header.generation||!header.sequence||
       !valid_range(payload.data(),payload.size())||!valid_range(out.data(),out.size())||
       overlap(payload.data(),payload.size(),out.data(),out.size())||overlap(&header,sizeof(header),out.data(),out.size()))return fail(Error::InvalidArgument);
    if((header.kind==Kind::Data&&(payload.empty()||payload.size()>maximum_inner))||(header.kind!=Kind::Data&&payload.size()!=handshake_payload))return fail(Error::InvalidArgument);
    auto count=header_bytes+payload.size()+tag_bytes;if(out.size()<count)return fail(Error::CapacityExceeded);
    std::fill_n(out.begin(),count,std::byte{});std::copy(magic.begin(),magic.end(),out.begin());out[4]=std::byte{1};out[5]=std::byte(header.kind);out[6]=std::byte(header.side);
    std::copy(header.route.begin(),header.route.end(),out.begin()+8);put64(out.data()+24,header.generation);put64(out.data()+32,header.sequence);
    out[40]=std::byte(payload.size()>>8);out[41]=std::byte(payload.size());put32(out.data()+42,header.slot);std::copy(payload.begin(),payload.end(),out.begin()+header_bytes);return count;
}
bool ReplayWindow::accepts(std::uint64_t n) const noexcept {
    if(!n)return false;if(n>highest_)return true;auto distance=highest_-n;return distance<64&&!(bits_&(std::uint64_t{1}<<distance));
}
bool ReplayWindow::consume(std::uint64_t n) noexcept {
    if(!accepts(n))return false;
    if(n>highest_){auto delta=n-highest_;bits_=delta>=64?1:(bits_<<delta)|1;highest_=n;}
    else bits_|=std::uint64_t{1}<<(highest_-n);return true;
}
}
