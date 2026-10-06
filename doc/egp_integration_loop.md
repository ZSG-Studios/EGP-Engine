# EGP integration and completion loop

The loop owns the combined EGP result, including handoffs and unfinished work from
other EGP chats and worktrees. Resume every 5 minutes and finish concrete acceptance
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
| Merge provenance | Every EGP tree/feature commit/dirty source accounted for; conflicts resolved; canonical commits and combined build | Seven trees preserved/accounted for; canonical commits present; final combined gates and publication pending |
| Public API | Actual ClassDB dump matches embedded SDK, generated C# and docs; signatures, enums, properties, signals, defaults and errors consistent | Paired live dumps and 77-class audit added; refreshed binding gate pending; behavioral/signature coverage incomplete |
| API usability | Familiar naming; typed options/results; actionable errors; examples for GDScript/C#/C++; threading and ownership documented | Audit pending |
| Library/build | Native and Mono builds; exact fork bindings; dependency/license manifests; lean server build; reproducible toolchain | Earlier receipts exist; combined snapshot pending |
| Physics | Box2D/Box3D scene integration, joints, characters, queries, events, serialization, deterministic stepping and restore; unsupported capabilities exposed honestly | Handoff consolidated; Box2D 12 runs passed; Box3D 26/28 passed before fixing missing query bindings; rerun pending |
| Physics/network | Explicit fixed clock, fingerprint validation, authoritative state, commands, prediction/correction/replay and recovery | Existing limited fixtures; full game contract pending |
| Networking | Encrypted admission, account/peer/entity identities, authority, ownership, interest, lifecycle, reconnect and backpressure | Native and language fixtures exist |
| Advanced networking | Field deltas, bounded bandwidth/queues, input acknowledgments, lag compensation, scale/soak, malicious input rejection | Implementation/qualification gaps remain |
| Network lab | Dedicated server, listen host, N clients, visible windows, latency/jitter/loss, directional simulation, reconnect, logs/watchdog/cleanup | Dedicated/host headless runs passed; host plus 3 verified visible windows passed on earlier engine; final combined rerun pending |
| Network lab expansion | Editor controls; mixed GDScript/C#/C++ clients; packaged games; IPv6; server restart; interest/ownership checks; load/soak and adverse-condition matrix | Pending |
| C++ GDExtension | Scaffold, compiler errors/navigation, Debug/Release, exact SDK, reload, ABI/restart path, exported load | Handoff/tip merged locally; headless editor regression passed; combined Debug/Release exports pending |
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
- The loop is active as `egp-api-and-integration-loop` every 5 minutes, attached to
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
  uncommitted in primary at handoff. Physics handoff is now received at
  `.build/physics-consolidation-handoff.md`; all four chats have stood down.
- Next: merge intended current source and C++ ancestry, resolve the native-only
  backend hook audit decision, build matching combined artifacts, then qualify
  API/reload/migration and Debug/Release interop gates.

## Consolidation state and immediate continuation

Canonical branch: `codex/egp-integration` in the primary checkout. Source integration
commit `25aa0fdf2e` includes Box physics, Yojimbo, language helpers, FASTBuild, API
audit and network lab. Merge `3278f8b8dc` includes C++ tip `2624581a26`. Later commits
repair compilation and expose debug contacts, remove leftover CSG/GridMap priority
and collision-polygon margin APIs, and give joint toggle properties boolean methods.
Remote master remains `f4389f3a`; publication/final merge is pending combined gates.

| Tree | Integration decision |
| --- | --- |
| Primary | Current authoritative source, committed on integration branch |
| native-cpp-validation | Tip 2624581a merged; workflow dirty status has an empty content diff |
| box3d-parity | Historical base plus physics snapshots; current frozen native qualification build |
| net-trilingual-api | Old qualified source superseded by frozen canonical Mono snapshot; keep ignored caches/binaries |
| box-physics-cutover | Earlier qualification snapshot; current primary supersedes its code |
| cpp-combined | Older C++/Mono test snapshot; preserve evidence, do not copy over current code |
| egp-network-qualification | Historical networking snapshot, including retired stack; retain evidence, exclude superseded sources |

