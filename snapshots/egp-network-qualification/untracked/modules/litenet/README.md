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

Use the same profile for editor and exported runtime:

```powershell
python -m SCons platform=windows target=editor profile=misc/egp/litenet_profile.py -j4
bin/godot.windows.editor.x86_64.mono.console.exe --headless --generate-mono-glue modules/mono/glue
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir bin --godot-platform windows
python -m SCons platform=windows target=template_debug profile=misc/egp/litenet_profile.py -j4
python -m SCons platform=windows target=template_release profile=misc/egp/litenet_profile.py -j4
```

Developer build filenames include `.dev`. Package generation places
EGP.Networking and the matching Godot packages in `bin/GodotSharp/Tools/nupkgs`.
The EGP Godot.NET.Sdk adds EGP.Networking automatically; a standalone server can
reference `modules/litenet/managed/EGP.Networking.csproj`, or the generated package.
Do not mix the SDK/API assemblies with a different engine build.

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

Primary sources: [LiteNetLib](https://github.com/RevenantX/LiteNetLib),
[LiteEntitySystem setup](https://revenantx.github.io/LiteEntitySystem/articles/getting-started/installation.html),
[prediction](https://revenantx.github.io/LiteEntitySystem/articles/netcode/prediction-and-rollback.html),
[.NET AES-GCM](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.aesgcm.encrypt?view=net-10.0).
