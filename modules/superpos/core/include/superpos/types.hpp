#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <limits>
#include "result.hpp"
namespace superpos {
using Epoch = std::uint64_t;
using Tick = std::uint64_t;
using PeerId = std::uint64_t;
struct ObjectHandle {
 std::uint64_t value{};
 constexpr std::uint32_t slot() const noexcept { return static_cast<std::uint32_t>(value); }
 constexpr std::uint32_t generation() const noexcept { return static_cast<std::uint32_t>(value >> 32); }
 static constexpr ObjectHandle from_parts(std::uint32_t slot, std::uint32_t generation) noexcept {
  return { (std::uint64_t(generation) << 32) | slot };
 }
 // Worlds use one-based slots; zero slot/generation are reserved and cannot
 // resolve to an object, even when their combined integer happens to be nonzero.
 constexpr explicit operator bool() const noexcept { return value != 0 && slot() != 0 && generation() != 0; }
 constexpr bool operator==(const ObjectHandle&) const noexcept = default;
};
constexpr Result<std::uint64_t> increment(std::uint64_t value) noexcept {
 if (value == UINT64_MAX) return fail(Error::CounterExhausted);
 return value + 1;
}
constexpr Result<std::uint64_t> checked_distance(std::uint64_t newer, std::uint64_t older) noexcept {
 if (newer < older) return fail(Error::InvalidArgument);
 return newer - older;
}
// Convert only a bounded tick distance, never an absolute full-width tick.
constexpr Result<double> tick_offset(Tick tick, Tick origin, std::uint64_t maximum) noexcept {
 auto d = checked_distance(tick, origin);
 if (!d || *d > maximum || maximum > (std::uint64_t{1} << 53)) return fail(Error::InvalidArgument);
 return static_cast<double>(*d);
}
}
