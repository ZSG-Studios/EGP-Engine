# EGP native networking

Yojimbo is EGP's single multiplayer transport. Pinned version 1.13.5,
commit `272153a10f32135bb44bb60e7467072baf48f762`, includes netcode,
reliable, serialize, TLSF and a minimal libsodium subset. The engine builds
these sources directly. Mono is optional and no networking assembly is required.

Build the desktop engine with `profile=misc/egp/egp_net_profile.py`.
Install the helpers into an existing game:

```sh
python misc/scripts/install_egp_net_helpers.py --project /path/to/game
```

For all three language APIs, build with `profile=misc/egp/egp_net_mono_profile.py`
and install using `--languages gdscript csharp cpp`. C# requires the matching
Mono editor, freshly generated GodotSharp assemblies and Mono export templates.
The native profile remains usable for games that use only GDScript and C++.

| Layer | GDScript | C# | C++ |
| --- | --- | --- | --- |
| Native session, tokens, raw channels, opaque replication | `EGPNetSession` | `EGP.Networking.NetSession` | `egp::networking::Session`; standalone `egp::net::Session` |
| AIO messages, state validation, entities, ownership, interest and scene factories | `EGPNet` | `EGP.Networking.NetNode` | `egp::networking::Net` |
| Prediction, correction and bounded replay | `EGPNetPrediction` | `NetPrediction` | `Prediction` |
| Explicit Box3D server clock adapter | `EGPNetBox3D` | `NetBox3D` | `Box3D` |
| Optional 2D/3D presentation interpolation | `EGPNetEntity2D/3D` | `NetEntity2D/3D` | `EntityPresentation2D/3D` |

C# sources live in `csharp/`; the C++ extension façade is the header
`cpp/egp_net.hpp`. Both high-level façades execute the shared GDScript codec,
prediction and physics adapters. All three languages therefore use the same
envelope validation and wire format. The low-level session APIs do not need
GDScript. C++ extension authors include the installed header and link the
editor's bundled godot-cpp SDK; standalone servers use `net_core.h` and its
native build instead.

```csharp
using EGP.Networking;
var net = new NetNode();
AddChild(net);
net.Configure(new NetOptions { GameProtocol = "my-game-v1" });
net.Host(10515);
long entity = net.Spawn(1, new Godot.Collections.Dictionary { ["health"] = 100 });
```

```cpp
#include "egp_net.hpp"
egp::networking::Net net(*game_node); // Keep this wrapper alive with the game.
egp::networking::Options options;
options.game_protocol = "my-game-v1";
net.configure(options);
net.host(10515);
godot::Dictionary state;
state["health"] = 100;
auto entity = net.spawn(1, state);
```

Check every returned error and admission result. C# uses ordinary typed events;
C++ connects Godot `Callable`s to the same named signals. The high-level
`Bridge`/`bridge()` and low-level `Native`/`native()` expose the underlying
Godot object for integrations. A custom C++ scene actor binds
`apply_network_state(Dictionary)` and forwards its frame delta to its
presentation helper. C# presentation actors include that callable method.
All session operations and disposal belong on the constructing Godot thread.
C++ `Net` adds a tree-owned child and defers freeing it when the wrapper dies;
C# `NetNode` closes its bridge when it exits the tree. Dispose C# low-level,
prediction and Box3D wrappers deterministically; detach physics adapters before
freeing their network node.

For C# runtime hot reload, transfer a low-level `NetSession` in the serialization
hooks. Save its capsule in an exported dictionary and subscribe your application
handlers again after restoration:

```csharp
using EGP.Networking;
using Godot;

public partial class GameSession : Node, ISerializationListener
{
    [Export] public Godot.Collections.Dictionary SessionReload { get; set; } = new();
    private NetSession? session;
    public override void _Ready()
    {
        session = new();
        session.ApplicationReceived += Receive;
        // Configure and listen/connect here; check each returned Error.
    }
    private void Receive(long peer, byte[] payload) { /* Application message. */ }
    public void OnBeforeSerialize()
    {
        if (session == null) return;
        SessionReload = session.DetachForReload();
        session = null;
    }
    public void OnAfterDeserialize()
    {
        if (SessionReload.Count == 0) return;
        session = NetSession.ResumeAfterReload(SessionReload);
        session.ApplicationReceived += Receive;
    }
    public override void _ExitTree() => session?.Dispose();
}
```

