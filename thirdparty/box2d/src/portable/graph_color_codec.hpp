// SPDX-License-Identifier: MIT
#pragma once
#include "broadphase_codec.hpp"
#include "joint_sim_codec.hpp"
#include "contact_codec.hpp"
#include "graph_color_bridge.h"
namespace superpos::box2d_portable {
constexpr uint32_t graph_color_kind=0x2100;
struct GraphContactEndpoints {uint32_t native_contact; int body_a,body_b;};
struct GraphBodyRole {uint32_t native_slot;uint32_t body_type;};
struct GraphColorMaps {const canonical::IdentityMap &bodies,&contacts,&joints;std::span<const GraphBodyRole> roles;
 const canonical::IdentityMap &solver_bodies;
 std::span<const GraphContactEndpoints> contact_endpoints;};
struct GraphColorImage {
 std::array<canonical::Atom,5> meta{};
 std::array<std::span<canonical::Atom>,3> storage;
 std::array<canonical::Field,8> fields{};canonical::Record record{};
 explicit GraphColorImage(std::array<std::span<canonical::Atom>,3>)noexcept;
 GraphColorImage(const GraphColorImage &)=delete;GraphColorImage &operator=(const GraphColorImage &)=delete;
};
std::span<const canonical::FieldSpec> graph_color_fields()noexcept;
Status capture_graph_color(const SpGraphColorView &,uint32_t color,canonical::Identity,const GraphColorMaps &,TreeCaptureBoundary,std::span<uint32_t> body_scratch,GraphColorImage &)noexcept;
Result<SpGraphColorView> restore_graph_color(const canonical::Record &,const GraphColorMaps &,TreeCaptureBoundary,std::span<SpContactSim>,std::span<SpJointSim>,std::span<uint64_t> body_words,std::span<uint32_t> body_scratch)noexcept;
// Diagnostic only: ordinal of the last failing check in this module (0 = none).
extern uint32_t graph_color_failure;
}
