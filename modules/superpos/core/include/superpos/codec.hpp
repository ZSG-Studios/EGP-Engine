#pragma once
#include "types.hpp"
#include <array>
#include <string_view>
namespace superpos {
class Writer {
 std::span<std::byte> bytes_; std::size_t position_{};
public:
 explicit Writer(std::span<std::byte> bytes) noexcept : bytes_(bytes) {}
 Status u64(std::uint64_t value) noexcept;
 Status varuint(std::uint64_t value) noexcept;
 Status raw(std::span<const std::byte> value) noexcept;
 std::size_t size() const noexcept { return position_; }
};
class Reader {
 std::span<const std::byte> bytes_;std::size_t position_{};
public:
 explicit Reader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}
 Result<std::uint64_t> u64() noexcept;
 Result<std::uint64_t> varuint() noexcept;
 Result<std::span<const std::byte>> raw(std::size_t size) noexcept;
 bool empty() const noexcept { return position_==bytes_.size(); }
 std::span<const std::byte> remaining() const noexcept { return bytes_.subspan(position_); }
};
std::array<std::byte,8> sortable_u64(std::uint64_t value) noexcept;
Result<std::uint64_t> parse_u64(std::string_view text) noexcept;
Result<std::size_t> format_u64(std::uint64_t value,std::span<char> output) noexcept;
}