Enable runtime hot reload in the project as described in the integration checklist.
Both operations belong on the original Godot thread. Detaching disconnects managed
event bridges, makes the old wrapper unusable and keeps the native session alive;
disposing that old wrapper later does not close it. Restoration consumes the
capsule. Missing, malformed, foreign or already consumed capsules throw
`ArgumentException`; `null` throws `ArgumentNullException`. Copies cannot claim
the same session twice. A rejected capsule remains unchanged. Capsules carry local
native references and must remain in memory; they are not a disk or network format.
Save application state separately in exported properties. Event subscriptions are
explicitly rebuilt; arbitrary captured closures and high-level `Net`/physics
adapter ownership are not automatically serialized by this API.

For high-level C# `NetNode`, keep the node in the scene tree and preserve its
reference in your owner's exported `NetNode` property. Its serialization hooks
detach and restore forwarding on the same GDScript codec child and native
session. Registered message handlers should use named Godot object methods,
for example `new Callable(this, nameof(ReceiveMessage))`. Subscribe ordinary
C# application events again in their owner's `OnAfterDeserialize`; captured
closures are not a state persistence format. Overrides of `NetNode`'s
serialization hooks must call `base`. Leaving the tree closes the session and
disconnects all forwarding callbacks; re-entering reconnects forwarding on the
existing codec child. Host or join explicitly to create a new session afterward.
Configure your custom options again before that host/join operation. Close
disconnects the codec from its old native session, including retained native
references held by other code. Registered message handlers and scene factories
remain on the codec; application event handlers remain your owner's responsibility.
Native peer/entity numbers belong to their issuing session and may repeat in a
new session. Keep the session identity with stored handles; do not carry ownership
commands from a retired session into a replacement session.
Preserve application state separately. Physics adapters still require their
own explicit ownership and restoration policy.

`samples/trilingual` compiles C# sources and a C++ GDExtension, admits encrypted
C#/GDScript/C++ peers, and exercises the shared high-level codec, opaque
low-level replication, raw channels, prediction and explicit Box3D ticks.
Run `misc/scripts/validate_egp_net_languages.py` with `--engine` pointing to the
Mono editor, `--sdk` to its extracted SDK and `--packages` to its freshly built
`GodotSharp/Tools/nupkgs`. The validator uses an isolated NuGet cache to avoid
reusing older packages with the same development version. Compilation alone
does not establish runtime or packaged-game support; retain its receipt.
It also starts a C# server and separate C#, GDScript and C++ clients to validate
encrypted admission, player identity, replication and request/reply exchange.
Pass `--template /path/to/mono-template-debug.exe` to verify a relocated Windows
export with the same interop and separate-process cases.

Add `EGPNet` to your scene or autoload. Configure both endpoints with the same
game protocol, simulation fingerprint and tick rate. Configure the native
Box3D world before networking and use its `get_simulation_fingerprint()` in
the options. `simulation_tick(tick, server)` is the fixed clock; feed validated
commands to the explicit Box3D world and call `step_tick(world.get_tick()+1)`
once per tick. `EGPNetBox3D` attaches this clock, validates the world fingerprint and rate, and publishes tracked body states. Ordinary scene physics still uses SceneTree's physics clock.

Map an authoritative network entity to its stable physics body with
`EGPNetBox3D.track(entity, body_id)`, C# `NetBox3D.Track(entity, bodyId)`, or C++
`Box3D::track(entity, body_id)`. The optional body ID defaults to zero, which uses
the entity ID as before. Positive explicit IDs keep snapshot bodies independent
of connection-scoped network handles. Negative body IDs are rejected without
replacing the current mapping. Queue the body's creation before its next step;
untrack or despawn an entity when its replicated body is no longer needed.

For server checkpoint recovery, detach the adapter, restore a trusted local
`EGPBox3DWorld` snapshot, restart the listener, reattach and map new network handles
to the restored bodies. The world keeps its restored tick while the restarted
transport clock begins at zero. Each subsequent server tick advances the world
once. The network lab's `--physics --server-stall-at` mode demonstrates local
checkpoint restoration, damaged-snapshot rejection, identical local command replay
and owner-driven replicated motion. It does not supply automatic client rollback.
Do not step a world from both clocks.