Current build supervision (do not start duplicates):

- Native physics editor: shell session `69971`, runner `.build/build_box3d_parity.py`,
  worktree `box3d-parity/EGP`. Log `.build/box3d-parity-editor-build.log`; receipt
  `.build/box3d-parity-editor-build.json`. Frozen source manifest
  `.build/box3d-parity-source-snapshot.json`. Inspect receipt timestamps; older
  receipts may still exist while a new run is active. Original session `88174`
  failed on collision-polygon margin and is finished. Session `89582` passed and
  supplied the 26/28 Box3D and 12-run Box2D baseline. Current frozen engine source is
  `557887f4b2`, including the safe query bindings from `3c55d6eb4c`.
- Combined Mono editor, fresh exact SDK/API, glue/assemblies, Debug and Release
  templates: shell session `94709`, runner `.build/build_combined_mono.py`, worktree
  `net-trilingual-api/EGP`. Current PID/step/log in
  `.build/integration-combined-mono/active.json`; frozen source `source.json`; stage
  logs and completion `receipt.json` in that directory. Script waits for each stage
  and uses a 7200-second build watchdog. A stopped first attempt is preserved under
  `.build/integration-takeover/mono-bootstrap-stopped*`. Session `7202` completed
  matched editor, glue and managed assemblies, then was stopped before obsolete
  templates. Its logs/source/receipts are retained at
  `.build/integration-takeover/query-binding-checkpoint-1791265230277053500/mono`.
  Frozen current source is `557887f4b2`; later acceptance-tool/checklist changes do
  not modify engine implementation. Do not copy them into an active build tree.
- Native networking on combined primary source passed: 101 checks and CTest,
  `.build/integration-native-net/receipt.json`. Box3D Release golden trajectory,
  native joints/replay and upstream suite passed:
  `.build/integration-box3d-native/Release/receipt.json`.
- C++ editor-panel regression passed with clean shutdown after repairing the
  fixture's close notification. Permanent validator:
  `misc/scripts/validate_egp_cpp_ui.py`; receipt
  `.build/egp-cpp-ui/1791264409488401100/receipt.json`. It verifies scaffold,
  registration, SDK reuse, deliberate compiler failure, diagnostics/source save,
  changed implementation reload and Release mappings through editor controls.
  This is headless validation on the earlier native binary, not current combined
  engine or running-game reload qualification.
- Network lab also passed client-only wifi impairment:
  `.build/egp-network-lab/1791264175579256000/receipt.json`. A 100% packet-loss run
  correctly failed and its processes exited; receipt at `1791263869496218800`.
  The launcher now retains per-process results on failure and hashes both the
  console wrapper and actual engine executable.

When builds finish, run focused native capabilities/soft-body/damping fixtures, then
full Box2D/Box3D scenes, migration preflight, exact API audit with **newly generated**
managed files and extracted SDK, networking runtime and relocated Debug/Release
trilingual exports. `.build/integration-api-prerebuild.json` correctly rejects old
Script RPC getters; `.build/integration-api-historical-baseline.json` demonstrates
that primary generated glue is stale. Neither is current parity evidence.
`doc/egp_api_contract.md` labels 19 pointer virtual callbacks native-only; the audit
verifies their signatures remain virtual/pointer interfaces and still checks C++.

The expanded physics build's first failure (obsolete MT wrapper/CSG calls and
soft-body C++ declaration) and second failure (collision-polygon margin) are retained
under `.build/integration-takeover/physics-expanded-first-*` and
`physics-margin-failure.*`. Keep them while proving fixes. Do not copy source into
a worktree while its build is running.

After combined gates pass, complete the authorized final merge/push to master,
verify remote SHA and reconcile PR #1. Preserve both accidental root artifacts
(`Temp Plan.md`, `tagged_query_test.b3rec`) outside commits. Re-run the permanent C++
panel validator against the final combined editor, then proceed to actual C#/C++
running-game reload, state/instances/events and repeat-load cleanup. Those remain
incomplete. Keep the repaired deferred EditorNode close path in the fixture; do
not restore recursive root notification propagation.

