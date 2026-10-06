# EGP API exposure contract

Public gameplay APIs must be callable through GDScript, generated C# and the
editor's matching C++ GDExtension SDK. Generated bindings must come from the actual
combined editor API, with matching pointer size and precision. API version numbers
alone do not establish compatibility. The SDK records the exact API SHA256.

`misc/scripts/validate_egp_api.py` checks physics/networking classes, native method
names, C# signals/properties, C++ methods/constants and required/removed members
against `misc/egp/api_contract.json`. It rejects missing ordinary game methods and
retired networking surfaces. This structural audit supplements runtime fixtures;
it does not verify every signature, default, return value or behavioral contract.

The extension API dump omits inspector properties. Capture those separately with
`misc/scripts/dump_egp_classdb.gd`, passing `--api=FILE --output=FILE` after the
script argument separator, and provide the resulting snapshot through the
validator's `--classdb` option. Use the same editor for both dumps. The audit
requires matching API hashes, an unchanged engine binary and coverage of each
audited class; property acceptance cannot pass without reflection data. The
manifest can require exact Variant type IDs (for example, 1 for boolean toggles).

Properties containing `/` are inspector paths. The managed binding generator
deliberately omits named C# properties for them; they remain accessible through
`GodotObject.Get/Set`. The receipt lists these paths separately with their actual
types. Their bound accessor methods still undergo normal C#/C++ exposure checks.
This classification does not establish runtime behavior; fixtures must test the
typed accessor and inspector path where behavior is required.

The servers' `_debug_changed` signals are internal editor debug-display
notifications. The managed generator intentionally excludes them; receipts name
these two signals explicitly. Ordinary public signals and the RigidBody2D/3D
`_integrate_forces` gameplay virtuals remain subject to language exposure checks.

The 16 raw-pointer virtual callbacks in `PhysicsDirectSpaceState2DExtension`,
`PhysicsDirectSpaceState3DExtension`, `PhysicsServer2DExtension` and
`PhysicsServer3DExtension` are native backend-authoring hooks retained for native
extension compatibility. They write to caller-owned native result buffers. The
managed generator omits those callbacks; they are not supported C# backend APIs.
The audit lists them explicitly and permits that scope only when the actual method
is virtual, belongs to an Extension class and has a pointer argument/result. Stale
or ordinary-method exemptions fail. C++ exposure is still checked.

Game code uses safe public queries: `PhysicsDirectSpaceState2D/3D.intersect_ray`,
`intersect_point`, `intersect_shape`, `cast_motion`, `collide_shape` and `get_rest_info`,
and the ordinary `PhysicsServer2D/3D` motion/query APIs. Those use Godot parameter
and result values and remain subject to language parity checks. A retained native
backend hook is never evidence of a missing managed gameplay replacement being
acceptable.

Three `ScriptExtension` callbacks (`_instance_create`, `_placeholder_instance_create`
and `_placeholder_erased`) likewise exchange native script-instance `void*` values.
They author native scripting backends and are explicitly native-only. Ordinary
game scripts use the language's supported script/scene instantiation APIs. The
same pointer/virtual checks apply to these declarations; public RPC getters remain
retired and must fail the audit if regenerated bindings still contain them.

Networking options, lifecycle, thread ownership, errors and limits are documented
in `modules/egp_net/README.md`. Native session wrappers can operate without the
GDScript helper; higher-level C#/C++ façades use the shared GDScript codec and
adapters. This dependency must stay clear in SDK installation and examples.

Pass `--docs <repository-root>` to check XML enum documentation against the same
actual editor API. The audit checks names, enum membership, numeric values and
nonempty descriptions, and rejects documented constants that have been retired.
It records class XML hashes and the number of checked constants. This does not
validate the semantic accuracy of every description or all method defaults.

Running-game C#/C++ reload is enabled before startup with
`debug/hot_reload/enable_runtime=true` in an editor build. The editor debugger
reloads changed native extensions at an idle boundary before script bindings;
successful C++ Debug publication notifies attached games automatically. C# Build
already sends the same debugger command. Export templates keep runtime reload
disabled. A project that opts in uses collectible managed assemblies; libraries
requiring non-collectible assemblies must retain the default setting.

`misc/scripts/validate_egp_hot_reload.py` creates a disposable project and launches
a separate game through the actual editor debugger. It checks compatible method
changes, native object identities, scalar/vector/node-reference state, cached
callables, native and managed event subscriptions, serialization callbacks and
failed-compile recovery. This scope does not guarantee recovery from arbitrary
class-layout changes, active application threads or static state.