```gdscript
var net = EGPNet.new()
add_child(net)
net.configure({"game_protocol": "my-game-v1"})
net.host(10515)
# After authenticating a player through your server-side account service:
var admission = net.issue_token(player_id, "203.0.113.10:10515")
# Deliver admission.token over your authenticated service to that player.
# Client: net.join_token(player_id, token)
```

Tokens are bounded, encrypted 2048-byte netcode connect tokens, valid for 30
seconds by default. The server accepts keys from its trusted configuration or
generates one locally. Addresses are IPv4/IPv6 literals; resolve service hostnames in the trusted admission backend. Account authentication, token delivery and matchmaking
belong to the game/backend. Never expose the private server key to clients.
`join(address, port)` is restricted to `127.0.0.1` or `::1`, with
`allow_insecure_loopback=true` explicitly set on both endpoints; that mode also
restricts the listener to loopback and uses a development-only key.

High-level helpers provide server-only spawn/update/despawn, monotonic entity
references, connection-generation ownership, a reconnect baseline, per-peer
interest filtering, explicitly registered message handlers, ownership-checked
input dispatch, and registered scene factories. Dictionary states are limited
to 4096 bytes and reject object deserialization. `EGPNetEntity2D/3D` provide
optional visual interpolation. Gameplay validates the values of inputs before
applying them. Named messages do not invoke arbitrary methods on scene nodes.

The low-level interface is `ClassDB.instantiate("EGPNetSession")` or
`EGPNet.send_packet`. Four user channels each offer reliable ordered delivery
(`2`, up to 4096 bytes) and unreliable unordered delivery (`4`, up to 900
bytes). Replication and named messages use separate reliable channels.
Unsupported delivery modes return an error. Check send errors: rate budgets
and bounded queues can return busy; broadcasts can partially enqueue before
returning that error. All operations run on the constructing thread.

The native replication layer sends changed entity revisions as complete bounded
states over reliable ordered delivery. Each peer keeps at most one unacknowledged
state message per entity; changes made while that message is pending are coalesced
into the latest revision after acknowledgment. Intermediate states may be skipped.
Use application messages for events that must each arrive. This prevents frequent
updates to one entity from filling the reliable queue with its obsolete revisions;
each peer resumes after its last queued entity when a queue/rate budget fills.
Its initial ordered baseline completes after every visible entity has been queued,
without requiring continuous game updates to become idle. Later revisions continue
through the same ordered channel. Delivery time still depends on entity count,
payload sizes, rate budgets, latency and loss; thousands of entities require scale
qualification. Configure endpoint rate budgets for each role; server outgoing
budgets and client incoming budgets need not be identical.
The client's `server_tick` statistic includes accepted replicated entity ticks.

`messages_per_second` and `bytes_per_second` apply per peer in separate outgoing
and incoming one-second budget windows. Outgoing admission returns busy when its
quota fills. Incoming delivery drains valid messages at the configured quota;
it does not classify arrival bursts from latency, jitter or reliable backlog as
abuse. Channels advance in rounds so application/raw traffic can progress beside
replication. Each channel retains at most one decoded envelope while waiting for
byte budget, and remaining traffic stays in Yojimbo's bounded transport queues.
Ordered channels retain their per-channel order. Unreliable delivery still permits
loss, including under transport queue pressure. Stop/disconnect clears pending
copies; connection generations never reuse them.

Byte charges estimate payload plus 64 bytes for entity state or 32 bytes for
metadata/application/raw messages. They do not measure or cap UDP headers,
handshake traffic, fragmentation or retransmissions. Receiver quotas limit game
callback/application delivery work, not all transport packet decoding CPU or wire
bandwidth. Malformed envelopes and unauthorized replication remain rejection
conditions; sustained valid excess traffic is flow controlled and can exhaust
transport queues. Games still need input validation and application-specific
abuse/disconnect policies. `received_messages`/`received_bytes` count delivered
messages/payload bytes (and malformed messages examined for rejection), excluding
valid envelopes still waiting for budget.

