// SPDX-License-Identifier: MIT
#pragma once
#include "visitor_codec.hpp"
#include "sensor_bridge.h"
namespace superpos::box2d_portable {
constexpr uint32_t sensor_kind=0x1602;
struct SensorImage {std::array<std::span<canonical::Atom>,3> storage;std::array<canonical::Atom,4> scalars{};std::array<canonical::Field,7> fields{};canonical::Record record{};
 explicit SensorImage(std::array<std::span<canonical::Atom>,3>)noexcept;SensorImage(const SensorImage &)=delete;SensorImage &operator=(const SensorImage &)=delete;};
std::span<const canonical::FieldSpec> sensor_fields()noexcept;
// This profile requires the post-overlap normalized capture barrier (hits empty).
// Canonical overlap columns are sorted by stable lifetime identity. Native
// restoration sorts them again by successor-local shape slot for Box2D's merge.
Status capture_sensor(const SpSensorView &,canonical::Identity,const canonical::IdentityMap &active_shapes,const canonical::VersionedIdentityMap &history,std::span<canonical::Identity> scratch,SensorImage &)noexcept;
Status restore_sensor(const canonical::Record &,const canonical::IdentityMap &active_shapes,const canonical::VersionedIdentityMap &history,std::span<SpVisitor> scratch,SpSensorView &)noexcept;
}
