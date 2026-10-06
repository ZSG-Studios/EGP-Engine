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
GDScript helper; higher-level C#/C++ facades use the shared GDScript codec and
adapters. This dependency must stay clear in SDK installation and examples.

Pass `--docs <repository-root>` to check XML method, signal and enum documentation
against the same actual editor API. The audit checks method and signal argument
names, ordering, types, enum identities, typed-array defaults, return types,
const/static/vararg/virtual/required qualifiers, and nonempty descriptions.
Property accessors may be documented through their member entries; explicitly
documented accessors still require correct signatures. Only the two named
internal physics debugger signals are excluded. Enum names, membership and
numeric values are checked too. Missing, empty, duplicate or retired callable
entries fail the audit. Receipts record XML hashes and coverage counts. Passing
these structural checks does not establish the semantic accuracy of every
description or qualification of the documented behavior.

The native `EGPNetSession` class reference lists every configuration option,
its bounds and default, command return shapes, error handling, thread ownership,
connection states and message limits. The Box2D/Box3D server references document
native joint configuration, force/torque snapshots, hit and threshold event
schemas, and explosions. Box2D shape sweeps and body metadata have backend class
references compiled into editor help and generated managed documentation.

## Runtime reload

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

Use `--assembly-recovery --unload-recovery` to exercise a corrupted project DLL
and a managed thread that prevents assembly unload. After a failed load, native
placeholders retain properties and serialized event subscriptions until a valid
assembly can be loaded. Deleting a placeholder releases its pending event state.
The fixture releases its own thread and checks recovery diagnostics in the editor
debugger panel. Application threads still require application-controlled shutdown.
Use `--disable-runtime` for the default non-collectible player and
`--feature-override` to check the editor feature override of the runtime setting.

Use `--native-recovery --native-abi-recovery` to check missing/invalid native DLLs,
changed argument counts and return types, rejected native-base changes and live
class removal. The fixture restores the original class, checks its identity,
saved properties and signal subscriptions, and verifies restart/repair diagnostics.
This qualifies dynamic method and `Callable` lookup; raw cached method bindings,
arbitrary class layouts and inheritance chains still require separate validation.


Use `--native-recovery` to exercise a missing DLL followed by an invalid DLL
before restoring the original extension. The fixture avoids extension method
calls while the library is unavailable, keeps the native parent alive, and edits
a parent property during failure. Reload retries must retain the original
extension properties while refreshing editable parent properties. Recovery must
preserve object identity, cached callables, signal connections and diagnostics.
This does not cover arbitrary native ABI or class-inheritance changes.


Use `capture_egp_api.py --include-docs` and pass the recorded compiled API to
`validate_egp_api.py --compiled-docs <capture>/compiled-docs/extension_api.json`.
The capture requires identical ABI data after removing only description fields
and checks that the editor binary did not change between commands. The audit
compares embedded method/signal descriptions with source XML, rejecting stale
help even when signatures still match. CLI documentation dumps load shipped
metadata without constructing an editor. Exposed implementation classes with
only inherited documentation retain their ABI entries with empty descriptions.

For explicit C++ networking/Box3D owner handoff, see
[the networking helper contract](../modules/egp_net/README.md#explicit-c-owner-handoff).
Use an application-controlled safe boundary before unload and stored Dictionary
properties for capsules. Restore the same bridge/session/adapter/world and
resubscribe application callbacks after compatible reload. The focused Debug
fixture uses manual polling and pauses it during unload; automatic or in-flight
owner transfer and prolonged failed-library outages remain separate acceptance
items. The ownership fixture's `--native-recovery` mode qualifies consecutive
missing/invalid DLL attempts followed by compatible repair within the fixed-clock
catch-up budget, with unavailable extension methods, retained parent edits and
exact native solver state. It does not establish automatic re-admission after a
long fault interval.
