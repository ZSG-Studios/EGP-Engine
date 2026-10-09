// SPDX-License-Identifier: MIT
#pragma once
// C ABI over the portable Box2D checkpoint pipeline, for engine code that is
// compiled as C++17. The implementation is C++23 and owns every allocation.
#include "box2d/id.h"
#include "portable_world_access.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Persistent lifetime registries for one live world. Identities of objects
// that survive between captures are stable; births get fresh identities and
// deaths retire theirs.
typedef struct SpB2PortableState SpB2PortableState;
// A pointer the world holds, named by a symbol. Each process supplies its own
// table; checkpoints carry symbols only.
typedef struct SpB2Symbol {uint64_t symbol;void*pointer;} SpB2Symbol;
typedef struct SpB2CaptureStats {uint64_t captures,births,retirements,survivors;uint32_t records,bindings,failure_line;} SpB2CaptureStats;
typedef void SpB2ByteSink(void*context,const uint8_t*bytes,size_t size);
typedef enum SpB2PortableStatus {
 SP_B2_PORTABLE_OK=0,
 SP_B2_PORTABLE_BUSY,             // runtime scratch not quiescent or world locked
 SP_B2_PORTABLE_INVALID,          // invalid world, state or arguments
 SP_B2_PORTABLE_UNKNOWN_SYMBOL,   // a pointer or symbol absent from the registry
 SP_B2_PORTABLE_INCOMPATIBLE,     // other format version or position width
 SP_B2_PORTABLE_CORRUPT,          // truncated, tampered or non-canonical bytes
 SP_B2_PORTABLE_DIGEST_MISMATCH,  // restored state differs from the embedded digest
 SP_B2_PORTABLE_ADOPT_FAILED,     // the live world is not an empty adoption target
 SP_B2_PORTABLE_NO_MEMORY
} SpB2PortableStatus;
SpB2PortableState*spB2PortableCreateState(void);
void spB2PortableDestroyState(SpB2PortableState*);
// Captures a complete checkpoint (SPB2WCK1 bytes) through the persistent
// registries and passes it to `sink`. The world must be at its runtime
// capture barrier (not locked, task contexts drained).
SpB2PortableStatus spB2PortableCapture(b2WorldId world,SpB2PortableState*state,const SpB2Symbol*symbols,uint32_t symbol_count,SpB2ByteSink*sink,void*context,SpB2CaptureStats*stats);
// Decodes, restores, verifies the embedded digest, adopts into the empty live
// world and verifies the adopted digest. Unknown symbols are rejected.
SpB2PortableStatus spB2PortableRestore(const uint8_t*bytes,size_t size,const SpB2Symbol*symbols,uint32_t symbol_count,b2WorldId live_world);
// Complete-state digest with slot-derived identities and symbolic bindings.
SpB2PortableStatus spB2PortableDigest(b2WorldId world,const SpB2Symbol*symbols,uint32_t symbol_count,uint8_t digest[32]);
// Persistent identity number of the live object in a pool slot after the
// last capture, or 0. Pools: 0 body, 1 contact, 2 joint, 3 island, 4 solver
// set, 5 shape, 6 chain.
uint64_t spB2PortableIdentity(const SpB2PortableState*,uint32_t pool,uint32_t slot);
uint32_t spB2PortablePositionBits(void);
#ifdef __cplusplus
}
#endif
