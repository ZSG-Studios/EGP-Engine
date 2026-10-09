// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace superpos::box2d_portable {
// Self-contained FIPS 180-4 SHA-256 for canonical state digests. It has no
// allocation, callbacks or platform dependencies.
class Sha256 {
 std::array<uint32_t,8> h_;std::array<uint8_t,64> block_{};uint64_t bits_{};size_t used_{};
 void compress(const uint8_t*)noexcept;
public:
 Sha256()noexcept;
 void update(std::span<const std::byte>)noexcept;
 void update_u32(uint32_t)noexcept;
 void update_u64(uint64_t)noexcept;
 std::array<std::byte,32> finish()noexcept;
};
}