`EGPNetPrediction` adds bounded local input/state history, authoritative correction and deterministic replay through game-provided capture/restore/simulate callbacks. Its default caps are 128 pending ticks, 64 KiB per local snapshot and 8 MiB of history. History pressure refuses new predictions; callback/state failures require an explicit baseline reset. Server input acknowledgments and complete predicted state are game contracts. Replay callbacks must suppress duplicate presentation effects. State sent over the network still obeys the 4096-byte wire limit.

For dedicated servers, listen hosts, visible local clients, impairment and bounded
server replacement, see [the network lab guide](../../doc/egp_network_lab.md).
Restart recovery in that fixture explicitly restores an application checkpoint and
obtains fresh admission tokens. Production persistence and authentication belong to
the game/backend.

Field-delta compression, game-level prediction/input acknowledgment integration, lag-compensated hit tests,
scene-level Box2D/Box3D rollback fidelity, production authentication, bandwidth
scaling and platform qualification remain required before a full competitive
game or AAA readiness claim. Historical LiteNet qualification does not qualify
this replacement.

Validate the native suite with:

```sh
python misc/scripts/validate_egp_net.py
```

The pure GDScript fixture is `samples/gdscript/Smoke.tscn`; it checks removal of
legacy multiplayer classes, application handlers, owner-only inputs, scene
factories, entity limits, raw channels, state updates, despawns and reconnects.
The validator optionally runs it, `Token.tscn`, `Physics.tscn`, `Prediction.tscn`, `Lifecycle.tscn` and separate processes using `Processes.tscn` with `--engine /path/to/egp-editor`. The physics fixture steps the explicit Box3D world from server ticks and verifies replicated motion. The lifecycle fixture closes sessions from signal callbacks and checks that rejected endpoint calls preserve roles. Separate Godot server/client processes verify encrypted admission, account identity, authoritative replication and named request/reply messages. Their temporary local token handoff is a test harness, not an account service. For IPv6 secure tokens, pass `"::"` as the third argument of `join_token`.
Windows native Debug and Release pass 101 core checks and all five CTest entries: core networking, separate-process encrypted connections, interest-memory lifetime, and both upstream suites. The interest test performs 1024 spawn/hide/despawn cycles with no retained C++ allocations.
The current native Windows editor passes all five fixtures and the separate-process game check. The matching native debug export also passes those checks after relocation, including separate packaged server/client processes. Receipts record source, editor, template, executable and PCK hashes. The validated editor and template are preserved in `bin/egp-native-network-qualified`.

Validate a relocated Windows export with matching native binaries:

```sh
python misc/scripts/validate_egp_net_export.py --engine /path/to/egp-editor.exe --template /path/to/egp-template-debug.exe
```

The sample's `Entry.tscn` selects a fixed fixture using a user argument such as
`-- --fixture=physics`. The exported template disables command-line scene path
overrides, so use this fixture launcher when testing the packaged game.

Configuration defaults and principal limits:

| Option | Default | Contract |
| --- | --- | --- |
| `tick_rate` | 60 | 1–240; equal at both endpoints |
| `max_players` | 32 | 1–64 with this pinned Yojimbo version |
| `max_entities` | 1024 | 1–4096 live entities |
| `game_protocol` | `egp-game-v1` | Nonempty string, up to 256 UTF-8 bytes |
| `simulation_fingerprint` | `script-state-v1` | Nonempty string, up to 256 UTF-8 bytes; use the configured Box3D fingerprint for physics |
| `messages_per_second` | 1000 | At least 32, incoming/outgoing per-peer budget |
| `bytes_per_second` | 4194304 | At least 8192, incoming/outgoing per-peer budget |
| `timeout_seconds` | 5 | Connection timeout, 1–60 seconds |
| `token_lifetime_seconds` | 30 | Admission token expiry, 1–120 seconds |
| `private_key` | Generated | Optional server-only 32-byte `PackedByteArray` |
| `allow_insecure_loopback` | false | Explicit development-only direct connection |
| `simulated_loss` | 0 | Packet loss percentage, 0–100 |
| `simulated_latency_ms`, `simulated_jitter_ms` | 0 | Each 0–5000 ms, development network simulation |