## Live reflection and query-binding continuation

- Engine source commits `3c55d6eb4c` and `557887f4b2` expose owned RID-array queries
  for body/soft-body collision exclusions and finite nonnegative contact-depth
  thresholds in both dimensions. XML schemas passed. The fresh scene suite must
  establish behavior; source exposure alone is not sufficient.
- Before those bindings, native engine SHA256
  `9b405f0a69a47acc41ece215c80eac4e8628363a763ea3e587d5dc3c21ffb402` passed
  `python misc/scripts/validate_box2d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.exe --output .build/integration-box2d-scenes`:
  12 runs, including one/four-worker trajectory equality. Box3D's complete 28-case
  suite at `.build/integration-box3d-scenes-before-bindings/receipt.json` passed 26
  and rejected two fixtures at parse time for the missing bindings. Keep these
  failures as evidence, then rerun into new output paths after the builds pass.
- `python misc/scripts/test_validate_egp_api.py` passed 8 regression checks.
  These reject absent/partial reflection, stale API/engine hashes, incorrect bool
  types, unsafe native-only waivers and incorrectly omitted Variant properties.
  They also record the two explicitly internal debug signals while rejecting
  other missing signals. `_integrate_forces` remains required for both rigid-body
  scene APIs; the old removed-member entry for RigidBody2D was erroneous.
- Paired dump command:
  `python misc/scripts/capture_egp_api.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.console.exe --output .build/integration-api-capture-before-queries`.
  Receipt `.build/integration-api-capture-before-queries/1791265380253952100/receipt.json`
  records API SHA256 `975d818ba5a4f16c37e8e4fa8befaee4a7af0c541786fc5e3fb6391a5e734f71`,
  actual Mono engine SHA256 `4c22e7a197017bddd7c76d15534857df95fe2ffbab4f6350cf3e660931d3cca7`.
  The editor's `--cpp-check` extracted its actual SDK at
  `%LOCALAPPDATA%/Godot/egp_cpp/sdk/93559c3a85cd1944`. The 77-class audit against
  generated/compiled managed sources reports only the seven expected missing new
  query methods in `.build/integration-api-before-query-bindings.json`. This is
  deliberately failing pre-refresh evidence, not current parity success.
- `python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.console.exe --mode host --clients 3 --visible --preset wan --duration 10 --reconnect-at 4 --output .build/integration-network-windows`
  passed at `.build/integration-network-windows/1791265798979372900/receipt.json`:
  four distinct visible top-level windows belonging to owned GUI processes,
  replies/authoritative ticks and reconnects under 100 ms latency, 25 ms jitter,
  3% loss. All processes exited with code zero. Actual engine SHA256 begins
  `d09f9b74379e`; this proves the launcher on the earlier engine, and must be rerun
  on the final combined engine. Window presence does not establish visual quality.
- API capture/audit/regression and launcher tools passed Ruff checks, formatting
  and mypy (the existing Python 3.9 setting produces a mypy-version warning).
  PR #1's remote static failure was Ruff formatting; local C++ followup `2624581a`
  already contains that formatting fix. Its native CI jobs were still running
  when inspected; the remote PR is not the whole consolidated source.
- A prematurely dispatched build overlapped source preparation. Both owned retry
  runners were stopped, snapshots finished and their two later committed files
  were advanced before restarting sessions `69971` and `94709`. Interrupted logs
  are retained in `.build/integration-takeover/query-binding-overlap-stopped`;
  those attempts provide no qualification evidence. Always await a source-freeze
  command's completion before starting a dependent build.

Next run the complete native Box2D/Box3D scenes (including the new server-query and
scene-integrator fixtures), repair genuine behavior failures, then capture and audit
the final Mono API with `--classdb`, the freshly extracted SDK and newly generated
managed files. Continue existing Debug/Release build ownership rather than starting
duplicates. Remote final merge and actual running-game reload remain unfinished.
