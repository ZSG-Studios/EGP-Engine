> **Legacy migration source:** this guide and its fixtures use the retired networking module. They are disabled in the default Superpos build and do not qualify Superpos. See ../../doc/egp_superpos.md for the replacement API.

# Superposition

Superposition adds an Inspector workflow for native scene spawning, selected gameplay properties and permitted remote calls. It uses EGP's existing Yojimbo transport and authenticated peer identities. The same native nodes and resources are available in GDScript, C# and the editor's matching C++ SDK.

## Set up a networked scene

1. Add **SuperpositionWorld** as the network owner. Choose **Role**, **Port**, the shared game protocol and simulation fingerprint. Enable **Auto Poll** for ordinary scene processing. **Auto Start** is optional; authenticated clients normally call `join_token()` after receiving an admission token.
2. Add **SuperpositionSpawner** below the World. Set **Spawn Path** to the gameplay container. Add **SuperpositionScene** resources to **Scenes**, assign each a unique **Prefab ID**, and choose its **PackedScene**. Ship the same allowlist on server and clients.
3. In each gameplay scene, add **Superposition** below the node whose properties should replicate. Expand **Replicate** and check the stored properties to transmit. Save the generated **Config** as a shared resource.
4. Add **SuperpositionRPC** beside the Spawner. Choose **Spawner Path** and add **SuperpositionRPCMethod** resources. Give each a stable **Method ID**, the root actor's method name, caller permission, exact argument types and call-rate limit.
5. Spawn from the listening server with `spawner.spawn(prefab_id, owner_peer, data)`. The component creates the matching local scene on each compatible client, including late joiners. A root actor can read `superposition_entity_id`, `superposition_owner_client_id` and `superposition_spawn_data` metadata in `_ready()`.

World descendants discover their nearest ancestor's `get_session()` provider. The Spawner refreshes that binding when the World stops or replaces its session. An explicit **Session Path** can point to another provider; `set_session()` supports code-owned sessions. A manually assigned session takes precedence over automatic discovery.

For preplaced objects, add Superposition directly to matching server/client scenes and use matching scene paths or an explicit unique **Replication Key**. For spawned scenes, the Spawner assigns the session and keys based on the native spawn entity plus each component's relative scene path before tree entry. Multiple nested components therefore have distinct, corresponding identities on every peer.

## Authentication and admission

The game/backend authenticates accounts and delivers short-lived admission tokens through its authenticated service. Start the server, issue a token for that account's stable numeric identity and public endpoint, and pass the returned token to the client's World:

```gdscript
# Server, after authenticating the player's account:
var admission: Dictionary = world.get_session().issue_token(player_id, "203.0.113.10:10515")
if admission.error == OK:
    # Deliver admission.token to that player through the game's account service.
    pass

# Client, with matching game protocol, simulation fingerprint and tick rate:
var error: Error = world.join_token(player_id, admission_token)
```

Check every returned `Error`. Configure token lifetime and any server private key before starting. **Allow Insecure Loopback** is an explicit local-development option. Token delivery, account policy, persistence and production recovery remain game/backend responsibilities.

## Scene spawning and ownership

The allowlist accepts 1–64 scenes with unique IDs from 1–65535. Wire data contains IDs and bounded scalar metadata, never a resource path chosen by a remote peer. Compatibility handshakes compare the allowlist's stable IDs, saved scene structure, nested replication schemas, resource identity and sorted declared script method/property/RPC interfaces. Unknown or mismatched peers remain hidden and cannot dispatch RPC. Script implementation bytes are not hashed, so source and compiled-script deployments can match. Matching asset builds and game-protocol/build-version gating are still required; the handshake is not a complete package-content hash.

Only the listening server can spawn, despawn or change scene visibility. `spawn()` returns the native entity ID, or zero with a `spawn_error` diagnostic. `despawn()` retires that entity and removes its local instances. `get_spawned_node()` returns the current root actor. The default instance cap is 256 and can be configured up to 4096.

An owner is identified by the authenticated account/client ID captured from the native peer table. A disconnect's reusable peer slot does not transfer ownership. Reconnecting with a fresh token for the same identity can recover owner permission for a retained scene. Numeric entities belong to their issuing session; a replacement session can reuse numbers and requires new scene bindings.