Poll automatically through the helper's Node processing, or set `auto_poll=false`
and call `poll()` once from your main-thread loop. Connect `diagnostic` to your
game's logging and check each returned `Error`. At most eight fixed ticks run per
poll. A client's simulation clock starts when its authoritative baseline completes
and the session becomes `Connected`; transport polling during `Connecting` and
`Synchronizing` does not advance simulation ticks. A server's clock starts after
listening. If unprocessed simulation time exceeds 0.5 seconds, poll emits the catch-up
diagnostic, returns `FAILED`, stops the endpoint, clears entities and resets ticks.
Recovery is explicit: after poll returns, close/reconfigure the language facade,
obtain a fresh admission token and reconnect for a new authoritative baseline.
Do not reconfigure from synchronous poll callbacks. The server must revoke old
connection ownership and grant a new entity/authority to the new peer; account
identity alone does not restore ownership. A server that stops must restore its
game state explicitly. The [network lab guide](../../doc/egp_network_lab.md)
provides `--client-stall-at`, `--client-stall-ms`, `--client-stall-index`,
`--client-stall-count` and `--client-stall-interval` to exercise single or repeated
recovery without increasing the engine budget. Each recovery uses a fresh token,
peer and owned entity; the next gap waits for verified authoritative progress.
The lab's `--server-stall-at` and `--server-stall-ms` restore a stopped server in
the same process. After poll returns, a stopped configured server can call
`host()` again, restore application state and issue fresh tokens. A secure listener
without an explicit `private_key` generates a new key each time it starts, so old
tokens no longer admit clients. Keeping the Session preserves its retired handle
generations. Handles are scoped to their Session; replacing the Session requires
the application to discard old handles and track its new authority generation.
An explicitly configured `private_key` is retained across listener restarts.
Do not use a stopped/rebound listener as a general token-revocation mechanism:
the transport rejects tokens whose expiry precedes the new listener's UTC start
second plus its configured maximum lifetime, but an unused token issued in that
same second can still admit when the key is retained. Use fresh admission and
the backend's revocation policy. `misc/scripts/validate_egp_net_admission.py`
checks this boundary with zero/nonzero test keys and generated keys through
native and GDScript APIs, including an optional packaged Windows template.
`server_tick` reports the latest
replicated server tick, not a continuously synchronized idle clock.

For named messages, register the same message contract at each receiving
endpoint using `register_message(name, handler, Sender.SERVER/CLIENT/BOTH)`.
The handler receives `(peer_id, arguments)`. A client sends to peer `0`;
the server uses a `peer_id` from `get_peers()` or a connection signal. The
authenticated account `client_id`, connection `peer_id`, and replicated
`entity` handle are separate identities. Ownership uses `peer_id`, so a new
connection never inherits the previous connection's authority.

For custom codecs, instantiate `EGPNetSession` and call `command()` directly:

| Command | Arguments | Result |
| --- | --- | --- |
| `peers` | None | Array of `peer_id`, `client_id`, `ping_ms` records |
| `entities` | None | Array of `entity`, `kind`, `authority_peer`, `revision`, `tick`, raw `state` records |
| `spawn` | `kind`, raw `state`, `authority_peer` (default -1) | Dictionary containing `error`, `entity` |
| `update_entity` | `entity`, raw `state` | Error |
| `despawn` | `entity` | Error |
| `set_visible` | `entity`, `peer`, `visible` | Error |
| `disconnect` | `peer` | Error |
| `send_packet` | `peer`, raw `payload`, `channel` (0–3), `delivery` (2 or 4) | Error |

Native `send_application()` is the helper's named-message lane. Use the same
envelope codec if mixing it with `EGPNet`, or use raw packet channels for your
own format. Native replication accepts opaque bytes; the Dictionary codec and
scene factories belong to the GDScript helper. Native C++ integrations use
`egp::net::Session` from `net_core.h` and must keep the Session alive throughout
a call and its callbacks.


C# `NetBox3D` can transfer ownership across a managed assembly reload using
`DetachForReload()` and `ResumeAfterReload()`, like `NetSession`. Keep the capsule
in an exported Dictionary on your `ISerializationListener` owner:

