# EGP networking

EGP uses pinned LiteNetLib 2.1.4 and LiteEntitySystem 1.2.2 sources for its desktop
.NET networking runtime. Both namespaces live in `EGP.Networking.dll`; do not add
separate LiteNetLib or LiteEntitySystem packages to a game. The package includes
RefMagic, the upstream SyncVar analyzer, and a locked LZ4 dependency.

The runtime provides authoritative servers, per-connection client managers,
baseline/delta replication, owned prediction/reconciliation, lag compensation,
typed RPCs, player/controller/pawn lifetime, interpolation and explicit sync-group
interest controls through the upstream entity APIs. EGP adds a compatibility
handshake, bounded packet admission, owner-thread checks, reliable application
channel, reconnect/reset handling, optional AES-256-GCM match-key protection,
native/GDScript facade and fixed-tick Box3D integration hooks.

## Build and package

Use the same profile for editor and exported runtime. The qualified Windows
developer build uses the repository's SCons environment:

```powershell
$env:NUGET_PACKAGES = "$PWD/.build/litenet-game-nuget"
.build/venv/Scripts/python.exe -m SCons platform=windows target=editor profile=misc/egp/litenet_profile.py dev_build=yes debug_symbols=no accesskit=no d3d12=no -j4
bin/godot.windows.editor.dev.x86_64.mono.exe --headless --generate-mono-glue modules/mono/glue
.build/venv/Scripts/python.exe modules/mono/build_scripts/build_assemblies.py --godot-output-dir bin --godot-platform windows --push-nupkgs-local bin/GodotSharp/Tools/nupkgs
.build/venv/Scripts/python.exe -m SCons platform=windows target=template_debug profile=misc/egp/litenet_profile.py dev_build=yes debug_symbols=no accesskit=no d3d12=no -j4
```

Developer build filenames include `.dev`. Package generation places
EGP.Networking and the matching Godot packages in `bin/GodotSharp/Tools/nupkgs`.
The EGP Godot.NET.Sdk adds EGP.Networking automatically; a standalone server can
reference `modules/litenet/managed/EGP.Networking.csproj`, or the generated package.
Do not mix the SDK/API assemblies with a different engine build.
Development packages reuse versions; choose a fresh task NuGet cache after rebuilding
them. The sample's NuGet.Config points to this repository's matching package feed.
For release templates, build `target=template_release` with the same profile and
qualify that configuration separately.

Build/run/export the included sample after building the matching SDK and template:

```powershell
dotnet build modules/litenet/samples/godot/EGP.NetworkSmoke.sln -c Debug
bin/godot.windows.editor.dev.x86_64.mono.exe --headless --max-fps 30 --path modules/litenet/samples/godot
New-Item -ItemType Directory -Force .build/litenet-export | Out-Null
bin/godot.windows.editor.dev.x86_64.mono.exe --headless --path modules/litenet/samples/godot --export-debug "Windows Network Smoke" .build/litenet-export/EGP.NetworkSmoke.exe
.build/litenet-export/EGP.NetworkSmoke.exe --headless --max-fps 30
```

The sample exits with a JSON success/failure marker. Keep the solution file: Godot's
C# exporter requires it even though dotnet can compile a project alone.

With LiteNet enabled, ENet, Godot scene replication and WebRTC are excluded even
if their legacy module flags were requested. WebSocketPeer and the debugger remain;
WebSocketMultiplayerPeer is not built or registered. Godot's generic
MultiplayerAPIExtension remains as a core compatibility shell. Node RPC,
MultiplayerSynchronizer and MultiplayerSpawner do not run the LiteNet protocol:
migrate gameplay to registered LES entities and typed RPCs. Ordinary TCP/UDP/HTTP,
TLS, editor/debugging APIs are separate services and remain available.

## Game setup

Register every game entity with stable enum IDs and the same constructors on both
peers. Make a fresh map for each manager. Specify an explicit `GameProtocol` string
and increment it whenever gameplay, RPC meaning, inputs or field semantics change.
The handshake combines that version, type identities, LES field hash, library
revision, tick/send rate and physics fingerprint. An incompatible client is rejected
before adding a replication player.

```csharp
var options = new SessionOptions {
    TickRate = 60, MaxPlayers = 32, GameProtocol = "my-game-v1",
    SimulationFingerprint = physicsWorld.SimulationFingerprint
};
var server = new LiteSession(BuildTypes(), options);
server.PlayerJoined += (world, player) => {
    var pawn = world.AddEntity<MyPawn>();
    world.AddController<MyController>(player, pawn);
};
server.Listen(10515);
// Every game-loop iteration, on the constructing thread:
server.Pump();
// On shutdown:
server.Dispose();
```

For an engine game, retain `Godot.EGPLiteRuntime` on a C# autoload or game node.
Its `Session` exposes the same typed API and its `Bridge` is an EGPLiteSession
RefCounted usable by native code/GDScript. Poll either interface once per frame.
Dispose the runtime on scene exit. The included Godot qualification project shows
the native bridge, Box3D world and actual state replication together.
`Listen` takes an IPv4 bind address. Wildcard servers also bind IPv6; loopback
servers bind IPv6 loopback, and a specific IPv4 interface disables the IPv6 socket.
The engine and game share one EGP.Networking assembly across managed load contexts.
Stop sessions before script reload. Upstream custom field-type processors are
process-wide; changing custom schemas currently requires an editor restart.

