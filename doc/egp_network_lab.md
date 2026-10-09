# Superpos fixtures and application demos

Use native Superpos sessions and matching generated bindings. The retired
Yojimbo network lab, Superposition helpers and earlier arena receipts target
a different protocol and do not qualify these examples.

## Native contract fixture

`misc/egp/superpos_lab` exercises explicit schemas, canonical publication,
owner retirement and authenticated separate-process loopback packet delivery.
With an already built, matching editor, run from the engine checkout:

```powershell
python misc/scripts/validate_superpos.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe
```

Diagnostics are replaced under `.build/diagnostics/superpos`. This focused
fixture does not establish production gameplay, WAN capacity or platform parity.

## Physics showcase

[The physics showcase](../demos/physics_superpos_showcase/README.md) runs two
independent authenticated UDP associations in one process. A native Box3D
world steps at 60 Hz and sends validated poses at 10 Hz; a separate viewport
explicitly projects received canonical state. Four controls travel back
through the second association. Rewind restores a trusted local solver
snapshot; it does not transmit portable physics state.

Run the demo's `launch.ps1` with the current Mono editor, or use `verify.ps1`
for bounded headless and Vulkan checks. Evidence uses the fixed
`.build/diagnostics/physics-superpos-showcase` directory. Continuous 20 Hz
fragmented pose traffic exhibited control starvation; the demo uses its
documented 10 Hz workload. Separate-process transport is checked elsewhere.

## Remote courier arena

[The courier arena](../demos/superpos_100_player_lab/README.md) uses one remote
headless Box3D server, four local processes containing 25 independent bot
sessions each, and one playable client: 101 streams in total. Gameplay travels
over native UDP/DTLS through the existing VPN; SSH supplies supervision and
private bootstrap. Per-stream packet proxies apply bounded, seeded delay,
jitter, loss, duplication, reordering and bandwidth limits.

Run `python run_lab.py --duration 1800` from the demo directory with the
existing remote native editor and configured SSH/VPN access. This launches
existing binaries and updates the lab source; it does not build the engine.
The 80-second recorded qualification in
`.build/diagnostics/superpos-100/qualification.json` passed all 101 streams
and recovered all 20 blackout streams. It recorded 10,174 drops, 537 duplicates,
4,135 reorder events and a remote solver-step p95 of 1.505 ms. Solver timing
excludes networking and application frame cost. Physical human input was not
automated, and this workload does not establish a production capacity limit.

The server publishes each client's nearest-32 interest frame. Clients validate
complete frames and explicitly apply them; extrapolation and smoothing belong
to the application. Admission uses pre-provisioned associations rather than
public matchmaking, NAT traversal or a general multi-peer listener. Credentials
remain private and are never retained in telemetry.

See [native networking](egp_superpos.md) and [migration](egp_superpos_migration.md)
for ownership, API and prediction boundaries. Build changes must follow the
workspace's remote execution policy and canonical output locations.
