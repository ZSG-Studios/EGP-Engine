# EGP integration and completion loop

The loop owns the combined EGP result, including handoffs and unfinished work from
other EGP chats and worktrees. Resume every 15 minutes and finish concrete acceptance
items promptly. A check is complete only when its recorded evidence establishes the
claimed behavior on the combined source and matching binaries.

## Workflow and merge ownership

1. Inspect Git status, worktrees, feature commits, pending PRs, handoffs and useful
   running builds. Keep a provenance inventory; include committed changes and dirty
   source files. Preserve patches and untracked source before resolving overlaps.
2. Consolidate intended changes into one canonical integration branch. Resolve
   conflicts by comparing implementation and contracts, then test the combined
   behavior. Preserve unrelated user edits and never discard a tree for convenience.
3. Select the highest impact unfinished item, reproduce the problem, implement the
   fix, and run appropriate checks. Avoid duplicate builds and overlapping mutations.
4. Record exact commands, source/binary/API hashes, output paths, failures and next
   steps. Keep old receipts separate from current combined-engine qualification.
5. Finish the combined native/Mono editors, generated C#/C++ APIs and Debug/Release
   templates. Validate imports, games, exports and relocation using matching builds.
6. Report meaningful verified progress or actionable failures. Remain quiet when
   nothing changes. Mark merged only after commits are present in the canonical
   branch; mark fully integrated only after the combined acceptance checks pass.

The user authorized taking over General Agent, Physics Agent, Networking Agent and
Build Agent on 2026-10-06. Handoffs were requested. Existing useful commands should
finish; this chat owns subsequent integration. Do not spawn extra agents merely
because these chats exist.

## Acceptance matrix

| Area | Required evidence | Current status |
| --- | --- | --- |
| Merge provenance | Every EGP tree/feature commit/dirty source accounted for; conflicts resolved; canonical commits and combined build | Inventory and handoffs in progress |
| Public API | Actual ClassDB dump matches embedded SDK, generated C# and docs; signatures, enums, properties, signals, defaults and errors consistent | Existing validator covers only part of this contract |
| API usability | Familiar naming; typed options/results; actionable errors; examples for GDScript/C#/C++; threading and ownership documented | Audit pending |
| Library/build | Native and Mono builds; exact fork bindings; dependency/license manifests; lean server build; reproducible toolchain | Earlier receipts exist; combined snapshot pending |
| Physics | Box2D/Box3D scene integration, joints, characters, queries, events, serialization, deterministic stepping and restore; unsupported capabilities exposed honestly | Physics handoff requested |
| Physics/network | Explicit fixed clock, fingerprint validation, authoritative state, commands, prediction/correction/replay and recovery | Existing limited fixtures; full game contract pending |
| Networking | Encrypted admission, account/peer/entity identities, authority, ownership, interest, lifecycle, reconnect and backpressure | Native and language fixtures exist |
| Advanced networking | Field deltas, bounded bandwidth/queues, input acknowledgments, lag compensation, scale/soak, malicious input rejection | Implementation/qualification gaps remain |
| Network lab | Dedicated server, listen host, N clients, visible windows, latency/jitter/loss, directional simulation, reconnect, logs/watchdog/cleanup | First dedicated 3-client run passed |
| Network lab expansion | Editor controls; mixed GDScript/C#/C++ clients; packaged games; IPv6; server restart; interest/ownership checks; load/soak and adverse-condition matrix | Pending |
| C++ GDExtension | Scaffold, compiler errors/navigation, Debug/Release, exact SDK, reload, ABI/restart path, exported load | C++ chat handoff requested; prior tests exist |
| C++ hot reload | Changed behavior in editor and running game, live instances/state/signals, failed build retains working code, repeat reload/unload cleanup | Partial editor smoke exists; full acceptance pending |
| C# hot reload | Build/watch notifications, live running-game change, scene/state/event preservation, failed build recovery, repeated reload/ALC cleanup | Hooks exist; current runtime qualification pending |
| Export/platform | Relocated Debug/Release games with C#/GDScript/C++ and Box physics/networking; platform-specific binaries and missing-binary diagnostics | Combined Windows and platform matrix pending |
| Performance | Identical-scenes upstream comparison; p95/p99, CPU/GPU/memory/allocations, server tick, bandwidth and long sessions | No AAA readiness claim |

AAA describes the intended quality target. Compilation, API exposure checks and
small smoke fixtures do not prove complete features, scale, performance or readiness.
Production authentication/token delivery remains a game/backend contract.

## Network lab

The launcher copies its project and current shared helpers into a unique evidence
directory. The server binds loopback and issues encrypted per-client tokens via a
temporary trusted local handoff; token bytes and private keys are never retained in
logs, receipts or the project. Headless operation is the default. `--visible` shows
clients and the host window; a dedicated server remains headless. All processes are
bounded and receive individual logs and a combined receipt with source/binary hashes.

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.console.exe --clients 3
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.console.exe --mode host --clients 3 --visible --preset wan --duration 20 --reconnect-at 8
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.console.exe --clients 4 --latency 150 --jitter 30 --loss 5 --simulate-on clients
```

Use `--help` for port, duration, profiles and directional simulation. `host` supplies
a server-owned test entity and can show a host window; this is not a completed local
player gameplay implementation. Latency is outgoing one-way per endpoint, so enabling
it on both endpoints increases the round trip accordingly. Simulation is stochastic;
a passing short run does not establish reliability under every packet-loss sequence.
The fixture checks authenticated identities, replies, ongoing authoritative tick
replication and, when selected, distinct connection generations after reconnect.

## Initial evidence, 2026-10-06

- Dedicated server plus 3 separate headless clients, 6-second run: passed. Receipt:
  `.build/egp-network-lab/1791263652476021000/receipt.json`.
- Launcher Ruff and mypy checks passed (the repository's mypy Python 3.9 setting
  produces a tool-version warning).
- The loop is active as `egp-api-and-integration-loop` every 15 minutes, attached to
  this chat; merge/takeover responsibilities are included in its saved prompt.
- Listen-host fixture plus 3 headless clients, 100 ms outgoing latency, 25 ms
  jitter and 3% loss on both endpoints, with a reconnect at 4 seconds: passed.
  Receipt: `.build/egp-network-lab/1791263789292947900/receipt.json`. An earlier
  failed receipt is retained at `1791263684601163600`: reconnect incorrectly reused
  an admission token for a new socket. The lab now refreshes admission through the
  test backend before reconnecting, and verifies distinct connection generations.
- All 7 EGP worktrees have source snapshots (tracked binary patches and untracked
  archives) under `.build/integration-takeover/1791263742021680000/inventory.json`.
  The snapshots preserve historical qualification trees; their older source must
  not overwrite current primary physics/networking code.
- C++, networking and build handoffs received: `.build/cpp-final-handoff.md`,
  `.build/networking-final-handoff.json`, `.build/fastbuild-api-handoff.md`.
  These owners have stood down with no local builds running. C++ tip is local
  `2624581a`; its PR is #1. Networking/physics/FASTBuild work is predominantly
  uncommitted in primary. Physics handoff remains pending.
- Next: merge intended current source and C++ ancestry, resolve the native-only
  backend hook audit decision, build matching combined artifacts, then qualify
  API/reload/migration and Debug/Release interop gates.
