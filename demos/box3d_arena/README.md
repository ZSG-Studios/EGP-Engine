> **Legacy migration source:** this guide and its fixtures use the retired networking module. They are disabled in the default Superpos build and do not qualify Superpos. See ../../doc/egp_superpos.md for the replacement API.

# EGP Box3D Arena

For physics prediction, solver rollback and canonical-input replay with per-tick hash verification, see the companion [deterministic networking demo](../box3d_deterministic/README.md). The engine also supplies the native `SuperpositionPrediction` journal for GDScript, C# and C++, with an Inspector-configured `EGPNetBox3DPrediction` GDScript adapter. Games must provide deterministic complete-world input callbacks and matching trusted genesis state; the adapter cannot infer gameplay rules. The 52-player arena uses authoritative pose streaming and does not run complete-world client rollback.

The arena server enables the engine's native `physics/box3d/audit_determinism` diagnostic. A separate native solver receives every canonical physics command, including admission, AI attacks, player movement, body spawning, kinematic motion and trusted checkpoint restores. Hashes are compared at each completed step and command boundary; divergence stops the operation. Status receipts expose verified steps and mismatch evidence, and validation requires zero mismatches. This checks the complete arena backend while its 52 clients keep independent network replicas. The diagnostic intentionally doubles solver work and is disabled by default in other projects.

A reusable GDScript game base: one dedicated authoritative server, up to 52 independently authenticated Yojimbo clients, six tactical AI pursuers, collectible energy cores, shockwaves, spawnable physics props and trusted solver checkpoints in a neon arena. Clients send sequenced 28-byte movement packets at 20 Hz through Yojimbo's unreliable transport. The server checks packet layout, ownership, sequence, finite values and action limits, then simulates Box3D at 60 Hz with four substeps.

The POS4 pose stream carries quantised position, rotation, linear velocity, world-space angular velocity and a monotonic discontinuity epoch. Each unreliable packet contains at most 23 bodies and occupies at most 890 bytes. Three packets preserve the existing 62-body interest budget: the client's own player, six AI and six mechanisms, nearby players and props, plus rotating distant bodies. Clients reject stale ticks and older epochs. Checkpoint restores and boundary rescues increment the affected epochs, clearing presentation history and correction offsets even for short relocations. Ordinary fast motion no longer relies on a distance threshold to identify teleports.

Clients use the native `EGPNetSnapshotInterpolator` with a 200 ms base buffer, optional adaptive buffering capped at 400 ms, and at most 100 ms of linear and angular extrapolation. Adaptive buffering responds to sparse authoritative sample spacing; it can add up to 200 ms of presentation latency for all entities on the shared timeline. It does not remove transport delay. The monotonic presentation clock slews within ten percent of the nominal tick rate and stops at the newest authority tick during outages. Position and shortest-arc rotation corrections decay when extrapolated entities receive fresh samples. Set the fourth `configure` argument to zero to disable adaptation. The distant round robin fills the remaining three-packet capacity.

Status receipts report buffered ticks, target delay, interpolation, extrapolation, holds, corrections and explicit epoch resets. Per-class counts separate players, AI, mechanisms and props; held samples also distinguish moving from stationary bodies. These are entity presentation samples, not packet-loss measurements. Reliable 76-byte entity states provide admission baselines and one-second fallback snapshots; lifecycle, action requests and scoreboards stay reliable. Cosmetic shockwaves use an unreliable channel so they do not delay action requests or score updates. Fast movement never waits for acknowledgment of an older pose.

The scene also includes a native `Superposition` node. Four exported gameplay integers are selected through its Inspector-compatible configuration: TEAL and AMBER team scores, total pickups and completed checkpoint restores. The node resolves the configured `Network` session and captures at 4 Hz, sending only changed reliable states. Motion continues on the separate unreliable snapshot stream. You can select additional supported properties in the Replicate Inspector checkboxes using the same workflow.

From this repository, run:

```powershell
powershell -ExecutionPolicy Bypass -File demos/box3d_arena/launch.ps1
```

For 50 headless tactical players plus the two rendered players:

```powershell
powershell -ExecutionPolicy Bypass -File demos/box3d_arena/launch.ps1 -HeadlessClients 50 -StressCount 32
```

Every player is a separate process with a unique authenticated identity and UDP socket, bound to a distinct local address (`127.0.0.2` through `127.0.0.53`). These are loopback addresses on one computer, not separate public Internet connections. The launcher records Windows UDP endpoints in `udp-endpoints.json`. Headless players stop after ten minutes; use the printed runtime path with `stop.ps1` to stop the whole demo. Headless clients run at 20 Hz and visible clients at up to 60 FPS. The default physics stress adds 32 mixed boxes, spheres and capsules, stacked bodies, three moving lifts and three horizontal rams. Set `-StressCount` between 0 and 192.

