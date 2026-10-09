// SPDX-License-Identifier: MIT
#pragma once
#include "geometry_root.hpp"
#include "world_events_bridge.h"
namespace superpos::box2d_portable {
enum class EventKind : uint32_t {BodyMove=0x2101,SensorBegin=0x2102,ContactBegin=0x2103,SensorEnd=0x2104,ContactEnd=0x2105,ContactHit=0x2106,Joint=0x2107};
// Public event IDs carry native slot+1, world index and generation. Every
// body/shape/contact/joint reference translates through a versioned lifetime
// map (live and retained generations), so end events may name destroyed
// objects. world0 must equal the source world index on capture and is
// replaced by the admitted destination world index on restore. Positions
// encode as IEEE double bits and must round-trip exactly in a 32-bit profile.
struct EventMaps {const canonical::VersionedIdentityMap &body,&shape,&contact,&joint;const canonical::PointerIdentityMap &bindings;uint16_t world0;};
// Column-major record: capacity, then one column per event field.
struct EventArrayImage {std::span<canonical::Atom> storage;std::array<canonical::Field,9> fields{};canonical::Record record{};
 explicit EventArrayImage(std::span<canonical::Atom>)noexcept;EventArrayImage(const EventArrayImage&)=delete;EventArrayImage&operator=(const EventArrayImage&)=delete;};
size_t event_columns(EventKind)noexcept;
size_t event_element_size(EventKind)noexcept;
// move_events maps each body-move event index to its admitted identity.
Status capture_event_array(EventKind,const SpEventArray&,canonical::Identity,const EventMaps&,const canonical::IdentityMap*move_events,EventArrayImage&)noexcept;
// destination.data must hold the recorded capacity; move_ids receives the
// body-move event identities in native order.
Status restore_event_array(EventKind,const canonical::Record&,const EventMaps&,SpEventArray&destination,std::span<canonical::Identity> move_ids)noexcept;
struct WorldEventRecords {const canonical::Record*move{},*sensor_begin{},*contact_begin{},*hit{},*joint{};std::array<const canonical::Record*,2> sensor_end{},contact_end{};};
// Owns the nine native event arrays at their recorded capacities and a new
// candidate root copied from the geometry root with the events bound. Every
// body-move event must name a live body whose cold bodyMoveIndex is that
// event, and every body's bodyMoveIndex must name its own event. The prior
// root is never modified. Task contexts, arena, debug sets, world identity and
// lifecycle flags remain unrestored; the candidate is never registered or live.
class OwnedWorldEvents {
 Allocator*allocator_{};void*block_{};size_t bytes_{};void*world_{};std::span<canonical::NativeBinding> move_bindings_;std::optional<canonical::IdentityMap> move_map_;std::optional<OwnedGeometryRoot> prior_;
 OwnedWorldEvents()noexcept=default;
 friend Result<OwnedWorldEvents>restore_owned_events(OwnedGeometryRoot&&,const WorldEventRecords&,const EventMaps&,Allocator&)noexcept;
public:
 OwnedWorldEvents(const OwnedWorldEvents&)=delete;OwnedWorldEvents&operator=(const OwnedWorldEvents&)=delete;
 OwnedWorldEvents(OwnedWorldEvents&&)noexcept;OwnedWorldEvents&operator=(OwnedWorldEvents&&)=delete;~OwnedWorldEvents();
 bool has_storage()const noexcept{return prior_.has_value();}
 const OwnedGeometryRoot&geometry_root()const noexcept{return *prior_;}
 // Never pass to native world destruction or stepping.
 const void*native_world()const noexcept{return world_;}
 // Destination body-move event index map (index -> admitted identity).
 const canonical::IdentityMap&move_event_map()const noexcept{return *move_map_;}
 size_t reserved_payload_bytes()const noexcept{return bytes_+(prior_?prior_->reserved_payload_bytes():0);}
};
Result<OwnedWorldEvents>restore_owned_events(OwnedGeometryRoot&&,const WorldEventRecords&,const EventMaps&,Allocator&)noexcept;
}
