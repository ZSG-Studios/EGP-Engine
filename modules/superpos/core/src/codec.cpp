#include "superpos/codec.hpp"
#include <charconv>
#include <cstring>
namespace superpos {
Status Writer::raw(std::span<const std::byte> v) noexcept { if(v.size()>bytes_.size()-position_) return fail(Error::Truncated);if(!v.empty())std::memcpy(bytes_.data()+position_,v.data(),v.size());position_+=v.size();return {}; }
Status Writer::u64(std::uint64_t v) noexcept { auto b=sortable_u64(v);return raw(b); }
Status Writer::varuint(std::uint64_t v) noexcept {
 std::array<std::byte,10> b{};std::size_t n=0;
 do {auto part=static_cast<unsigned>(v&127);v>>=7;b[n++]=std::byte(part|(v?128:0));} while(v);
 return raw({b.data(),n});
}
Result<std::span<const std::byte>> Reader::raw(std::size_t n) noexcept { if(n>bytes_.size()-position_)return fail(Error::Truncated);auto r=bytes_.subspan(position_,n);position_+=n;return r; }
Result<std::uint64_t> Reader::u64() noexcept { auto b=raw(8);if(!b)return fail(b.error());std::uint64_t v=0;for(auto c:*b)v=(v<<8)|std::to_integer<unsigned>(c);return v; }
Result<std::uint64_t> Reader::varuint() noexcept {
 // A failed read restores the cursor so callers can reject the complete record.
 const auto start=position_;std::uint64_t value=0;
 for(unsigned i=0;i<10;++i) {
  if(position_==bytes_.size()) {position_=start;return fail(Error::Truncated);}
  auto b=std::to_integer<unsigned>(bytes_[position_++]);
  if(i==9 && b>1) {position_=start;return fail(Error::Overflow);}
  value|=std::uint64_t(b&127)<<(i*7);
  if(!(b&128)) {if(i && b==0){position_=start;return fail(Error::NonCanonical);}return value;}
 }
 position_=start;return fail(Error::Overflow);
}
std::array<std::byte,8> sortable_u64(std::uint64_t v) noexcept {std::array<std::byte,8>b{};for(unsigned i=0;i<8;++i)b[7-i]=std::byte((v>>(8*i))&255);return b;}
Result<std::uint64_t> parse_u64(std::string_view s) noexcept {if(s.empty()||(s.size()>1&&s[0]=='0'))return fail(Error::NonCanonical);std::uint64_t v{};auto r=std::from_chars(s.data(),s.data()+s.size(),v);if(r.ec==std::errc::result_out_of_range)return fail(Error::Overflow);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())return fail(Error::InvalidArgument);return v;}
Result<std::size_t> format_u64(std::uint64_t v,std::span<char> s) noexcept {auto r=std::to_chars(s.data(),s.data()+s.size(),v);if(r.ec!=std::errc{})return fail(Error::Truncated);return static_cast<std::size_t>(r.ptr-s.data());}
}
