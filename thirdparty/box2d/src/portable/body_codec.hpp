// SPDX-License-Identifier: MIT
#pragma once
#include <superpos/canonical_checkpoint.hpp>
#include <superpos/canonical_identity_map.hpp>
#include <array>
// Public math operators must remain in C++ linkage. Private C records retain
// their actual layout; MSVC's C-only _Alignas spelling is scoped to this include.
#include <box2d/math_functions.h>
#if defined(_MSC_VER) && !defined(_Alignas)
#define SUPERPOS_PRIVATE_UNDEF_ALIGNAS
#define _Alignas alignas
#endif
extern "C" {
#include "body.h"
}
#ifdef SUPERPOS_PRIVATE_UNDEF_ALIGNAS
#undef _Alignas
#undef SUPERPOS_PRIVATE_UNDEF_ALIGNAS
#endif
namespace superpos::box2d_portable {
constexpr std::uint32_t body_kind=0x1001;
constexpr std::uint32_t bodystate_kind=4098;
struct BodyStateImage {
 std::array<canonical::Atom,8> atoms{};
 std::array<canonical::Field,5> fields{};
 canonical::Record record{};
 BodyStateImage() noexcept;
 BodyStateImage(const BodyStateImage &)=delete;
 BodyStateImage &operator=(const BodyStateImage &)=delete;
};
std::span<const canonical::FieldSpec> bodystate_fields() noexcept;
Status capture_body_state(const b2BodyState &,canonical::Identity,BodyStateImage &) noexcept;
Status restore_body_state(const canonical::Record &,b2BodyState &) noexcept;
constexpr std::uint32_t bodysim_kind=4100;
struct BodySimImage {
 std::array<canonical::Atom,24> atoms{};
 std::array<canonical::Field,17> fields{};
 canonical::Record record{};
 BodySimImage() noexcept;
 BodySimImage(const BodySimImage &)=delete;
 BodySimImage &operator=(const BodySimImage &)=delete;
};
std::span<const canonical::FieldSpec> bodysim_fields() noexcept;
Status capture_body_sim(const b2BodySim &,canonical::Identity,const canonical::IdentityMap &,BodySimImage &) noexcept;
Status restore_body_sim(const canonical::Record &,const canonical::IdentityMap &,b2BodySim &) noexcept;
}
