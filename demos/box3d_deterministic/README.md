# Deterministic Box3D networking

One dedicated server and two independently authenticated clients simulate the same Box3D world: two players, a mixed physics stack, shockwave impulses, respawning props and a moving kinematic platform. Each client predicts locally, receives canonical tick-stamped inputs, rolls back to its own locally captured solver state when those inputs differ, and replays the complete physics world. Every acknowledged tick must match the server's diagnostic state hash.

```powershell
powershell -ExecutionPolicy Bypass -File demos/box3d_deterministic/launch.ps1
```

Both clients start with AI movement. Arrow keys move, Space jumps, Q pushes nearby props, and B toggles AI. The HUD shows verified ticks, rollback and replay counts, pending inputs and bounded history memory. Unlike the 52-player arena, these clients run physics and predict movement; they do not receive pose streams.

The actual native transport applies 120 ms latency, 40 ms jitter and 5% loss to each outgoing direction. The server repeats a bounded window of unacknowledged canonical inputs over an unreliable channel at 20 Hz. Client acknowledgments ride on their sequenced movement packets. A 240-tick server journal and a 128-tick client prediction history bound memory and permitted lag. Falling outside those limits requires a new session; it does not silently skip simulation ticks.

The world starts from identical, code-defined genesis, matching simulation fingerprints, stable body IDs, deterministic command ordering and identical 60 Hz / four-substep physics. The server chooses accepted inputs and timeout policy. All replayed gameplay decisions use tick time. Clients restore only snapshots they captured themselves: native solver snapshot bytes never cross the network. Hash disagreement fails closed, preserving evidence instead of treating divergent physics as verified.

Run `launch.ps1 -Validate` for the independent-process prediction/rollback/hash test. Run `launch.ps1 -Validate -Fault Hash` to verify that deliberate hash corruption is rejected. Receipts and logs are under `.build/deterministic-demo/`. The arena's `stop.ps1 -RunPath <runtime>` also stops this demo's exact recorded processes.

`EGPNetDeterministicReplay` is a reusable GDScript helper with game-supplied capture, restore, simulate and hash callbacks. This sample qualifies a shared local Windows build and profile. A matching fingerprint alone does not prove cross-platform determinism, and the state hash covers documented observable physics state rather than every latent solver field. This is not yet the 52-player arena's networking mode, a late-join baseline system, or a C#/C++ rollback adapter. Rolling back a complete 52-player world on every client requires separate CPU, bandwidth and history qualification.