`set_visible(entity, peer, visible)` controls whole-scene interest. Hidden clients remove the local instance; re-entry recreates it from the current allowlisted scene and baseline. Superposition property components additionally provide distance relevance through **Interest Radius**, **Interest Hysteresis** and `set_observer_position(peer, position)`. Unspecified property observers remain visible. Relevance is an optimization; keep secrets outside replicated gameplay state.

Spawn data accepts at most 16 string keys and scalar values: null, bool, int, finite float, finite Vector3 and bounded string. Keys are at most 64 UTF-8 bytes; strings are at most 256 bytes. The complete manifest is capped at 4096 bytes. Object deserialization is disabled.

## Select and schedule properties

Supported property types are bool, int, finite float, string, Vector2, Vector3 and Color. Schemas contain 1–32 rules; strings are capped at 256 UTF-8 bytes and complete snapshots at 4096 bytes. Clients validate version, identity, ordered schema, exact types, quantization and values before changing the target. A listening server rejects received property state.

The default capture rate is 10 Hz. Float and vector quantization reduces insignificant changes. An unchanged quantized snapshot produces no update. Optional property smoothing eases presentation values; it is separate from physical prediction and fixed-tick simulation.

**Capture Mode / Pushed** skips getter calls and serialization until gameplay calls `mark_dirty()`. Notifications coalesce at the configured update rate. A new entity, schema change or restarted session captures a complete baseline automatically. **Automatic** retains the checkbox-only polling workflow.

**Priority** selects a weight from 1–16. The per-peer scheduler retains a pending flow's position until admission so continually changing high-priority objects cannot indefinitely move lower-priority objects behind them. Weights are preferences, not a promised delivery frequency; reliable in-flight state coalescing, loss and latency still affect observed rates.

Set an authenticated peer's gameplay update budget with:

```gdscript
session.command("set_peer_replication_budget", {
    "peer": peer_id,
    "bytes_per_second": 16384,
})
```

Zero disables this additional budget. It accounts for admitted gameplay update envelopes, not all UDP headers or retransmissions. The token bucket permits a burst of `max(rate, 4160)` bytes. A selected large update reserves the next opportunity to avoid starvation. Membership baselines and teardown bypass this update budget while retaining the session's transport quotas. Reconfigure the budget for each fresh peer generation. `replication_peer_statistics` reports admitted bytes, updates, available tokens and deferrals.

### Acknowledged delta updates

Enable **Config / Delta Replication** to send sparse byte-run patches against each peer's acknowledged full snapshot. Quantization and validation still operate on the complete gameplay frame; the receiver reconstructs a full native entity state before applying properties. This is byte-patch compression, rather than semantic property encoding.

Joining, re-entry, reconnect, changed sizes or authority/kind, insufficient savings and baseline-capacity pressure send full frames. Acknowledged and pending baseline retention is capped at 1 MiB per peer. Evicting a baseline makes the next update full. Reliable ordering supplies patch prerequisites; malformed patches or a wrong baseline reject the connection. Both endpoints must use the matching `native-wire-3` fingerprint. Delta compression does not change the application's complete-state contract.

## Allowlisted remote calls

Each method resource defines who may invoke it:

| Permission | Allowed sender |
| --- | --- |
| **Authority** | The authenticated server |
| **Owner** | The authenticated client identity that owns the target scene |
| **Any Peer** | Any currently authenticated, schema-compatible peer, including the server |

Calls target a Spawner-owned root entity and a configured numeric method ID. A remote packet cannot choose an arbitrary node path or method name. The Inspector's **Argument Types** entries select bool, int, float, string or Vector3. At most eight arguments are permitted, with exact type matching, finite numeric values and strings up to 256 UTF-8 bytes. Containers and objects are rejected.

```gdscript
# Owner/Any Peer calls travel from a client to its authenticated server.
var error: Error = rpc.send_rpc(entity_id, 1, [requested_action])

# Authority/Any Peer calls travel from the server to compatible clients.
var error: Error = rpc.send_rpc(entity_id, 2, [announcement], target_peer)
```

The default peer argument broadcasts on the server and selects peer zero on clients. The server cannot invoke an owner-only method. The server checks ownership again against its live native peer table; game handlers must also validate action values, gameplay state and permissions specific to the game.

