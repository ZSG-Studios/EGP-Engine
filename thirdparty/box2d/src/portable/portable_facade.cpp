// SPDX-License-Identifier: MIT
#include "portable_facade.h"
#include "checkpoint_wire.hpp"
#include "world_adoption.h"
#include <new>
#include <superpos/allocator.hpp>
using namespace superpos;using namespace superpos::box2d_portable;using namespace superpos::box2d_portable::fixture;
struct SpB2PortableState {PersistentRegistries registries;};
namespace {
struct Budget {MemoryPlan plan;BudgetAllocator allocator;Budget():plan(make()),allocator(plan){}
 static MemoryPlan make(){MemoryPlan p;p.limits.fill(0);p.limits[unsigned(MemoryDomain::Recovery)]=256u<<20;return p;}};
std::optional<SymbolRegistry> registry_of(const SpB2Symbol*symbols,uint32_t count){if(count&&!symbols)return std::nullopt;std::vector<SymbolBinding> b;b.reserve(count);for(uint32_t i=0;i<count;++i)b.push_back({symbols[i].symbol,symbols[i].pointer});return SymbolRegistry::make(std::move(b));}
SpB2PortableStatus decode_status(Error e){switch(e){case Error::PermissionDenied:return SP_B2_PORTABLE_UNKNOWN_SYMBOL;case Error::IncompatibleSchema:return SP_B2_PORTABLE_INCOMPATIBLE;case Error::OutOfMemory:return SP_B2_PORTABLE_NO_MEMORY;default:return SP_B2_PORTABLE_CORRUPT;}}
}
extern "C" {
SpB2PortableState*spB2PortableCreateState(void){return new(std::nothrow) SpB2PortableState();}
void spB2PortableDestroyState(SpB2PortableState*s){delete s;}
uint32_t spB2PortablePositionBits(void){return checkpoint_position_bits();}
SpB2PortableStatus spB2PortableCapture(b2WorldId id,SpB2PortableState*state,const SpB2Symbol*symbols,uint32_t count,SpB2ByteSink*sink,void*context,SpB2CaptureStats*stats){
 void*world=spB2PortableWorldPointer(id);if(!world||!state||!sink)return SP_B2_PORTABLE_INVALID;auto registry=registry_of(symbols,count);if(!registry)return SP_B2_PORTABLE_INVALID;
 Budget budget;WorldCheckpoint cp;auto captured=capture_checkpoint(world,cp,budget.allocator,nullptr,&*registry,&state->registries);
 if(!captured&&stats)stats->failure_line=cp.failure_line;
 if(!captured)return captured.error()==Error::PermissionDenied?SP_B2_PORTABLE_UNKNOWN_SYMBOL:captured.error()==Error::NotReady?SP_B2_PORTABLE_BUSY:captured.error()==Error::OutOfMemory?SP_B2_PORTABLE_NO_MEMORY:SP_B2_PORTABLE_INVALID;
 auto bytes=encode_checkpoint(cp);if(bytes.empty())return SP_B2_PORTABLE_INVALID;
 if(stats){const auto&r=state->registries;*stats={r.captures,r.last.births,r.last.retirements,r.last.survivors,cp.record_count,uint32_t(cp.bindings.size()),0};}
 sink(context,reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());return SP_B2_PORTABLE_OK;}
SpB2PortableStatus spB2PortableRestore(const uint8_t*bytes,size_t size,const SpB2Symbol*symbols,uint32_t count,b2WorldId live){
 void*world=spB2PortableWorldPointer(live);if(!world||(!bytes&&size))return SP_B2_PORTABLE_INVALID;if(!spB2PortableWorldEmpty(live))return SP_B2_PORTABLE_ADOPT_FAILED;
 auto registry=registry_of(symbols,count);if(!registry)return SP_B2_PORTABLE_INVALID;
 Budget budget;WorldCheckpoint cp;if(auto d=decode_checkpoint(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes),size),*registry,cp);!d)return decode_status(d.error());
 MapStorage dm;auto candidate=restore_checkpoint(cp,uint16_t(live.index1-1),budget.allocator,dm);if(!candidate)return candidate.error()==Error::OutOfMemory?SP_B2_PORTABLE_NO_MEMORY:SP_B2_PORTABLE_CORRUPT;
 bind_candidate_maps(*candidate,dm);auto cd=digest_world(candidate->native_world(),dm.digest,budget.allocator);if(!cd||*cd!=cp.source_digest)return SP_B2_PORTABLE_DIGEST_MISMATCH;
 if(!spAdoptCandidateWorld(world,candidate->native_world()))return SP_B2_PORTABLE_ADOPT_FAILED;
 auto ad=digest_world(world,dm.digest,budget.allocator);return ad&&*ad==cp.source_digest?SP_B2_PORTABLE_OK:SP_B2_PORTABLE_DIGEST_MISMATCH;}
SpB2PortableStatus spB2PortableDigest(b2WorldId id,const SpB2Symbol*symbols,uint32_t count,uint8_t out[32]){
 void*world=spB2PortableWorldPointer(id);if(!world||!out)return SP_B2_PORTABLE_INVALID;auto registry=registry_of(symbols,count);if(!registry)return SP_B2_PORTABLE_INVALID;
 Budget budget;MapStorage ms;if(!slot_digest_maps(world,ms,nullptr,&*registry))return SP_B2_PORTABLE_UNKNOWN_SYMBOL;auto d=digest_world(world,ms.digest,budget.allocator);if(!d)return SP_B2_PORTABLE_BUSY;
 for(size_t i=0;i<32;++i)out[i]=uint8_t((*d)[i]);return SP_B2_PORTABLE_OK;}
uint64_t spB2PortableIdentity(const SpB2PortableState*s,uint32_t pool,uint32_t slot){if(!s)return 0;auto id=s->registries.identity(pool,slot);return id?id->simulation:0;}
}
