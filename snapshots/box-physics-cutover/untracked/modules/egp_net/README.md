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

Add `EGPNet` to your scene or autoload. Configure both endpoints with the same
game protocol, simulation fingerprint and tick rate. Configure the native
Box3D world before networking and use its `get_simulation_fingerprint()` in
the options. `simulation_tick(tick, server)` is the fixed clock; feed validated
commands to the explicit Box3D world and call `step_tick(world.get_tick()+1)`
once per tick. Ordinary scene physics still uses SceneTree's physics clock.
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
generates one locally. Account authentication, token delivery and matchmaking
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
states over reliable ordered delivery. It is not a field-delta schema or a
prediction engine. Client prediction/reconciliation, lag-compensated hit tests,
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
The validator optionally runs it using `--engine /path/to/egp-editor`.
Windows native Debug initially passes 46 checks plus both upstream CTest suites.
Engine and exported-game qualification must use current binaries.
