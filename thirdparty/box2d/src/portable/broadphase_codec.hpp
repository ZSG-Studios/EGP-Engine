// SPDX-License-Identifier: MIT
#pragma once
#include "dynamic_tree_codec.hpp"
#include "moves_codec.hpp"
#include "pair_set_codec.hpp"
#include "broadphase_bridge.h"
namespace superpos::box2d_portable {
static_assert(b2_bodyTypeCount==3,"Unqualified body-type profile");
constexpr uint32_t broadphase_kind=0x2000;
struct BroadPhaseIds {std::array<canonical::Identity,3> trees;canonical::Identity moves,pairs;};
struct BroadPhaseMaps {std::array<const canonical::IdentityMap*,3> nodes,proxies;const canonical::IdentityMap* shapes;};
struct BroadPhaseScratch {std::span<uint8_t> marks;std::span<uint32_t> order;std::span<uint64_t> identities;};
// Child native values must come from successful, matching child-record restore.
// Borrowed buffers remain owned by the caller; this is not an engine adoption token.
struct BroadPhaseParts {std::array<b2DynamicTree,3> trees;MoveBufferView moves;b2HashSet pairs;BroadPhaseIds ids;};
struct BroadPhaseImage {std::array<canonical::Atom,5> atoms{};std::array<canonical::Field,3> fields{};canonical::Record record{};BroadPhaseImage()noexcept;};
std::span<const canonical::FieldSpec> broadphase_fields()noexcept;
Status capture_broadphase(const SpBroadPhaseView &,canonical::Identity,const BroadPhaseIds &,const BroadPhaseMaps &,TreeCaptureBoundary,BroadPhaseScratch,BroadPhaseImage &)noexcept;
Result<SpBroadPhaseView> restore_broadphase(const canonical::Record &,const BroadPhaseParts &,const BroadPhaseMaps &,TreeCaptureBoundary,BroadPhaseScratch)noexcept;
}