`-Validate -RenderClients -HeadlessClients 50` runs a bounded 52-client stress check, requiring at least 30 seconds with all clients connected, responsive owned pose streams, tactical decisions, interactions, fixed-step physics within budget and recovery of the two deliberately stalled rendered clients. Passing this local workload does not establish WAN capacity or release readiness.

Historical qualification: the earlier b2 binary passed the 52-player RoughWifi run recorded in `.build/arena-demo/20261007-141318-830/validation.json`: 76.05 seconds at full admission, 6,593 audited solver steps with zero mismatches, worst full-load physics p95 of 3.56 ms, and both rendered clients at 60 FPS. That run used the earlier fixed buffer and distance-based reset detection; its aggregate held/extrapolated samples do not qualify the newer epoch and adaptive-buffer behavior. The expanded native interpolation fixture subsequently passed 1,361 checks, including epoch ordering and angular extrapolation; its synthetic sparse-cadence test reduced held samples from 343 to 15 by spending additional buffering latency. Full-load results for the current implementation must be read from its own runtime receipt.

The server runs hidden and headless; both client windows open with independent following cameras. Each begins as an independent tactical bot, using only its own delayed replicated world to choose energy objectives, avoid crowds and machinery, evade enemies, jump props and counterattack with shockwaves. They anticipate nearby motion and change routes when progress stalls. Players collide with each other, push shared props, collect energy and evade the AI pack. Server AI chooses the nearest player, leads moving targets, alternates flanking directions, separates neighbors, lunges at close range and retreats after shockwaves. Its visible colors indicate patrol, pursuit, strike and evasion states. These are hand-authored tactical rules, not a learned AI or navigation mesh system.

| Control | Action |
| --- | --- |
| WASD | Take control and move |
| Space | Jump |
| Q | Shockwave: knock nearby bodies away and scatter AI |
| E | Drop a new sphere or box; at most 20 additional props |
| R | Restore the server's trusted solver checkpoint |
| B | Toggle showcase autopilot |
| Tab | Toggle following camera and arena overview |

Checkpoints are recaptured when physics membership changes. A restore request waits up to 15 seconds for a fresh checkpoint and full player admission; it expires if its requesting player leaves. Shockwaves, spawns and restores have server cooldowns. The visible showcase spawns props and fires shockwaves; checkpoint rewind runs automatically only during the bounded validation, so ordinary play does not teleport at ten seconds. Gold energy cores respawn after five seconds; collection and scores are server owned. Bodies knocked outside the arena or below its floor respawn safely. Manual R restores intentionally relocate bodies; explicit reset epochs clear their render history and correction offsets.

The default **Broadband** profile applies **70 ms delay, 20 ms jitter and 2% packet loss to each endpoint's outgoing transport**. This is actual Yojimbo transport impairment. The nominal round-trip delay is 140 ms; the displayed application echo RTT also includes polling and scheduling, measured using an unreliable echo independently of the reliable event queue. Choose `-Network Clean` for no impairment or `-Network RoughWifi` for 120 ms delay, 40 ms jitter and 5% loss per outgoing direction. These are synthetic profiles, not measurements of a particular real connection. Shader disk caching is disabled in this small sample to prevent simultaneously launched GLES clients racing over shared cache directories.

`launch.ps1 -Validate -Network RoughWifi` runs a bounded headless integration check: clients receive moving entities; AI attacks, spawns, shockwaves, scoring and checkpoint restoration execute; each client deliberately stalls for 700 ms and recovers through a fresh token and authoritative baseline. The bounded validation freezes score collection during its final readback period while physics, input, AI and pose streaming continue. Every client must apply native Superposition state and match the four final server values; the server must demonstrate skipped unchanged captures. Interactive runs keep scoring throughout. The native 500 ms catch-up watchdog stays enabled. Recovery attempts are capped at three. Receive logs expose fast-pose counts, owned-pose arrival gaps, frame timing and the largest wall-clock gap. Logs, process identities, local admission tokens, screenshots and status receipts live under `.build/arena-demo/`. Poll failures also save the actual poll gap, native diagnostic and pre-shutdown statistics in a separate failure receipt. To close only this demo's processes, run `stop.ps1 -RunPath <the printed runtime directory>`.

Use an EGP editor built with the single-entity cache refresh command added alongside this demo. The launcher defaults to the matching local Mono editor, although this project uses GDScript and does not require managed scripts. Set `-Engine` to another compatible EGP executable. Open `project.godot` in that editor to develop the game.

The server and rendering are separated in `arena.gd`. Replace the procedural visuals and movement rules with your game scenes; preserve input validation and server ownership. The copied `addons/egp_net` helpers match the engine's native API.

Admission tokens are handed off through local files for this loopback demonstration. A deployed game needs its own trusted authentication service. This sample demonstrates current Box3D and native networking; it does not demonstrate every engine feature, client prediction/rollback, C#/C++ gameplay, hot reload, WAN scale or a production character controller.
