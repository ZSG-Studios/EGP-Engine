// SPDX-License-Identifier: MIT
#pragma once
#include "shape_codec.hpp"
extern "C" {
#include "bitset.h"
}
namespace superpos::box2d_portable {
constexpr uint32_t moves_kind=0x1F00;
struct MoveBufferView { std::array<b2BitSet,3> bits{};int *moves{};uint32_t count{},capacity{}; };
struct MoveStorage { std::array<std::span<uint64_t>,3> bits;std::span<int> moves; };
struct MoveMappings { std::array<const canonical::IdentityMap*,3> proxies{}; };
struct MovesImage {
 std::array<canonical::Atom,7> meta{};std::span<canonical::Atom> references,types;
 std::array<canonical::Field,5> fields{};canonical::Record record{};
 MovesImage(std::span<canonical::Atom>,std::span<canonical::Atom>)noexcept;
 MovesImage(const MovesImage &)=delete;MovesImage &operator=(const MovesImage &)=delete;
};
std::span<const canonical::FieldSpec> moves_fields()noexcept;
// Profile is limited to BroadPhase movedProxies: inactive capacity words must be zero.
Status capture_moves(const MoveBufferView &,canonical::Identity,const MoveMappings &,std::span<uint32_t> order_scratch,MovesImage &)noexcept;
Result<MoveBufferView> restore_moves(const canonical::Record &,const MoveMappings &,std::span<uint32_t> order_scratch,MoveStorage)noexcept;
}
