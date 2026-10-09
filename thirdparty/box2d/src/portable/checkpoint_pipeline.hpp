// SPDX-License-Identifier: MIT
#pragma once
// Fixture-only orchestration of the private component chain for live worlds:
// capture a complete canonical checkpoint from an actual b2World, restore it
// into an owned candidate root, and derive digest maps. Uses std::vector for
// fixture storage; this is not a production API or an engine adoption path.
#include "world_digest.hpp"
#include "pool_occupants.hpp"
#include "world_pool_candidates.hpp"
#include "world_capture_bridge.h"
#include <memory>
#include <optional>
#include <vector>
namespace superpos::box2d_portable::fixture {
struct OwnedRecord {canonical::Identity identity;std::vector<canonical::Field> fields;std::vector<canonical::Atom> atoms;};
// A lifetime named by an event or sensor overlap after its object died.
struct RetiredLifetime {uint32_t pool;canonical::Identity identity,slot;uint32_t generation;};
// Process-local registry of symbolic binding IDs. A pointer that crosses a
// process boundary is named only by its symbol; each process resolves symbols
// through its own registry, and an unknown symbol or pointer is rejected.
struct SymbolBinding {uint64_t symbol{};void*pointer{};};
class SymbolRegistry {
 std::vector<SymbolBinding> by_symbol_,by_pointer_;
public:
 // Rejects zero symbols, null pointers and duplicate symbols or pointers.
 static std::optional<SymbolRegistry> make(std::vector<SymbolBinding>);
 std::optional<uint64_t> symbol(const void*)const;
 void*pointer(uint64_t symbol)const;
 size_t size()const{return by_symbol_.size();}
};
struct WorldCheckpoint {
 std::vector<std::unique_ptr<OwnedRecord>> owned;
 std::vector<canonical::Record> sets,bodies,states,islands,cold_bodies,cold_contacts,cold_joints,set_contacts,set_joints,set_payloads,graph_contacts,graph_joints,graph_payloads,shapes,shape_geometry,chains,sensors;
 std::array<std::vector<canonical::Record>,3> nodes;std::array<canonical::Record,3> trees{};std::array<canonical::Record,24> colors{};std::array<canonical::Record,9> events{};
 canonical::Record root{},graph{},moves{},pairs{},broadphase{};
 std::array<std::vector<canonical::Record>,SP_POOL_COUNT> pool_occupants;std::array<canonical::Record,SP_POOL_COUNT> pool_records{};
 // Binding table. Without a symbol registry the identities are address-ordered
 // and same-process only; with one they are {binding_kind, symbol, 1} and only
 // the symbols are serialized.
 std::vector<canonical::PointerBinding> bindings;
 std::vector<RetiredLifetime> retired;
 std::vector<std::pair<uint32_t,canonical::Record>> ordered; // visit order, for diagnostics
 // Allocation-slot identities in canonical (reservation) order: source slot
 // of the k-th slot identity of each pool, whose simulation is slot_base+k.
 std::array<std::vector<uint32_t>,SP_POOL_COUNT> slot_source;std::array<uint64_t,SP_POOL_COUNT> slot_base{};
 uint32_t record_count=0,failure_line=0;WorldDigest source_digest{};
 canonical::Record store(const canonical::Record&);
};
// Maps that keep their backing storage alive for as long as they are used.
struct MapStorage {
 std::vector<std::vector<canonical::NativeBinding>> bindings;std::vector<std::vector<uint32_t>> orders;std::vector<std::vector<canonical::VersionedNativeBinding>> versioned;
 std::vector<std::unique_ptr<canonical::IdentityMap>> maps;std::vector<std::unique_ptr<canonical::VersionedIdentityMap>> versioned_maps;std::unique_ptr<canonical::PointerIdentityMap> pointers;std::vector<canonical::PointerBinding> pointer_bindings;std::vector<uint32_t> pointer_order;
 WorldDigestMaps digest{};uint32_t failure_line=0;
};
void assign_singleton_ids(WorldDigestMaps&)noexcept;
// Lifetime registries that outlive a single capture. Each capture reconciles
// them with the world's native slots: an object alive at both captures keeps
// its canonical identity; an object born since the previous capture receives a
// fresh identity; an object that died retires its identity, which is never
// reused. Intermediate native generations of a slot are replayed as retired
// lifetimes. Allocation-slot identities are reserved once per slot. Pools
// without native generations (solver sets) treat a slot that is occupied at
// both captures as the same object.
struct PersistentRegistries {
 struct Pool {std::vector<canonical::LifetimeSlot> object_slots,allocation_slots;std::optional<canonical::LifetimeRegistry> objects,allocations;uint32_t reserved=0;};
 struct Stats {uint64_t births=0,retirements=0,survivors=0;};
 std::array<Pool,SP_POOL_COUNT> pools;uint64_t captures=0;Stats last{};
 // Canonical identity of the live object in a native slot after the last capture.
 std::optional<canonical::Identity> identity(uint32_t pool,uint32_t slot)const;
};
// Destination layout requested at capture. order[p] lists source slots of pool
// p in destination order (a permutation; empty keeps native order). The restore
// places slot identities by canonical order, so the k-th listed source slot
// lands in destination slot k (solver-set roles still fix sets 0..2). Bit p of
// reverse_free reverses that pool's recorded free order, which changes which
// slots future allocations reuse: free order is state, not storage.
struct LayoutPlan {std::array<std::vector<uint32_t>,SP_POOL_COUNT> order;uint32_t reverse_free=0;};
// Captures every component from an actual world at the runtime capture
// barrier. Lifetime registries are created for this capture in native slot
// order, so a restore preserves the native layout.
Status capture_checkpoint(const void*world,WorldCheckpoint&,Allocator&,const LayoutPlan* =nullptr,const SymbolRegistry* =nullptr,PersistentRegistries* =nullptr);
// Restores the checkpoint into an owned candidate root for destination world
// index world0 and fills destination digest maps.
Result<OwnedWorldEvents> restore_checkpoint(const WorldCheckpoint&,uint16_t world0,Allocator&,MapStorage&);
// Points the destination digest maps at the candidate's own maps. Call it
// after the candidate reaches its final location: moving the owner moves them.
void bind_candidate_maps(const OwnedWorldEvents&,MapStorage&);
// Slot-derived identities for comparing two worlds with identical native
// layouts (stepping equivalence); never used for remapped restores.
bool slot_digest_maps(const void*world,MapStorage&);
// Destination slot -> source slot for every pool of a restored candidate.
using SlotTranslation=std::array<std::vector<uint32_t>,SP_POOL_COUNT>;
bool destination_sources(const WorldCheckpoint&,const OwnedWorldEvents&,SlotTranslation&);
// Slot-derived identities after translating each native slot through
// `canonical` (slots past its size map to themselves). With the translation
// of a remapped restore, source and destination worlds name corresponding
// objects identically, so their digests compare independently of layout.
bool slot_digest_maps(const void*world,MapStorage&,const SlotTranslation*canonical);
// The same with symbolic binding identities from `symbols` (null keeps the
// address-ordered same-process identities).
bool slot_digest_maps(const void*world,MapStorage&,const SlotTranslation*canonical,const SymbolRegistry*symbols);
}