Each resource permits 1–128 calls per second, default 16. Aggregate outgoing and incoming limits are 128 calls per second per endpoint/sender, with a maximum 256-call dispatch queue and a 4096-byte wire frame. Dispatch runs after transport receipt and revalidates the session generation, scene/root instance, method schema, sender identity and ownership. Disconnect or rebind invalidates queued work. Root metadata `superposition_rpc_sender_peer` and `superposition_rpc_sender_client_id` exposes the authenticated sender to the called game method.

## Motion and complete-world prediction

Use the independent unreliable snapshot stream and `EGPNetSnapshotInterpolator` for high-rate poses. Reliable gameplay snapshots and RPC can wait behind retransmissions. Buffered interpolation separates the rendered pose from authoritative fixed-step physics; selected-property easing does not replace that presentation clock.

Native **SuperpositionPrediction** maintains bounded local snapshots and canonical-input history. Configure local `capture() -> PackedByteArray`, `restore(local_snapshot) -> Error`, `simulate(tick, input, replay) -> Error` and `state_hash() -> String` callbacks. `predict(tick, input)` advances local prediction; `accept([{tick, input, hash}], epoch)` validates contiguous authority frames, restores a locally captured baseline and replays the complete predicted world. It never accepts remote solver snapshots for restoration.

The default caps are 128 pending ticks, 1 MiB per snapshot and 32 MiB total history. Hash or callback failure stops prediction and releases history. Supply a trusted local baseline and a strictly newer epoch for an explicit reset. Genesis, simulation profile, creation/removal order, seeds and every input affecting the predicted collision world must match. A body selected for presentation does not limit rollback to that body.

The **EGPNetBox3DPrediction** helper offers an Inspector adapter for an explicit Box3D world, session provider, body and presentation node. Game hooks supply typed input and queue validated commands; the adapter owns `step_tick()`. The server authorizes remote input and publishes complete-world canonical frames. Native World routing accepts authority frames only from the authenticated server. This helper supplies bounded reconciliation plumbing; the game's full input/genesis, authorization, admission and resynchronization contracts remain explicit.

## Native APIs in three languages

The saved scene workflow is shared. Programmatic construction uses the native generated classes directly:

```gdscript
var world := SuperpositionWorld.new()
world.port = 10515
add_child(world)
var error: Error = world.start_server()
```

```csharp
using Godot;

var world = new SuperpositionWorld { Port = 10515 };
AddChild(world);
Error error = world.StartServer();
```

```cpp
#include <godot_cpp/classes/superposition_world.hpp>
#include <godot_cpp/core/memory.hpp>

auto *world = memnew(godot::SuperpositionWorld);
world->set_port(10515);
add_child(world);
godot::Error error = world->start_server();
```

C# requires the matching Mono engine and freshly generated GodotSharp assembly. C++ requires that editor's bundled generated SDK. Native GDScript/C++ nodes do not require Mono or a networking assembly. Existing helper facades remain available for application-specific message codecs and game flows; they share the transport with the new native components.

## Feedback and qualification

Configuration warnings identify missing targets, scene allowlists, providers and method rules. `spawn_error`, `rpc_error` and `replication_error` carry actionable error/message pairs. `get_statistics()` exposes instance counts, compatibility state/fingerprints, captures, traffic, queued/rejected calls and the last failure. World exposes its session state and last error in the Inspector.

`misc/scripts/validate_superposition_spawner_rpc.py` runs an authenticated dedicated server and two independent clients with simulated delay, jitter and loss. It checks late join, deliberately mismatched scene IDs, nested property streams, ownership/authority/schema/argument rejection, interest removal/re-entry, despawn and identity-preserving reconnect. A separate World fixture checks automatic discovery/rebinding and stale queued RPC isolation. Retain the validator's binary/source-hashed receipt; these are bounded component tests, not WAN scale or game-performance qualification.

Native property/delta, complete-world prediction and motion fixtures qualify their own contracts. Production account services, persistence, larger-world prediction costs, lag compensation, bandwidth scale, platform parity and long-running recovery need game-specific evidence. Superposition does not claim feature parity with another engine's replication system.
