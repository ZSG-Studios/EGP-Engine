// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_codec.hpp"
#include "chain_bridge.h"
namespace superpos::box2d_portable {
struct ChainMaps {const canonical::IdentityMap &chain,&body,&shape;};
struct ChainImage {std::array<canonical::Atom,4> scalars{};std::array<std::span<canonical::Atom>,7> storage;std::array<canonical::Field,11> fields{};canonical::Record record{};
 explicit ChainImage(std::array<std::span<canonical::Atom>,7>)noexcept;ChainImage(const ChainImage &)=delete;ChainImage &operator=(const ChainImage &)=delete;};
std::span<const canonical::FieldSpec> chain_fields()noexcept;
Status capture_chain(const SpChainView &,canonical::Identity,const ChainMaps &,std::span<canonical::Identity>,ChainImage &)noexcept;
Status restore_chain(const canonical::Record &,const ChainMaps &,std::span<canonical::Identity>,SpChainView &)noexcept;
}
