// SPDX-License-Identifier: MIT
#pragma once
#include "superpos/result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace superpos::relay {
inline constexpr std::size_t maximum_datagram=1200, header_bytes=48, tag_bytes=32;
inline constexpr std::size_t maximum_inner=maximum_datagram-header_bytes-tag_bytes;
inline constexpr std::size_t handshake_payload=72, handshake_datagram=152;
inline constexpr std::size_t maximum_routes=1024;
using RouteId=std::array<std::byte,16>;
using Key=std::array<std::byte,32>;
enum class Kind:std::uint8_t { Hello=1,Challenge=2,Prove=3,Ack=4,Data=5 };
struct Header {
    Kind kind{};
    std::uint8_t side{};
    RouteId route{};
    std::uint64_t generation{},sequence{};
    std::uint32_t slot{};
};
struct Packet {Header header;std::span<const std::byte> payload,authenticated,tag;};
Result<Packet> decode(std::span<const std::byte>) noexcept;
Result<std::size_t> encode(const Header&,std::span<const std::byte> payload,std::span<std::byte> output) noexcept;
void put64(std::byte*,std::uint64_t) noexcept;
std::uint64_t get64(const std::byte*) noexcept;
void put32(std::byte*,std::uint32_t) noexcept;
std::uint32_t get32(const std::byte*) noexcept;
bool valid_range(const void*,std::size_t) noexcept;
bool overlap(const void*,std::size_t,const void*,std::size_t) noexcept;
void wipe(void*,std::size_t) noexcept;
bool nonzero(std::span<const std::byte>) noexcept;
class ReplayWindow {
    std::uint64_t highest_{},bits_{};
public:
    bool accepts(std::uint64_t) const noexcept;
    bool consume(std::uint64_t) noexcept;
    std::uint64_t highest() const noexcept {return highest_;}
};
// Provider owns its crypto/runtime lifetime. No callback may destroy a borrower.
// Keys are trusted route material; implementations must use standard HMAC-SHA256.
class MacProvider {
public:
    virtual ~MacProvider()=default;
    virtual Status sign(std::span<const std::byte,32> key,std::span<const std::byte> message,std::span<std::byte,32> tag) noexcept=0;
    virtual Status verify(std::span<const std::byte,32> key,std::span<const std::byte> message,std::span<const std::byte,32> tag) noexcept=0;
    virtual Status random(std::span<std::byte>) noexcept=0;
};
}