The native bridge requires the .NET runtime to be initialized and a registered C#
entity schema. A GDScript-only project without that bootstrap has no managed
networking session. Transport peer IDs and LES byte player IDs are separate spaces;
the application channel uses transport IDs. Entity references include version as
well as ID, so do not retain a recycled ID alone.

## Box3D and replay

Configure Box3D before opening a session and use its exact fingerprint in options.
`PhysicsTimeline` rejects a solver/LES tick-rate mismatch and runs one solver step
after all entity commands, before server state serialization. Rendering reads
interpolated SyncVars. Box3D's `get_tick` is the last completed tick; the managed
adapter requests `get_tick + 1` for the next step.

Client physics history contains at most 128 full solver snapshots and has an
explicit byte budget. Baseline/correction callbacks must create/destroy bodies,
apply authoritative pose and velocities, and flush `apply_queued_commands` before
capturing a snapshot. Tick comparisons use wrapping ushort sequences; client
input ticks have an independent origin from interpolated server ticks. Never
index client prediction history by server tick directly.

Solver snapshots are trusted local data and are never accepted from network peers.
Full local restore/replay is verified. Authoritative poses/velocities do not
reconstruct all hidden remote contact caches. Exact physics convergence across
collisions needs the game's correction/resync contract and further qualification.
An exhausted physics history fails closed and requires reconnect/resynchronization.
The profile enables the standalone Box3D world; it does not replace PhysicsServer3D
or make ordinary RigidBody3D nodes use Box3D.

## Admission and protection

Packet size/rate, fragment count, pending diff count, baseline decompression size,
RPC framing, entity field sizes and input interpolation values are bounded or
validated. Malformed messages disconnect the peer. Custom game input/RPC payloads
still need game-specific validation before applying authority-changing actions.

For protected matches, provide a 32-byte `TransportKey` out of band to server and
clients, plus `AccessToken` when a match join credential is needed. AES-GCM protects
the complete LiteNetLib datagram, including its handshake. The key is shared by the
match; it does not establish individual account identity or protect against a player
who already knows that key. Rotate keys per match, restrict distribution and validate
account/session claims through the game's authenticated admission service. Do not
log keys/tokens. Without TransportKey the transport and AccessToken are plaintext.
No matchmaker, account service, relay or public DDoS defence is configured by this
repository. Packet protection allocates temporary buffers and requires profiling
before throughput/GC claims.

## Qualification

```powershell
python misc/scripts/validate_litenet.py --native-physics --package-checks
```

Receipts are under `.build/litenet-validation`. Checks use real UDP loopback,
multiple clients, baseline/deltas/despawn, typed RPCs, per-client sync-group filtering,
interest reentry, lag-history rewind/restore, input prediction/reconciliation, reconnect,
packet loss/latency simulation, wrong-thread rejection, malformed packets, encrypted
replication and native Box3D stepping/snapshot continuation. A clean package
consumer and a deliberately invalid SyncVar assignment verify dependency/analyzer
packaging. `--engine` runs the already-built Godot sample against a matching C#
editor/export binary. Keep actual engine, exported-game, long-session/load/security,
and cross-platform qualification separate. These checks do not establish AAA
production readiness or feature parity with every removed Godot networking API.
Vendor manifests normalize C#/IL CRLF to LF for hashes, matching Git's text
normalization across Windows/Linux/macOS. DLL hashes use the exact binary bytes.

Current Windows evidence: 40 managed/native UDP checks and the clean package/analyzer
checks pass. The actual native editor game mode, separate Box3D replay fixture,
and Windows exported debug executable pass, including running the copied export
from `bin/egp-network-qualified/NetworkSmoke`. The physics replication assertion
compares bit-exact height with the recorded server state at the client's replicated
state tick; it does not compare poses from different simulation times.
Engine/export receipts and preserved
first-failure logs are in `.build/litenet-engine-validation`; the exact qualified
source snapshot and executable hashes are in its `receipt.json`. Binaries are
preserved under `bin/egp-network-qualified`. This is the standalone Box3D world
configuration; the evolving scene PhysicsServer3D backend is separate qualification.
The opt-in scene editor separately passes 24 Box3D cases and the networking smoke
test. Its binary and GodotSharp assemblies are under `bin/egp-box3d-scene-qualified`;
receipts are in `.build/box3d-scene-current-validation`. This establishes coexistence,
not complete scene-physics network rollback or PhysicsServer3D parity.
Settled headless import and export pass. Immediate headless editor shutdown can
hit an upstream singleton/thread cleanup race; the failure logs remain preserved.

Primary sources: [LiteNetLib](https://github.com/RevenantX/LiteNetLib),
[LiteEntitySystem setup](https://revenantx.github.io/LiteEntitySystem/articles/getting-started/installation.html),
[prediction](https://revenantx.github.io/LiteEntitySystem/articles/netcode/prediction-and-rollback.html),
[.NET AES-GCM](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.aesgcm.encrypt?view=net-10.0).
