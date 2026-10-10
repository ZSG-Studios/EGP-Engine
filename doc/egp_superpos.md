# Superpos networking

Superpos is EGP's native networking integration. The engine-independent C++23
core is embedded in `modules/superpos`; the native adapter exposes the same
ClassDB implementation to GDScript, generated C# and generated godot-cpp.
Public engine and extension headers remain C++17-compatible.

Superpos, with its authenticated UDP/DTLS foundation, is EGP's only networking
stack. The former Yojimbo `egp_net` module was removed (see the
[migration guide](egp_superpos_migration.md)).
Superpos is a different implementation and API from the retired **Superposition** nodes.
Changing names in an existing project does not migrate its protocol or scenes.

## Native API

| Class | Purpose |
| --- | --- |
| `SuperposUInt64` | Exact unsigned ordering, decimal conversion, checked arithmetic and little-endian bytes. |
| `SuperposField` | Explicit field ID, registered codec, byte bound and audience declaration. |
| `SuperposSchema` | Explicit schema ID, revision and fields; native baking produces a bounded author manifest and fingerprint. |
| `SuperposSession` | Owner-thread canonical world, object ownership, packed publication, checked counters, packet delivery and native prediction entry points. |
| `SuperposWorld` | Scene owner of a session, authored schemas and optional physics-phase tick ownership. |
| `SuperposReplicator` | Explicit dirty capture and sequential property projection with owner lifetime and generation checks. |
| `SuperposSimulationProvider` | Abstract native simulation interface; registration requires a compiled provider, not gameplay script callbacks. |

Author resources are declarations. Baking does not instantiate gameplay or run
script getters. Configured schemas are frozen; changing a Resource afterwards
does not change an existing session's registry. Concrete codecs must be
registered before use. A schema manifest is not an admission credential or an
executable scene registry.

## Local canonical state

This example creates one local canonical object. It does not connect a peer or
automatically replicate a scene property:

```gdscript
var field := SuperposField.new()
field.field_id = 1
var schema := SuperposSchema.new()
schema.schema_id = 73
schema.fields = [field]
var session := SuperposSession.new()
var error := session.configure([schema], 4, 1, 0, 4096)
if error == OK:
    var handle := session.spawn_object(73, 0, SuperposUInt64.to_bytes(1))
    var observed := session.read_object(handle)
    if observed.error == OK:
        print(SuperposUInt64.from_bytes(observed.canonical).value)
session.close()
```

C# uses the generated `Godot.SuperposSession`, `Godot.SuperposWorld` and related
classes. C++ extensions use generated `godot::SuperposSession` bindings from the
SDK made by the same editor. The old `EGP.Networking` helpers and
`egp::networking` wrappers are not Superpos APIs. Never expose the core's C++23
types through an extension's public C++17 interface.

## Identifiers, ownership and publication

Native identifiers and absolute ticks are `uint64_t`; generated C# preserves
`ulong`. GDScript stores their exact bits in a signed `int`, so negative handles
are valid. Use `SuperposUInt64` for ordering and decimal/byte conversion.
Never transport identifiers through floating point or numeric JSON.

Use `read_tick()`, `read_binding_generation()` and `read_authority_epoch()`.
Their Dictionary contains `value` only on successful availability and owner
validation. Check `error` before reading a value; zero is not a failure sentinel.
Calls and destruction obey the owning engine thread. A `SuperposWorld` retires
its owned session when destroyed; retained wrappers cannot revive that owner.

`publish_packed()` applies bounded canonical operations atomically. Canonical
publication does not make arbitrary Godot property setters transactional.
`SuperposReplicator` supports explicit dirty capture and sequential projection;
generation and lifetime checks apply after callbacks. Automatic allowlisted
scene spawning from the old stack is not a supported equivalent.

## Transport

`configure_udp()` establishes one pre-provisioned native association with a
server/client role, local and remote endpoints, session ID, peer identity and
admission key. Use `get_admission_state()` to check actual readiness before
sending. A configured local world alone is not authenticated network admission.

Use `enqueue_packet()`, `read_packet()`, `acknowledge_packet()`,
`get_packet_outcome()` and `retire_packet()` with their checked message and
binding-generation values. Provision credentials through the application's
trusted backend. Never log a key or admission credential, serialize it into a
scene Resource, or reuse the former transport's token format.

The native adapter has bounded loopback authentication and packet fixtures.
The [network lab guide](egp_network_lab.md) separately describes the physics
showcase and a recorded 101-stream remote courier workload. These checks do
not establish production account services, general capacity or platform parity. Optional RTC
and service implementations in the separate core require their own selected
build feature, deployment and engine qualification.

## Prediction and physics

Native prediction requires a registered compiled simulation provider and
explicit history/byte bounds. Unregistered sessions report unavailable
simulation capabilities. A deterministic replay fixture is not a Box2D/Box3D
solver adapter and does not establish recovery or cross-platform determinism.
The retired `EGPNetBox3D` and `SuperpositionPrediction` integrations cannot be
used with Superpos. Box2D/Box3D local snapshots remain independent physics APIs.

## Build and qualification

Desktop native xmake builds select `module_superpos_enabled=yes`. The old
network module, its option and archive target, Yojimbo and their validators were
removed. The module's `superpos_dtls` option defaults to true and borrows EGP's
existing MbedTLS/PSA runtime. Builds and tests run on the local and remote build
PCs and the EGP self-hosted CI runners. Mobile/web adapter capability and full
platform qualification remain pending.

The separate Superpos-EGP `docs/consumer-migration.json` and `compatibility.json`
record isolated API, unsigned metadata, lifecycle, UDP and C++17/managed
evidence. Those historical editor receipts do not qualify the current full
engine, export templates, physics or rendered Inspector. The engine's fixed
`.build/diagnostics/superpos-cutover.json` records current cutover verification.
Historical receipts must retain their original scope.

See [migration](egp_superpos_migration.md) before porting an existing game.