```csharp
[Export] public Godot.Collections.Dictionary PhysicsReload { get; set; } = new();
private NetBox3D? physics;

public void OnBeforeSerialize()
{
    if (physics == null) return;
    PhysicsReload = physics.DetachForReload();
    physics.Dispose(); // The transferred adapter stays attached.
    physics = null;
}

public void OnAfterDeserialize()
{
    if (PhysicsReload.Count == 0) return;
    physics = NetBox3D.ResumeAfterReload(PhysicsReload);
    physics.Failed += ReportPhysicsFailure; // Resubscribe your application events.
}
```

The capsule retains the same GDScript adapter, its attached codec/world and its
entity-to-body mappings. Preserve the `NetNode` owner through its own serialization
hooks too. A capsule is single-use, contains local object references and does not
serialize to disk or over the network. Invalid or consumed capsules are rejected
without mutation. A detached wrapper's methods throw `ObjectDisposedException`;
its later disposal does not detach the resumed owner. Disposing the resumed owner
normally detaches the adapter; the caller still owns the world and network node.
After closing a native session, explicitly reconfigure/admit and map each new
entity to its stable body ID. Numeric entities belong to their issuing session.
This ownership transfer does not implement automatic client physics rollback or
restore arbitrary application event closures.

### Explicit C++ owner handoff

C++ `egp::networking::Net` and `Box3D` support `detach_for_reload()` and static
`resume_after_reload(Dictionary &, Error * = nullptr)`. Before a compatible DLL
reload, stop calling application code at a safe boundary on its Godot thread,
save the returned capsules in stored Dictionary properties on the extension node,
and destroy the detached wrappers. After the engine restores those properties,
consume them and register application callbacks again:

```cpp
// Before unload, at an application-controlled safe boundary:
godot::Dictionary physics_state = physics->detach_for_reload();
if (physics_state.is_empty()) return; // Ownership was not transferred.
physics.reset(); // Does not detach the transferred adapter.

// After compatible reload, using the restored Dictionary property:
godot::Error error;
physics = egp::networking::Box3D::resume_after_reload(physics_state, &error);
if (error != godot::OK) return; // Invalid capsules remain unchanged.
physics->connect("failed", failure_callback); // Register the new DLL's callback.
```

The `Net` capsule retains the tree-owned bridge and native session. Its original
parent must stay alive and must not be queued for deletion. The `Box3D` capsule
retains the adapter, its attached world, stable body mapping and GDScript clock
connection. Transfer the network wrapper too when that wrapper belongs to the
unloading DLL. A detached wrapper becomes unavailable; error-returning operations
return `ERR_UNCONFIGURED`, repeated transfer returns an empty Dictionary, and
later destruction cannot close or detach the resumed owner.

Wrapper-owned signal callbacks and registered message handlers are removed at
handoff. `Net` and `Box3D` both expose `connect`/`disconnect` for these tracked
callbacks. Callbacks installed directly on `bridge()`/`native()`, scene factories,
application threads and unrelated closures require application-controlled cleanup.
Resubscribe handlers from the new DLL before resuming application traffic.

Capsules use a version, exact helper-script identity and a random single-use
ownership token. Malformed, foreign, forged or already-consumed capsules return
null with `ERR_INVALID_PARAMETER` and remain unchanged. The successful consume
clears the Dictionary and invalidates copied capsules. These are local object
references, not a disk/network format, and C++ capsules cannot be consumed by the
C# helpers. Consume a pending capsule or explicitly close/detach its retained
object before discarding it.

`misc/scripts/validate_egp_cpp_ownership.py` qualifies two real compatible Debug
DLL reloads in one local Windows editor-build process with manually polled,
authenticated server/client sessions. It verifies exact solver state at the
handoff, stable identities/body mapping, advancing replicated physics, one
callback per tick and message-handler resubscription. Polling pauses during the
explicit unload. Add `--native-recovery` to inject a missing DLL and an invalid
DLL before each repair. The fixture verifies that extension methods stay
unavailable during failure, parent-property edits survive, and the same stored
capsules restore the owners and exact solver state after both failed attempts.
The loader diagnostics are expected and counted; unrelated errors still fail
qualification. These are short controlled failures within the network clock's
catch-up budget. This does not qualify automatic transfer, mutation inside active
callbacks, prolonged outages/re-admission, independent-process reload,
Release-library recreation or arbitrary native layouts.
