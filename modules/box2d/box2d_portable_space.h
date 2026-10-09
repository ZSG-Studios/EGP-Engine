// SPDX-License-Identifier: MIT
#pragma once

#include "core/templates/rid.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class Box2DPhysicsServer2D;
class Box2DSpace2D;

// Portable Box2D space checkpoints through the physics server.
//
// Capture runs at the physics phase boundary: after a space's step effects
// have been consumed by sync_state and before scripts can mutate it for the
// next step. A requested capture executes exactly there; an immediate capture
// is refused unless the space is at that boundary. Lifetime registries persist
// per space across captures, so surviving objects keep their canonical
// identities.
//
// The checkpoint (EGPB2SP1) carries the wrapper state the server owns
// (bodies, the default area, shape instances, space parameters, the
// constant-force order) and the complete native world (SPB2WCK1). Every
// pointer is a symbol: native callbacks, the pre-solve context, each wrapper
// and shape instance. Restore creates a fresh space and wrappers with new
// RIDs, resolves symbols to them, adopts the native world and binds each
// wrapper to its native body and shapes. Node instance ids, canvas instance
// ids and object user data are mapped through a caller-supplied table;
// unknown ids are rejected.
//
// Covered: rigid, kinematic and static bodies with any shapes; the default
// area and extra areas with their overrides, step order, monitor callbacks
// and overlap state; every joint type through the server (pin, groove, damped
// spring and the configured distance..wheel joints) with their creation
// definitions; collision exceptions and their filter joints; reported
// contacts; force-integration callbacks with their user data and the
// state-sync callback. Callbacks are rebound through `callable_map`
// ("<role>:<source id>" -> Callable) or, for method callables, through
// `object_map`; an unresolvable force-integration or monitor callback is
// rejected, an unresolvable state-sync callback is dropped. Cached hit and
// joint events and named bodies are refused at capture.
class Box2DPortableSpace {
public:
	static Dictionary capture(Box2DPhysicsServer2D *p_server, RID p_space);
	static Error request_capture(Box2DPhysicsServer2D *p_server, RID p_space);
	static Dictionary take_capture(Box2DPhysicsServer2D *p_server, RID p_space);
	static Dictionary restore(Box2DPhysicsServer2D *p_server, const PackedByteArray &p_bytes, const Dictionary &p_object_map, const Dictionary &p_shape_map, const Dictionary &p_callable_map);
	static PackedByteArray digest(Box2DPhysicsServer2D *p_server, RID p_space);
	static Dictionary capture_info(Box2DPhysicsServer2D *p_server, RID p_space);
	static int64_t identity(Box2DPhysicsServer2D *p_server, RID p_body);

	static void after_flush(Box2DSpace2D *p_space);
	static void space_destroyed(Box2DSpace2D *p_space);
};
