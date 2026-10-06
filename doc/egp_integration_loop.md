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
| Merge provenance | Every EGP tree/feature commit/dirty source accounted for; conflicts resolved; canonical commits and combined build | Seven trees preserved/accounted for; intended handoffs consolidated, combined gates pass and qualified source published to master; remaining feature acceptance continues |
| Public API | Actual ClassDB dump matches embedded SDK, generated C# and docs; signatures, enums, properties, signals, defaults and errors consistent | 77 classes, 1299 method contracts, 55 signal contracts, 371 enum constants and 1358 compiled descriptions pass; broader behavioral and semantic documentation coverage remains |
| API usability | Familiar naming; typed options/results; actionable errors; examples for GDScript/C#/C++; threading and ownership documented | Native session options/results/errors and physics event/joint contracts documented; broader facade ergonomics audit remains |
| Library/build | Native and Mono builds; exact fork bindings; dependency/license manifests; lean server build; reproducible toolchain | Combined Mono editor, glue/assemblies, exact SDK and Debug/Release templates pass; lean server/platform/reproducibility gates remain |
| Physics | Box2D/Box3D scene integration, joints, characters, queries, events, serialization, deterministic stepping and restore; unsupported capabilities exposed honestly | Combined Mono editor passes 19 Box2D runs and 28 Box3D cases; twelve relocated Debug/Release Box2D checks cover configured joints, events, explosions, canvas/casts, packed/vector polygons and invalid-input recovery; picking, broader shape/scaling/parity/platform gates remain |
| Physics/network | Explicit fixed clock, fingerprint validation, authoritative state, commands, prediction/correction/replay and recovery | Existing limited fixtures; full game contract pending |
| Networking | Encrypted admission, account/peer/entity identities, authority, ownership, interest, lifecycle, reconnect and backpressure | Native/language fixtures, matching-budget 64-entity fairness, bounded receive bursts and eight encrypted WAN clients with changing 4096-byte states pass; baseline, interest, revocation and reconnect are covered; larger worlds, peak load and soak remain |
| Advanced networking | Field deltas, bounded bandwidth/queues, input acknowledgments, lag compensation, scale/soak, malicious input rejection | Implementation/qualification gaps remain |
| Network lab | Dedicated server, listen host, N clients, visible windows, latency/jitter/loss, directional simulation, reconnect, logs/watchdog/cleanup | Native host and packaged Mono Debug host/Release dedicated server pass simultaneous visible clients, WAN simulation and reconnect; broader matrix remains |
| Network lab expansion | Editor controls; mixed GDScript/C#/C++ clients; packaged games; IPv6; server restart; interest/ownership checks; load/soak and adverse-condition matrix | Packaged Debug graceful and Release abrupt dedicated-server replacement pass with three persistent visible clients under WAN impairment; eight-second headless outage passes; single and repeated selected-client recovery and same-process server clock rejection/checkpoint/fresh admission/ownership recovery pass in editor and packaged Debug host/Release dedicated WAN runs; retired-token and retired-entity input rejection are covered; broader controls/scale/soak remain |
| C++ GDExtension | Scaffold, compiler errors/navigation, Debug/Release, exact SDK, reload, ABI/restart path, exported load | Matching SDK/editor controls, mixed-language exports, dynamic signature changes and rejected hierarchy/class repair pass on Windows; six cached instance/static call paths and nine return kinds now pass on Windows Debug SDK; arbitrary ABI changes and other platforms remain |
| C++ hot reload | Changed behavior in editor and running game, live instances/state/signals, failed build retains working code, repeat reload/unload cleanup | Two live Debug rebuilds preserve existing IDs, property state, callables and signals; failed compile retains published code; Missing/invalid DLL recovery and rejected base/extension-parent/ancestor/class-removal repair preserve extension and editable parent state; cached binding failure/default-return and compatible repair checks now pass; arbitrary ABI changes and soak remain |
| C# hot reload | Build/watch notifications, live running-game change, scene/state/event preservation, failed build recovery, repeated reload/ALC cleanup | Six combined native/managed reloads including corrupted-DLL and blocked-unload recovery preserve instances, properties and events; default/feature overrides and no-change command pass; broader script-type/state/long-session matrix remains |
| Export/platform | Relocated Debug/Release games with C#/GDScript/C++ and Box physics/networking; platform-specific binaries and missing-binary diagnostics | Relocated Windows Debug/Release trilingual games pass on the matching API; broader platform/export matrix remains |
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
The [network lab guide](egp_network_lab.md) documents packaged runs, graceful/abrupt
server replacement, outage bounds, fresh admission and explicit checkpoint scope.

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

- Native physics editor: shell session `69971` completed successfully, runner `.build/build_box3d_parity.py`,
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

### Refreshed native verification

Native build `69971` finished with exit code 0 in 561.875 seconds on frozen engine
source `557887f4b252e8aeaa76e6a7d03517d7eda74d35`. Actual editor SHA256 is
`3f8ec2b0e34ebe73b908c2fda463a15c2c985ab04002cca39d5f4bdc3af2ead9`.

- `python misc/scripts/validate_box2d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.exe --output .build/integration-box2d-query-scenes`
  passed all 14 runs, including the new scene `_integrate_forces` callback and
  server query fixtures, owned-result mutation isolation, and matching one/four
  worker trajectories. Receipt `.build/integration-box2d-query-scenes/receipt.json`.
- `python misc/scripts/validate_box3d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.exe --output .build/integration-box3d-query-scenes`
  passed all 28 cases. The damping threshold and soft-body fixtures now execute
  and pass, including mesh/pins/gravity/floor contacts, ray query, owned exclusion
  result, space migration and teardown. Receipt
  `.build/integration-box3d-query-scenes/receipt.json`. These cases do not establish
  triangle-based soft collision, all queries, broad rollback or full parity.
- `python misc/scripts/capture_egp_api.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.console.exe --output .build/integration-native-api-query-capture`
  passed; receipt `1791265997225959900/receipt.json` under that output. Actual API
  SHA256 is `6516096bb9a4ec2ff9a4f08627595a8cad38a935e5d81c2d724d624eb910493c`.
  All seven new methods are present; exclusion queries return `typedarray::RID`.
  ClassDB reports Variant BOOL (type 1) for Hinge spring and Slider limit/motor/spring
  toggles. This native editor's embedded SDK still requires matching regeneration;
  final C#/C++ parity must use the combined Mono build.
- `python misc/scripts/launch_egp_network_lab.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.console.exe --mode host --clients 3 --visible --preset wan --duration 10 --reconnect-at 4 --output .build/integration-network-windows-concurrent`
  passed with **four windows visible simultaneously**, authoritative ticks/replies,
  distinct reconnect generations and clean exits. Receipt
  `.build/integration-network-windows-concurrent/1791266120084153400/receipt.json`.
  The launcher now rejects requests whose windows were only observed at separate
  times. An earlier refreshed-native visible pass is retained at
  `.build/integration-network-windows-query-engine/1791266055561160000/receipt.json`.

The previous next step (native scene rerun) is complete. Continue session `94709`
through matching Mono SDK/editor, glue, managed assemblies and Debug/Release
templates. Re-run paired capture, exact API audit, C++ editor controls, trilingual
exports and network/physics fixtures on those final artifacts. The canonical source
includes the current fixes; remote master publication and running-game hot reload
acceptance remain pending. Preserve both unrelated root artifacts untracked.

### Combined Mono/export and migration continuation (2026-10-06)

The owned combined pipeline finished at `.build/integration-combined-mono/receipt.json`,
frozen engine source `557887f4b252e8aeaa76e6a7d03517d7eda74d35`. Editor, matching SDK,
glue, managed assemblies and both templates passed. Editor SHA256
`a0a29b9d84ea49dfa8fd01e2bc006d98c5dc861e53e50e4b9ec7b95f5ca0177b`, Debug template
`fa230aaaefd061f6425d7a34965d3ea2e66689381183eb40a24d2f3f423fd449`, optimized Release
`759f39a346cc9741ba45c4ece3b53f60feff72bc283372b2091efe3363d28718`.
Session `94709` initially failed managed file publication because an API-capture
process held a managed DLL. The capture ended; session `82775` resumed completed
stages. The initial failure receipt/logs remain in `resume-checkpoint-*`. All
build sessions are finished; do not duplicate those template builds.

- Paired capture `.build/integration-final-mono-api/1791266292794423800/receipt.json`
  passed. Actual API SHA256
  `0e8ae8b8c3f78acbd21c8e93dd9e86dd8ab8baaa1b64de1bc948b64e12fd60e1` matches SDK
  `%LOCALAPPDATA%/Godot/egp_cpp/sdk/98eaaf3b208b7881`.
  `python misc/scripts/validate_egp_api.py --api .build/integration-final-mono-api/1791266292794423800/extension_api.json --classdb .build/integration-final-mono-api/1791266292794423800/classdb.json --sdk C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/sdk/98eaaf3b208b7881 --managed C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/modules/mono/glue/GodotSharp --changes misc/egp/api_contract.json --docs . --output .build/integration-enum-api-audit-fixed.json`
  passes 77 class exposure checks and 371 documented enum constants in 17 classes.
  This is structural exposure; broad signatures/defaults/runtime behavior remain.
- The XML enum audit repaired 14 numeric values, 50 missing constants and 22 empty
  descriptions. It checks enum identity/value, retired names and nonempty text.
  `python misc/scripts/test_validate_egp_api.py` passes ten regressions, including
  deliberately stale, missing, empty and wrongly categorized docs. Six changed
  XML schemas, Ruff and mypy pass (existing Python 3.9 mypy warning remains).
  ConeTwist/6DOF docs no longer claim those implemented native joints are absent.
- `python misc/scripts/validate_egp_cpp_ui.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-mono-cpp-ui`
  passed at `1791266434797119900/receipt.json`: editor controls, diagnostics,
  shared SDK cache, failure recovery, changed implementation reload, Release
  mappings and clean shutdown. Existing running-game instances/state are untested.
- `python misc/scripts/validate_egp_net_languages.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --sdk C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/sdk/98eaaf3b208b7881 --sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/98eaaf3b208b7881/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib --release-sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/98eaaf3b208b7881/MSVC-19.51.36260.0-Windows-AMD64-x64/Release/egp_godot_cpp.lib --packages C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/GodotSharp/Tools/nupkgs --template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_debug.x86_64.mono.exe --release-template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe --output .build/integration-final-net-languages`
  passed all stages: isolated packages, C#/C++ Debug/Release builds, cold import,
  mixed-language interoperability, separate GDScript/C#/C++ clients and relocated
  Debug/optimized Release exports. Logs and artifact hashes remain in its receipt.
- The first Mono migration gate failed on obsolete class-rename targets. Source
  `3d58a53b70` removes retired physics/WebRTC targets and diagnoses legacy custom
  physics backends and WebRTC native/extension classes. `b38d42016d` decodes escaped
  embedded script text before checking it. Preserve failed receipts at
  `.build/integration-mono-migration` and `.build/integration-native-migration-fixed`.
  Native editor SHA256
  `170a1ca495b1a917c6afdfe0ae8dffa6db9b72f38a5175df2028a6f11a1779ff` passes
  `python misc/scripts/validate_egp_net_migration.py --engine C:/Users/Rose-X/.codex/worktrees/box3d-parity/EGP/bin/godot.windows.editor.dev.x86_64.exe --output .build/integration-native-migration-decoded`:
  25 checks; rejected scripts/scenes unchanged; clean Spatial/onready conversion
  succeeds. Native manifest records the frozen base and both incremental commits.

Next copy committed converter/docs updates into the idle combined Mono tree,
record hashes and incrementally rebuild its editor with the exact SDK API. Repeat
paired capture, migration, combined physics/network and editor-control checks
before authorized master publication. Templates are unaffected by these editor
migration/doc updates. Actual C#/C++ game reload, preserved instances/state/events,
repeat reload recovery and cleanup remain unfinished: C# gates reload on editor
hint and native extension reload is enabled only for editor mode. Implement and
qualify an explicit editor-binary game opt-in; packaged hot reload is a separate gate.

### Final editor refresh and packaged network lab

Frozen canonical engine source `bd4dfee3dc771c79b2f240607bc2aaee5327f850`
was checked file-by-file in the combined Mono tree and built successfully with
FASTBuild; `.build/integration-mono-editor-refresh/source.json` and `receipt.json`
record source hashes, exact command and 267.266 seconds. Editor SHA256
`35502944a136b6c3b21267823f9549c881337d409c40f5209214dff232aded75` passed:

- `python misc/scripts/validate_box2d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-mono-box2d-scenes`:
  all 14 runs and exact one/four-worker trajectory equality.
- `python misc/scripts/validate_box3d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-mono-box3d-scenes`:
  all 28 cases, including native joints, soft body, query ownership and teardown.
- `python misc/scripts/validate_egp_net_migration.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-refreshed-mono-migration`:
  all 25 checks, including untouched rejected embedded scripts.
- `python misc/scripts/validate_egp_cpp_ui.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-refreshed-mono-cpp-ui`:
  passed at `1791268071687824400/receipt.json`.

Paired API capture at `.build/integration-refreshed-mono-api/1791268071688329100`
found one newly registered class, `LightmapperRD`, relative to the preceding API.
Physics/network members are unchanged, but the full API hash differs, so the
matching audit correctly fails with the previous embedded SDK. Do not weaken the
hash check. Session `1999` owns `.build/finish_refreshed_mono.py`: exact new API,
matching SDK/editor, stable-dump confirmation, Mono glue/assemblies, editor with
final glue and both templates. Logs/active PID/receipt are under
`.build/integration-final-mono-matched`. Finish this pipeline before runtime tests.

The network launcher now accepts `--editor` with a template in `--engine`, exports
a real Windows application with that exact custom template, then launches the
adjacent executable/PCK. Optimized templates disable both `--path` and
`--main-pack`; their restrictions remain effective. Exported artifact hashes join
the source and input-engine identities. Admission tokens stay in a temporary
directory outside the exported application.

`python misc/scripts/launch_egp_network_lab.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe --editor C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --mode dedicated --clients 3 --visible --preset wan --duration 10 --reconnect-at 4 --output .build/integration-release-network-export-windows`
passed at `1791268266430889500/receipt.json`: three client windows simultaneously,
dedicated server, 100 ms latency/25 ms jitter/3% loss on both endpoints, replies,
authoritative ticks, reconnect generations and four clean process exits. Exported
EXE SHA256 `58ae23d4377ca0c20f7444ecce2bc8cf04aa9b1f58bbd1b7fe4f3058e6bcad11`, PCK
`e92a5c5ddc08a2fa2f6d8c1c7be479eaec45dcdf2efc740f10e28682f091c687`.
Retain failures in `.build/integration-release-network-windows` (path override)
and `.build/integration-release-network-pack-windows` (main-pack override).

Updated inventory: `.build/integration-takeover/final-inventory-1791267845811272200.json`.
All seven trees and local/remote refs remain accounted for. Origin's historical
Godot release branches are not EGP feature handoffs. Remote C++ feature head
`e4e0090fb8` and final handoff `2624581a26` are canonical ancestors. PR #1 remains
open; remote master is still `f4389f3a76`. Publish after final matching bindings
and combined runtime/export checks pass; then reconcile that PR.

### Qualified consolidation ready for publication

The final matched pipeline finished successfully; no build remains active.
Engine source remains frozen at `bd4dfee3dc771c79b2f240607bc2aaee5327f850`.
Later commits change only the lab launcher and documentation. Final actual editor
SHA256 `5b9c139caff2459b634cdf29407def5f785563008eda9345a767a0d6d7d5ce9d` has API SHA256
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`; both are recorded
in `.build/integration-final-mono-matched/receipt.json`. Debug/Release template
hashes remain `fa230aaaÃ¢â‚¬Â¦fd449` and `759f39a3Ã¢â‚¬Â¦28718` (full hashes above).

- `.build/integration-matched-final-api/1791268787938526900/receipt.json` is the
  final paired capture. Audit `.build/integration-matched-final-api-audit.json`
  passes 77 classes and 371 documented enum constants using the actual extracted
  SDK `%LOCALAPPDATA%/Godot/egp_cpp/sdk/4bc13481314e7023` and freshly compiled C#.
  Use the preceding audit command with these new paths; the earlier SDK hash
  rejection remains preserved.
- `.build/integration-matched-final-cpp-ui/1791268787939027100/receipt.json`
  passes on that exact editor, including cold Debug/Release SDK libraries, shared
  cache reuse, compile-failure recovery, diagnostics, editor reload and shutdown.
- Repeating the full trilingual command with SDK/library directory
  `4bc13481314e7023` and `--output .build/integration-matched-final-net-languages`
  passes all stages, 59 mixed-language runtime assertions, separate GDScript/C#/C++
  clients and relocated Debug/optimized Release exports. This uses isolated fresh
  NuGet packages; global caches with the same development version are not qualified.
- Packaged Debug host plus three clients passes four simultaneous visible windows,
  WAN simulation and reconnect at
  `.build/integration-debug-network-export-windows/1791268787944027300/receipt.json`.
  Command is the Release lab command above with `template_debug`, `--mode host`
  and that output directory. EXE SHA256
  `b002b0a1f4eaa756d46f436f054ae4bb26bf510f8c4957dbfe2d3fb40e04dc50`.
- Verified editor/console wrappers, both Mono templates and matching GodotSharp
  tools/assemblies/packages are now in primary `bin/`. All 86 copied files are
  hashed in `.build/canonical-mono-artifacts.json`; overwritten build outputs
  were preserved in its recorded backup directory. Native non-Mono files were
  not overwritten. `bin/godot.windows.editor.dev.x86_64.mono.console.exe --headless --path .build/integration-matched-final-net-languages/project --max-fps 60`
  passes 59 assertions from the canonical output; evidence is
  `.build/canonical-bin-interop.json`.

Qualified consolidation `8543a431024cd8ef85985e4245b37c79e8083549` was pushed
to origin master without force and its remote SHA verified on 2026-10-06. GitHub
marked PR #1 MERGED at 06:50:52 UTC. The primary checkout stays on
`codex/egp-integration` for loop ownership; local master is advanced to the same
qualified history. Publication evidence is `.build/integration-master-publication.json`.
The intended handoff source is consolidated; acceptance of all features remains
unfinished. Prioritize actual running-game C#/C++ reload and recovery, the 13
undocumented EGPNetSession methods and eight new server methods in each dimension
(see `.build/integration-missing-method-docs.json`, excluding normal property
accessors), native 2D capability/event fixtures, signature/default audits, fresh
development-package cache identity and advanced network/scale/platform gates.

### Live C++/C# game reload and recovery qualification

Engine source is frozen at `981a8ab945` in
`.build/integration-runtime-reload-build/source.json`. Commits `34407e1fac`,
`82b87b73cb` and `981a8ab945` add the runtime opt-in, Debug builds during gameplay,
debugger notifications and idle native reload, feature-override consistency,
unload-failure lifecycle reset and retained managed events across failed loads.
Later fixture/documentation changes do not change engine code.

Actual editor SHA256 is
`da7b45aadf7c25890bcdf59f1ab3e1c187fe8618c1ee26046b186e19012dd1c5`.
The console wrapper SHA256 is
`c68c175161d3aae3056e487bf7eeb2929dacd75f7f17f781d305c9f142761190`.
API SHA256 remains `e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`;
existing SDK/cache `4bc13481314e7023` remains an exact match.

```powershell
$engine = "C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe"
$packages = "C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/GodotSharp/Tools/nupkgs"
python misc/scripts/validate_egp_hot_reload.py --engine $engine --packages $packages --output .build/integration-runtime-reload-unload-recovery --feature-override --assembly-recovery --unload-recovery
python misc/scripts/validate_egp_hot_reload.py --engine $engine --packages $packages --output .build/integration-runtime-reload-default --disable-runtime
python misc/scripts/validate_box2d_scene.py --engine $engine --output .build/integration-runtime-reload-box2d-scenes
python misc/scripts/validate_box3d_scene.py --engine $engine --output .build/integration-runtime-reload-box3d-scenes
```

- `.build/integration-runtime-reload-unload-recovery/1791271539030868900/receipt.json`
  passes five successful managed reloads and two live native Debug rebuilds in a
  separate game through the actual editor debugger. Existing IDs, scalar/vector
  properties, typed node references, parents, cached callables, native signals and
  managed events survive. Ready runs once; serialization hooks run on reload.
  Invalid C++/C# builds retain working code; a no-change command does not reload.
  Corrupted-DLL and a thread blocking assembly unload retain placeholder state
  and recover after repair/release. Deleting a placeholder during failure is
  covered. Unload/load/recovery diagnostics are verified in the debugger panel.
  Managed runtime DLL hashes are checked before and after the fixture.
- Default non-collectible game behavior passes at
  `.build/integration-runtime-reload-default/1791271113974963800/receipt.json`.
  Feature overrides plus failed-load recovery also pass at
  `.build/integration-runtime-reload-feature-override/1791271198987931200/receipt.json`.
- Final paired capture is
  `.build/integration-runtime-reload-recovery-api/1791271103739780900/receipt.json`.
  The previous API audit command with this capture and
  `--output .build/integration-runtime-reload-recovery-api-audit.json` passes
  77 classes and 371 documented enum constants against exact SDK/compiled C#.
- `.build/integration-runtime-reload-net-languages/receipt.json` passes the full
  preceding trilingual command with that output directory: 59 assertions,
  separate clients in all three languages, cold imports and relocated Debug and
  optimized Release exports. Templates retain their prior qualified `bd4dfee3`
  engine identities; current managed export artifacts are requalified with both.
  Runtime reload remains an editor-build capability.
- Box2D passes all 14 runs and exact one/four-worker traces. Box3D passes all 28
  scene cases on this exact editor. Receipts are under the two directories above.
- `.build/canonical-runtime-reload-artifacts.json` verifies 82 editor/managed
  files in primary `bin/`; 12 changed files were backed up before replacement.
  `bin/godot.windows.editor.dev.x86_64.mono.console.exe --headless --path .build/integration-runtime-reload-net-languages/project --max-fps 60`
  passes 59 assertions after installation (exec evidence chunk `8c1e84`).

Preserved failure receipts include the pre-fix non-collectible opt-in baseline
at `.build/integration-runtime-reload-baseline/1791270128778519300`, the live C++
build refusal at `.build/integration-runtime-reload-positive/1791270483624737200`,
lost managed subscription after DLL repair at
`.build/integration-runtime-reload-assembly-recovery/1791270884321767900`, and
the unload fixture's transient Windows command-file sharing failure and initial
stdout-only diagnostic assertion under `.build/integration-runtime-reload-unload-recovery`.
Command publication now retries that sharing violation for at most two seconds;
all build, game and reload watchdogs remain bounded.

Seven-tree/113-ref inventory and current tracked patches/untracked file hashes:
`.build/integration-takeover/runtime-reload-1791270788583469900/inventory.json`.
No open EGP PR was found; foreign handoffs remain preserved. Qualified increment
`3e81a13d04` was pushed to origin master without force on 2026-10-06 and its remote
SHA verified. `.build/integration-runtime-reload-publication.json` records the
final canonical/local/remote identities, source delta and qualification receipts.
The build's displayed Git/version timestamp comes from the frozen worktree base;
the verified source manifest and actual binary hashes identify this increment.
All-feature acceptance remains open: incompatible native ABI/class changes and
invalid-library recovery, wider script/state and reload-soak cases, native 2D
capability events, missing public method/signature/default documentation, unique
development-package identity and canonical build version provenance, and advanced
networking/platform/performance gates.


## Public method documentation acceptance â€” 2026-10-06

The inventory at `.build/integration-takeover/runtime-reload-1791272296539957200/inventory.json`
accounts for seven worktrees, 113 local/remote refs and zero open PRs. Canonical,
local master and origin master started this increment at `487299e664`; original
foreign dirty work remains preserved without overwriting the combined source.
No concurrent engine/build owner was active.

Source documentation now adds 50 missing methods and seven networking signals,
fills 314 empty descriptions (including native ScriptExtension hooks), and fixes
two stale typed-array defaults and retired 3D shape references. New Box2D backend
classes are registered for compiled editor documentation. Native session options,
command result dictionaries, thread/error semantics, payload limits and physics
event/configuration schemas are explicit. Custom server and script-language
virtual methods link to their public contracts or describe native-only hooks.

`.build/integration-method-doc-validation/receipt.json` records exact commands,
changed source hashes, XML schema validation, help-reference lint, Ruff and 15
negative/regression tests. Help lint reports no warnings or errors; two existing
unpaired language code examples remain reported. The source audit at
`.build/integration-method-doc-source-audit.json` passes 77 classes against the
current engine API, exact C++ SDK and compiled generated C# source. Counts are
1299 methods, 55 signals and 371 enum constants. This source check precedes the
editor/managed rebuild; installation and combined runtime acceptance remain
pending until their fresh receipts are recorded. The native invalid-library and
incompatible-ABI reload recovery paths remain unfinished acceptance items.


## Qualified documentation and native recovery increment — 2026-10-06

Canonical commits `b09ff67db4`, `e3c53af097`, `815b7d62b3` and `2d21765d41`
contain the documentation/signature audit, native recovery fix and CLI help-dump
repair. Final frozen engine source is `2d21765d41`; subsequent changes are audit
tooling and receipts/documentation only. The original native recovery failure is
preserved at `.build/integration-native-recovery-baseline/1791273209630507500`:
two failed loads followed by repair reset the counter from 91 to 1. Reload retries
now retain extension properties while refreshing editable native-parent state.
The first compile rejected protected Object access; its archived failure under
`.build/integration-native-recovery-build/previous-*` led to the public class-ID
check. No failed command is treated as qualification.

The CLI dump failure at `.build/integration-native-recovery-api/1791273592241378900`
preserves its access-violation backtrace. API export now loads shipped help before
reading it, and keeps ABI entries for implementation classes that only have
inherited documentation. Final capture at
`.build/integration-native-recovery-api/1791273909942341700/receipt.json` passes
plain API, actual ClassDB and compiled help from the same unchanged editor:

- Source: `.build/integration-compiled-docs-build/source.json` verifies the frozen
  tracked source, including the new backend XML files. Build receipt is adjacent.
- Actual editor SHA256:
  `0c651f7827537f31e7514fb47a9dbc48f2ea310271945bbf31efe7f58929db5f`.
- Console wrapper SHA256:
  `3e98814f670c72c981d0b479d3a5be42963836f5ab93ae99f43ddffccbdfbcc2`.
- Plain API SHA256 remains
  `e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`;
  the exact cached C++ SDK and Debug/Release archives remain compatible.
- Compiled documentation API SHA256:
  `7414596a3aff5cfbdaab756d68bdacd94d71fcfda290fbae56fb8277e5c61367`.
- `.build/integration-native-recovery-api-audit.json` passes 77 classes,
  1299 non-accessor method contracts, 55 public signal contracts, 371 enum
  constants and 1358 source/compiled method/signal descriptions. Explicitly
  documented accessors are also signature-checked. Missing, stale, retired and
  unsafe-exemption fixtures are rejected by 17 regression tests.
- `.build/integration-native-recovery-managed-docs.json` verifies documentation
  for all 50 new methods and seven networking events in compiled Debug and
  Release GodotSharp XML, recording both DLL and XML hashes.

```powershell
python misc/scripts/capture_egp_api.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.console.exe --output .build/integration-native-recovery-api --include-docs
python misc/scripts/validate_egp_hot_reload.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --packages C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/GodotSharp/Tools/nupkgs --output .build/integration-native-managed-recovery-final --feature-override --assembly-recovery --unload-recovery --native-recovery
python -m unittest discover -s misc/scripts -p test_validate_egp_api.py
```

The final live fixture receipt is
`.build/integration-native-managed-recovery-final/1791273909976516500/receipt.json`.
Six successful reloads preserve native/managed IDs, counter/vector/node state,
cached callables, signal/delegate subscriptions and serialization hooks. It
recovers invalid managed assemblies, a managed thread preventing unload, and
missing then invalid native DLLs. The C++ counter returns as 91 and the parent's
name edited during failure remains `RecoveredNative`. Debugger diagnostics and
normal editor/game teardown pass; input binary/runtime hashes remain unchanged.
The default non-collectible game also passes at
`.build/integration-native-recovery-default/1791274130687358700/receipt.json`.

Fresh final-identity Box2D and Box3D receipts are under
`.build/integration-native-recovery-final-box2d-scenes` and
`.build/integration-native-recovery-final-box3d-scenes` (14 and 28 cases; exact
Box2D one/four-worker trace equality). Trilingual networking and exports pass under
`.build/integration-native-recovery-net-languages-final`: 59 mixed assertions,
separate clients in all three languages, cold import and relocated Debug/Release
games using freshly built managed packages.
The initial command used a nonexistent Debug template filename; its failed
receipt remains under `.build/integration-native-recovery-net-languages`.
Templates remain the prior qualified `bd4dfee3` Debug/Release binaries; current
changes affect editor help and editor-build-only reload. Cross-platform,
arbitrary ABI/class changes, reload soak, advanced networking/authoritative
physics, remaining 2D capabilities, unique package identity and canonical build
version provenance remain open. This increment does not establish AAA readiness.


`.build/canonical-native-recovery-artifacts.json` verifies 82 installed editor and
managed files; 16 updated files were backed up first. No build/engine process was
active during installation. The installed canonical editor command
`bin/godot.windows.editor.dev.x86_64.mono.console.exe --headless --path .build/integration-native-recovery-net-languages-final/project --max-fps 60`
passes all 59 trilingual assertions (exec evidence `0bc19f`). Publication follows
these combined-engine gates; the final receipt will record canonical/local/remote
commit identities, exact commands, inventories, installed hashes and limitations.


Qualified increment `e8bfe8dc9e` was pushed to origin master without force and its
remote SHA verified on 2026-10-06. `.build/integration-native-recovery-publication.json`
records the final acknowledged canonical/local/remote identities, frozen source,
combined qualification and installed-artifact hashes. Final seven-tree inventory
is `.build/integration-takeover/runtime-reload-1791274456410516300/inventory.json`:
113 refs, zero pending PRs, all foreign dirty source preserved, and only the two
unrelated user files untracked in primary. The loop remains ACTIVE. The displayed
engine version still comes from the frozen worktree base; verified source and
binary hashes identify this build. Next items include incompatible ABI/class
reload recovery, remaining 2D native event/capability fixtures, broader networking
simulation/ownership/authoritative physics, package identity and build provenance.

## Native Box2D capabilities and runtime query recovery (2026-10-06)

Frozen engine source `4165334987` adds null-parameter guards to both native shape
casts and validates rectangle hulls before calling Box2D. The previously published
editor crashes on null shape-cast parameters and on rectangles below the configured
native tolerance. Original failures and backtraces remain under
`.build/integration-box2d-capabilities-baseline` and
`.build/integration-box2d-capabilities-repro`; they are not passing qualification.
Invalid calls now return empty query results and report actionable diagnostics.
No fallback rectangle or silent geometry substitution is introduced.

The new `native_capabilities_test.gd` checks all seven configured 2D joint families,
distance/weld/filter behavior, linear and angular motor targets, independent joint
configuration dictionaries, finite reaction force/torque, joint-threshold events,
nonbreaking threshold behavior, copied event dictionaries, masked explosions and
native contact-hit geometry. `canvas_cast_test.gd` checks canvas reassignment,
body/area/RID filtering, nearest/all-hit ordering, destinations, exclusions and
empty results. Box2D uses pixels and its default cast tolerance is 0.5 pixels;
the initial test incorrectly reused 3D metre dimensions and was corrected. The
small rectangle crash discovered by that fixture remains a regression case.

The invalid-input runner requires exactly two null-query and four invalid-hull
messages, normal exit and a completion marker. Any extra/missing diagnostic,
script error, native assertion, crash or RID leak fails. Six gate regression tests
also reject completion from the wrong exported fixture. Exported templates disable
path/script overrides: the packaged validator combines unchanged fixture bodies
into a namespaced MainLoop selected through user arguments, with an ordinary main
scene, and records input/generated script hashes. Initial loose-project rejection,
missing-main-scene watchdog and CLI-selection failure are preserved under
`.build/integration-box2d-query-recovery-debug-scenes`, the release counterpart,
`.build/integration-box2d-query-recovery-exports` and
`.build/integration-box2d-query-recovery-exports-final`. Those incomplete runs do
not establish exported capability coverage. Final packaged evidence is
`.build/integration-box2d-query-recovery-export-mainloop/receipt.json`.

Verified frozen-source binaries from the sole combined build tree:

- Editor SHA256 `9e421dff29f73f3248776c571de4d498ed29057fa02298ed76e79195a0ccff91`.
- Debug template SHA256 `6066cdb4fd708a169d8e6ba2a26f938c2959be0b41868390f70ff269eabe9803`.
- Release template SHA256 `9ab45e46141a2506aa1a2f23db5a7b20b6d7ed2e66ac101f1a09fb6845f05af6`.
- Build/source receipts: `.build/integration-box2d-query-recovery-build` and
  `.build/integration-box2d-query-recovery-templates`. Both runtime templates now
  include the frozen source; they supersede the retained `bd4dfee3` templates.
- Actual plain/compiled APIs remain the qualified `e84e140b...` and `7414596a...`
  identities, with unchanged matching C++ SDK and generated managed bindings.
  Paired capture is `.build/integration-box2d-query-recovery-api/1791275263050508600`;
  `.build/integration-box2d-query-recovery-api-audit.json` passes 77 classes,
  1299 method contracts, 55 signals, 371 enums and 1358 compiled descriptions.
- `.build/integration-box2d-query-recovery-scenes/receipt.json` passes 17 runs,
  exact one/four-worker trajectory equality and the six expected diagnostics.
  `.build/integration-box2d-query-recovery-box3d/receipt.json` passes all 28 cases.
- Final packaged physics receipt passes four cases per configuration: backend
  activation, canvas/casts, configured native capabilities and rejected inputs.
  Each case must print its own exact completion message; templates run without
  loose-project overrides, and EXE/PCK hashes are recorded after relocation.
- `.build/integration-box2d-query-recovery-net-languages/receipt.json` passes
  trilingual assertions and separate GDScript/C#/C++ clients, cold import and
  relocated Debug/Release exports using the newly built templates.
- `.build/integration-box2d-query-recovery-hot-reload/1791275468233871200/receipt.json`
  passes six combined native/managed reloads, invalid assembly and blocked-unload
  recovery, missing/invalid native libraries, retained state/events/parent edits,
  debugger diagnostics and normal teardown. Default opt-out passes separately at
  `.build/integration-box2d-query-recovery-default/1791275598885648100/receipt.json`.
- Release dedicated server plus three visible clients passes under
  `.build/integration-box2d-query-recovery-dedicated/1791275598862583300`.
  Debug listen host plus three visible clients passes under
  `.build/integration-box2d-query-recovery-host/1791275742756037100`.
  Both use WAN latency/jitter/loss on both endpoints, ten seconds and reconnect
  at four seconds; all requested windows are observed simultaneously and every
  owned process exits normally. This short fixture is not a soak/performance gate.

```powershell
python misc/scripts/validate_box2d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-box2d-query-recovery-scenes
python misc/scripts/validate_box2d_exports.py --editor C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --debug-template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_debug.x86_64.mono.exe --release-template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe --output .build/integration-box2d-query-recovery-export-mainloop
python -m unittest discover -s misc/scripts -p test_validate_box2d_scene.py
```

Installation and publication receipts are
`.build/canonical-box2d-query-recovery-artifacts.json` and
`.build/integration-box2d-query-recovery-publication.json`. They record verified
canonical/local/remote commit identities, foreign merge ancestry, fresh seven-tree
inventory, unchanged managed artifacts, backups and combined qualification before
publication. All existing foreign source handoffs remain consolidated; unrelated
files and older dirty worktrees remain preserved. Subsequent source changes are
qualification tools and documentation only. The loop remains ACTIVE.

Next: incompatible ABI/class reload recovery, native picking filters, packed-float
convex polygon input validation (the backend conversion still has suspect count/
index handling), wider state/collection/reload-soak cases, server restart and
adversarial networking, ownership/interest scaling, authoritative physics gameplay,
platform/performance qualification, unique managed package identity and reliable
build-version provenance. This increment does not establish full parity or AAA
readiness.

## Packed convex polygon conversion and validation (2026-10-06)

Frozen engine source `72220b5923` repairs Box2D's documented packed polygon format.
The previous converter rejected arrays whose length was divisible by four,
skipped vertex slots, and could return after allocating point storage without
freeing it. The reproduced failure uses the published `4165334987` editor and a
preserved four-point fixture at `.build/integration-convex-input-baseline`:
`receipt.json` records engine/fixture hashes, exact command and exit code 1.
The original failure is retained; no geometry-parity claim comes from that run.

Packed input now consumes each complete `(x, y, normal_x, normal_y)` tuple at the
correct stride. Count is validated before writing a fixed eight-point native
buffer, avoiding partial initialization and temporary heap-array leaks. Both
vector and packed formats validate transformed native coordinates; packed normals
must be finite. Hull validity is checked before constructing a native polygon.
Malformed tuples, invalid counts/types, nonfinite values, coordinates beyond
native bounds, collinear/tiny geometry and singular transforms report errors and
produce empty query results. Supplied data stays stored; conversion occurs when
attached or queried. Box2D recomputes normals and can remove duplicate/interior
points. Public XML and compiled C# help explain the contract and native limits.
This does not establish broad allocation/memory or arbitrary-scale qualification.

The new positive fixture compares transformed ray hits, normals and cast travel
for three-, four- and eight-point vector and packed inputs. The negative fixture
requires exactly twelve diagnostics, empty results and a successful cast after
repair. Diagnostic allowances belong only to that named fixture; extra/missing
errors, assertions, crashes, script failures and RID leaks still fail the gate.
Eight runner regression tests and seventeen API regression tests pass. The
packaged MainLoop contains these unchanged fixture bodies and requires a specific
completion message for every selected case.

Qualified source/binary identities:

- Build/source receipts: `.build/integration-convex-input-build` (editor, shipped
  help, regenerated managed glue and rebuilt Debug/Release assemblies/packages)
  and `.build/integration-convex-input-templates` (both runtime configurations).
- Editor SHA256 `1f02f4a50b90eb5bbcae44b13d448f1126655e7709755dc719a6a9bae9f541f1`.
- Debug template SHA256 `b7c90e63ff29e94146845fd4e672f9e93e2181cc68e850f661c73d6bd94b7155`.
- Release template SHA256 `c6f1f5c0114f7d834e4e846f8cbbbf005abd0c7e6d445907034d3449b549773e`.
- Paired API/ClassDB/shipped-help capture:
  `.build/integration-convex-input-api/1791276680330441500/receipt.json`.
  Plain API SHA remains `e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`,
  retaining exact C++ SDK compatibility. Updated compiled documentation API SHA:
  `0f53af746a134aca00786612f4959649fe0ea4ee8f1eead9bafd42d4754b77c3`.
- `.build/integration-convex-input-api-audit.json` passes 77 classes, 1299 method
  contracts, 55 signals, 371 enums and 1358 source/compiled descriptions.
- `.build/integration-convex-input-managed-docs.json` records the existing 57
  documented members plus the revised ShapeSetData contract in compiled Debug and
  Release XML, DLL/description hashes and the revised source identity.
- `.build/integration-convex-input-doc-lint.json`: XML schema validation and full
  reference-link/RST lint pass. Two pre-existing unpaired language examples remain.
  `.build/integration-convex-input-unit-gates.json` records 8 + 17 passing tests.
- `.build/integration-convex-input-scenes/receipt.json`: all 19 Box2D runs pass,
  including exact one/four-worker trajectory equality, the six prior invalid-query
  diagnostics and twelve convex-input diagnostics. All 28 Box3D cases pass in
  `.build/integration-convex-input-box3d/receipt.json`.
- `.build/integration-convex-input-exports/receipt.json`: six focused cases pass
  in each relocated Debug/Release game (12 total), with EXE/PCK and fixture hashes.
- `.build/integration-convex-input-net-languages/receipt.json`: 59 trilingual
  assertions, separate GDScript/C#/C++ clients, fresh import, matched Debug/Release
  extensions and relocated exports pass with rebuilt managed packages.
- Six successful combined reloads and all recovery paths pass at
  `.build/integration-convex-input-hot-reload/1791276730404842700/receipt.json`.
  Native/managed state, identity, events, cached calls and edited native-parent
  state survive failed loads and recovery; debugger diagnostics and teardown pass.
  Default non-collectible behavior passes separately at
  `.build/integration-convex-input-default/1791276730405345800/receipt.json`.
- Release dedicated server plus three visible clients passes at
  `.build/integration-convex-input-dedicated/1791276887607064300/receipt.json`;
  Debug listen host plus three visible clients passes at
  `.build/integration-convex-input-host/1791276887606060400/receipt.json`.
  Both apply latency 100 ms, jitter 25 ms and loss 3% on both endpoints, reconnect
  at four seconds during a ten-second run, observe all windows simultaneously
  (three/four respectively), and terminate all owned processes normally.

```powershell
python misc/scripts/validate_box2d_scene.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --output .build/integration-convex-input-scenes
python misc/scripts/validate_box2d_exports.py --editor C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --debug-template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_debug.x86_64.mono.exe --release-template C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe --output .build/integration-convex-input-exports
python -m unittest discover -s misc/scripts -p test_validate_box2d_scene.py
```

All four handoff chats were checked idle/completed; no useful build was duplicated.
The fresh seven-tree inventory and merge ancestors are recorded by
`.build/integration-convex-input-publication.json`. Installation/backups and all
combined-engine gates are recorded by `.build/canonical-convex-input-artifacts.json`.
Publication follows validation; canonical/local/remote source identities and the
installed-file hashes are verified. Older worktree patches, source snapshots and
unrelated user files remain preserved. Changes after the frozen engine source are
documentation only. The loop remains ACTIVE.

Next concrete item: incompatible native method/class/base changes and restart or
repair recovery in the live C++ reload fixture. Native picking, broader shape/
scaling and double-precision behavior, wider C# state/collection/reload-soak cases,
server restart/adversarial networking, ownership/interest scaling, authoritative
physics gameplay, platform/performance and package/build-version identity remain
open. Limited Windows fixtures do not establish full integration, parity or AAA
readiness.


### Native signature and rejected hierarchy reload recovery

Frozen combined source: `184998304308421542889bd1c4fa2a8c0542dc0a`. The Mono editor hash is
`97ee09fdefe87575195e60ed7e058ba6dc360bde245976f24b73218dad3a2e61`;
Debug/Release template hashes are
`6485d101f6a826bc25d797199ae6c223634881f002820158dfa26c1d4c02d641` and
`bf9c7a9f535859470c73ba06cf5ea8d8cfa24958f54d572113ce003ce24b92c9`.
The plain API remains `e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`
with SDK `4bc13481314e7023`; the refreshed compiled help is `cc71bebdbfda7f440cfec2c52cb7bd510c0fa796969a20e9bd25d9a89977a883`.
Exact commands, all tracked-source hashes and build stages are in
`.build/integration-native-abi-build`, `.build/integration-native-abi-templates`,
and the preserved `.build/integration-native-abi-documentation-build`.

The baseline at `.build/integration-native-abi-baseline/1791277458347572900`
rejected a native-base change but could not restore the original live class.
The next candidate preserved game state but failed the diagnostic gate because
an editor without live objects discarded its rejected registration record:
`.build/integration-native-abi-hot-reload/1791278033273734700`. Both failures and
their logs remain preserved. The final fix retains rejected/removed class records
and pending instance state, skips already-cleared objects/ClassDB entries on
retry, and returns `LOAD_STATUS_NEEDS_RESTART` while any class is unavailable.
Changed method signatures warn that raw cached bindings are invalid. Changed
extension parents cannot bypass the hierarchy check; descendants cannot reload
until their parent has been restored.

The live fixture now has `--native-abi-recovery`. Its exact invocation used the
matched build-tree Mono editor and `bin/GodotSharp/Tools/nupkgs`, with
`--feature-override --assembly-recovery --unload-recovery --native-recovery
--native-abi-recovery`. `.build/integration-native-abi-hot-reload-final/1791278918055712400/receipt.json` verifies eleven native rebuilds: changed
argument count and return type through dynamic methods/cached Callable lookup;
rejected native-base, extension-parent and ancestor changes; live class removal;
and compatible repairs. Forced retries return NEEDS_RESTART, and a compatible
explicit retry returns OK. Object IDs, native counter 91, managed counter 87,
parent edits, signal/delegate subscriptions and serialization state survive.
Normal editor/game teardown passes without invalid unregister, double-clear,
invalid cached-call or script-error diagnostics. Expected fault diagnostics are
retained. The default non-collectible player also passes at `.build/integration-native-abi-default/1791278923279823400/receipt.json`.

The exposed `GDExtensionManager.reload_extension` help now describes runtime
opt-in, partial reloads, retained state and restart/repair behavior. Compiled
native help matches the source exactly; Debug and Release managed XML contain
the same recovery guidance. Paired API/ClassDB/SDK/C# and 1,358 compiled EGP
description checks, 19 Box2D cases including one/four-worker trajectories,
28 Box3D cases, 12 packaged Box2D cases, and 59 mixed-language interoperability
checks pass on the final engine. Documentation RST and XML-schema checks pass.
The current qualification and artifact receipts record final hashes and preserve
prior failed attempts; an evidence helper's initial UTF-8 decoding failure is
recorded separately from engine failures.

Merge provenance: all four original handoff chats remain idle/completed, the
original feature ancestors remain in the canonical branch, and the fresh
seven-worktree inventory preserves tracked patches/untracked hashes. Installation
and remote/local canonical publication must be verified in
`.build/canonical-native-abi-artifacts.json` and
`.build/integration-native-abi-publication.json` before reporting this increment
published. These checks do not qualify raw cached MethodBind pointers, arbitrary
ABI/layout/schema changes, wider inheritance graphs, cross-platform reload or soak.

Next concrete acceptance item: add a bounded dedicated-server restart mode to
the networking lab and verify server PID replacement, client disconnect/reconnect,
and resumed authoritative ownership/state with visible clients under impairment.
Wider C# collection/resource/static-state reload, native picking and shape/scaling/
double-precision, adversarial networking, ownership/interest scaling, authoritative
physics gameplay, performance and package/build-version identity remain open.
The loop remains ACTIVE; limited Windows fixtures do not establish AAA readiness.

### Dedicated-server replacement and native state pressure

Frozen combined source: `59b8f9bbd44df382aaa64c9102a734b3890649cc`.
The rebuilt Mono editor hash is
`10f5b5d247559d3446dc063e50884f3fa244af0c8ef03014d5c5b1c540628a24`;
Debug/Release template hashes are
`dab82b9b62ed43d9037bdf27a5b7554939f97f31c02e1b3ccad5bec8a1ca6407` and
`402d4842bb5016c3b8f99c4f7df54cb19f6e76337ebe05602315902945914e31`.
`.build/integration-server-restart-native-build/source.json` freezes tracked-source
hashes; its receipt contains the exact editor/Debug/Release build commands. The
public API and SDK fingerprints remain unchanged from the preceding increment.

The new launcher options are `--server-restart-at`, `--server-restart-mode
graceful|abrupt` and `--server-down-for`. Replacement keeps original client
processes running and reuses the server endpoint with a new owned PID. Clients
must observe transport disconnection, clear their old entities, obtain fresh
server admission, reconnect and send owner-authorized inputs in each generation.
The fixture restores an explicit application counter checkpoint and checks exact
restoration plus advancement from the replacement server's inputs. It does not
implement automatic engine/world persistence or production authentication.

Initial exported Release evidence at
`.build/integration-server-restart-abrupt/1791280118487943800` exposed a Windows
file replacement/read race. Readiness and periodic health publications now use
immutable filenames. Later failed evidence at `1791280370230964500` and
`.build/integration-server-restart-diagnosis/1791280448500797100` then exposed a
native reliable-state backlog: clients had hello replies but no owned entities,
and the replicated root tick remained far behind the running server. All failed
logs and receipts are retained.

Native replication now retains at most one queued state for each entity/peer,
using the reliable message's lifetime to wait for acknowledgment/reset. Further
updates coalesce into the latest revision, allowing late owned entities into the
bounded queue. Client server-tick statistics now include accepted entity ticks;
previously the code sampled an empty local entity before decoding the message.
Wire format and exposed API signatures are unchanged. Intermediate states may
be skipped; individual gameplay events belong in application messages.

The deterministic native pressure regression updates at 60 Hz for three seconds,
then requires a late owned entity and the final coalesced state within 2.5 seconds
under 100 ms outgoing latency on both endpoints. It also checks tick statistics,
interest reentry and despawn with a state pending. The exact preceding committed
core fails that late-ownership gate at
`.build/integration-net-state-pressure-baseline/receipt.json`. Debug and Release
native suites pass all six CTest cases and 101 native checks at
`.build/integration-net-state-pressure-debug` and
`.build/integration-net-state-pressure-release`. Current-source combined native,
GDScript, secure admission/quarantine, lifecycle, separate-process, Box3D clock
and 29 prediction/replay checks pass at
`.build/integration-server-restart-native-session/receipt.json`.

The final lab commands use `misc/scripts/launch_egp_network_lab.py` and matching
binaries from `C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin`:

- Release template plus matching editor: `--mode dedicated --clients 3 --visible
  --preset wan --duration 20 --server-restart-at 6 --server-restart-mode abrupt
  --output .build/integration-server-restart-release-final`; receipt
  `1791280934461872600/receipt.json` passes, server PID 14576 becomes 21712.
- Debug template plus matching editor: `--mode dedicated --clients 3 --visible
  --preset wan --duration 20 --server-restart-at 6
  --output .build/integration-server-restart-debug-final`; receipt
  `1791281037361877300/receipt.json` passes, server PID 8832 becomes 8796.
- Mono editor: `--mode dedicated --clients 3 --preset wan --duration 25
  --server-restart-at 6 --server-down-for 8 --server-restart-mode abrupt
  --output .build/integration-server-restart-long-outage`; receipt
  `1791281037360875500/receipt.json` passes, server PID 3304 becomes 9868.

Each final test restores counter 106 and reaches 112, verifies both generations'
input acknowledgments and replicated server PIDs, and retains original client
PIDs. Packaged cases observe all three client windows together. Simulation is
100 ms latency, 25 ms jitter and 3% loss on both endpoints. Launcher evidence
validation has twelve regressions rejecting incomplete/stale ownership, identity,
checkpoint, disconnect and endpoint evidence; the command, hashes and results
are in `.build/integration-server-restart-units/receipt.json`.

`.build/integration-server-restart-native-qualification/receipt.json` passes the
paired API/SDK/C#/compiled-doc audit, managed documentation, 19 Box2D runs,
28 Box3D cases, 12 packaged Box2D checks, 59 mixed-language checks and exports,
native signature/hierarchy/class-removal recovery, C# hot reload and default
non-collectible runtime. Fresh Debug listen-host and Release dedicated tests also
pass visible clients and manual reconnect. Exact commands and individual logs
are recorded in that qualification receipt. Matching artifacts must be installed
and canonical/local/remote publication verified in
`.build/canonical-server-restart-artifacts.json` and
`.build/integration-server-restart-publication.json` before reporting publication.

All four original handoff chats remain idle/completed; their final snapshots are
retained at `.build/integration-server-restart-handoffs.json`. The fresh inventory
continues to account for seven trees, feature ancestors and historical dirty
sources without overwriting current integration with stale qualification trees.
Unrelated `Temp Plan.md` and `tagged_query_test.b3rec` remain preserved.

Next concrete networking acceptance: measure ownership/interest fairness and
bounded queues/memory with more entities and peers under state pressure, repeated
resets and adverse conditions. Raw cached native MethodBind pointers, arbitrary
ABI/layout/schema changes, wider C# state, physics shape/scaling/platform parity,
production persistence/authentication, full authoritative gameplay, performance
and package/build-version identity remain open. The loop remains ACTIVE; these
bounded Windows fixtures do not establish full feature completion or AAA readiness.

### Rate-budget fairness, baseline completion and queued-state memory

Engine/library source is frozen at `20f0400aacca6d92bc70cd0247d3ffc168056095`.
The later commit `7e856b28de85f9f9df587d5a9ff80b626009de57` changes only the
standalone memory fixture's measurement checkpoints; that test is not compiled
into the engine. Exact source hashes and editor/Debug/Release build commands are
in `.build/integration-fairness-native-build/source.json` and `receipt.json`.
Mono editor SHA-256 is
`ed2f6d3801b86d4696afef366fc3bd244261e9bbab70b0de6729c6ad6b2c0087`;
Debug and Release template SHA-256 values are
`1ef926ee8dadd67215d61f76aa1b713adbd710e1905f82aa269542dbbf21f1ee` and
`05e12b4547d0026644cfbd8519c208576463e04897b4f9817b1e9770cabdda3a`.
Public API, wire format and SDK fingerprints remain unchanged.

A new native regression exposes two encrypted clients to 64 owned entities that
change at 60 Hz, with the server's queue-admission budget set to 32 messages and
8192 estimated payload/header bytes per second per peer. This does not measure
or cap actual retransmitted wire bandwidth. Clients use an independent 1000-message receive budget;
this case uses zero simulated latency/loss. The preceding published source fails
its five-second baseline gate at `.build/integration-fairness-baseline/receipt.json`
and `ctest.log`; the six preceding tests pass. Changing lower-ID entities consume
each new budget window and later entities starve. The baseline also incorrectly
waits for every entity's current revision while the world keeps changing.

Each peer now resumes replication after its last queued entity when its queue or
rate budget fills. The initial ordered baseline completes once each visible
entity has had a state queued; its completion marker gets priority over subsequent
updates at the next available budget window. Reliable ordering still places that
marker after the initial states. This is a membership baseline rather than an
atomic whole-world physics snapshot; complete predicted/rollback state remains a
game contract.

The new regression verifies both baselines, advancement of every entity, both
interest hide/reentry cycles, ownership revocation reaching the other client,
two fresh encrypted reconnects, distinct transport peer generations, no inherited
revoked ownership and cleanup. It passes in roughly 20 seconds per configuration.
The memory fixture now covers 1024 never-published hidden entities and 64 visible
spawn/update/despawn cycles with real queued state and acknowledgment lifetimes.
An initial combined Debug run failed with a negative allocation delta because
its starting count included unacknowledged baseline Debug bookkeeping; the failed
receipt/log remain at `.build/integration-fairness-native-session`. Measurement
now starts after empty-world handshake drainage and requires the exact initial
allocation count within a bounded final ACK drain. Both configurations report
`EGP_INTEREST_RETAINED_ALLOCATIONS=0`; exact commands, executable/fixture hashes
and outputs are in `.build/integration-fairness-memory/receipt.json`. This measures
retained allocations through C++ new/delete, not total RSS, allocator peak usage
or Yojimbo's separately allocated TLSF buffers.

Final commands:

- `python misc/scripts/validate_egp_net.py --configuration Debug --engine
  C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
  --output .build/integration-fairness-native-session-final` passes seven CTest
  cases, 101 native checks, GDScript ownership/scene/raw-channel cases, secure
  quarantine, lifecycle, separate processes, Box3D fixed-clock state and 29
  prediction/correction/replay checks.
- `python misc/scripts/validate_egp_net.py --configuration Release
  --output .build/integration-fairness-release-final` passes seven CTest cases
  and 101 native checks. Earlier Debug/Release attempts are retained separately.
- The matching Release template and Mono editor, with
  `misc/scripts/launch_egp_network_lab.py --mode dedicated --clients 3 --visible
  --preset wan --duration 20 --server-restart-at 6 --server-restart-mode abrupt
  --output .build/integration-fairness-restart`, pass at
  `1791282354489981100/receipt.json`: server PID 13564 becomes 584, original client
  PIDs persist, counter 106 is restored and reaches 112, fresh ownership inputs
  and both replicated server identities are checked. WAN simulation remains
  100 ms latency, 25 ms jitter and 3% loss on both endpoints.

`.build/integration-fairness-native-qualification/receipt.json` passes the fresh
paired native API/ClassDB/SDK/C#/compiled-doc audit, managed documentation,
19 Box2D runs, 28 Box3D cases, twelve packaged Box2D checks, 59 mixed-language
checks and exports, native signature/hierarchy/class recovery, C# reload and
default runtime, plus Debug listen-host/Release dedicated visible reconnect runs.
Exact commands and individual logs are retained. Verified installation and
canonical/local/remote publication belong in
`.build/canonical-fairness-artifacts.json` and
`.build/integration-fairness-publication.json` before this increment is reported
published. All four handoff chats remain idle/completed; the snapshots are in
`.build/integration-fairness-handoffs.json`. The seven-tree provenance inventory
preserves historical dirty sources and unrelated user files.

Next networking acceptance: incoming rate-window boundaries, larger payloads and
more peers under latency/loss, repeated server resets and peak queue/allocator/
bandwidth measurements. This two-client native fairness fixture does not qualify
large WAN worlds or production scale. Raw cached native bindings, arbitrary ABI
changes, wider managed state, physics shape/scaling/platform parity, full
production persistence/authentication/gameplay, performance and package/build
identity remain open. The loop remains ACTIVE.


## Receive budgets and fragmented WAN replication, 2026-10-06

Source increment `56b38cf0b8` fixes false receive-rate rejection. The published
`a2f0509e55` core failure is preserved in
`.build/integration-receive-jitter-baseline/receipt.json`, `run.log` and the exact
baseline fixture copy. With matching 32-message/8192-estimated-byte budgets,
outgoing simulation at 1000 ms latency/600 ms jitter and zero loss, 64 messages
were legally admitted; 32 were delivered and 32 rejected as abuse, disconnecting
the client. A separate matching-budget loopback fairness run passes; that alone
therefore did not cover delayed arrival.

Incoming and outgoing budget windows are independent. Valid incoming envelopes
wait for delivery quota in bounded queues. The wrapper retains at most one
copied envelope per channel; remaining messages stay in Yojimbo's bounded queues.
Channel delivery advances in rounds, retaining ordered-channel order and avoiding
application/raw starvation behind replication. State byte charges match outgoing
estimates (payload+64); other envelopes use payload+32. Receive counters exclude
valid pending copies, which stop/disconnect clears. Malformed or unauthorized
traffic still rejects a connection, and already-rejected server peers receive
no further deliveries/replication before deferred disconnect completes.

This bounds application delivery and wrapper buffering, not wire/retransmission
bandwidth or every transport decoding operation. Sustained valid excess traffic
can exhaust transport queues/TLSF allocation and disconnect. Games still need
per-action input validation and abuse policies. Native option comments, README,
compiled help and regenerated C# documentation describe these limits.

New gates cover 128 delayed zero-loss messages, 128 reliable messages across all
four raw channels at 32 deliveries/second, 11 fragmented 4096-byte raw/application
messages at 8192 estimated bytes/second, order/payload/counters, and an authenticated
peer sending forged server-only replication metadata followed by a healthy peer
joining the same server. The first combined Debug attempt is preserved at
`.build/integration-receive-native-debug`: its adversarial fixture used upstream's
256-message packet encoding instead of EGP's 32-message encoding, disconnecting
at transport decode before the intended envelope rejection. The corrected
matching-encoding fixture passes focused rejection and recovery.

The eight-client fixture checks eight changing 4096-byte owned states at 60 Hz
under 100 ms outgoing latency, 25 ms jitter and 3% loss on both endpoints: all
baselines, every entity's advancement and full-state integrity on all clients,
final coalesced convergence, interest removal/reentry, revocation reaching all
seven remaining clients and a fresh reconnect without inherited ownership. Its
focused run passes. This stochastic, short single-process run does not establish
production scale, peak memory, atomic physics snapshots or gameplay prediction.

The matching build and fresh native Debug/Release, API, physics, hot reload,
relocated exports and visible lab gates belong under `.build/integration-receive-*`.
Installation/publication must pass `.build/canonical-receive-artifacts.json` and
`.build/integration-receive-publication.json` before this increment is reported
published. Seven-tree provenance preserves historical dirty trees, original
handoffs, prior evidence and unrelated user files. The loop remains ACTIVE.


The complete current increment passes:

- `python misc/scripts/validate_egp_net.py --configuration Debug --output
  .build/integration-receive-debug-source`: ten CTest cases and 101 native checks.
- `python misc/scripts/validate_egp_net.py --configuration Release --output
  .build/integration-receive-release-final`: ten CTest cases and 101 native checks.
- `python misc/scripts/validate_egp_net.py --configuration Debug --engine
  C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
  --output .build/integration-receive-native-session-final`: ten CTest cases,
  101 native checks, 24 GDScript cases, eight raw-channel cases, encrypted token
  quarantine, lifecycle, separate processes, Box3D clock and 29 prediction checks.
- `.build/integration-receive-native-qualification/receipt.json`: fresh paired
  API/ClassDB/SDK/managed and compiled-help checks (77 classes, 1299 methods,
  55 signals, 371 enum constants and 1358 descriptions), matching new native/
  generated C# receive-budget help, 19 Box2D runs, 28 Box3D cases, twelve packaged
  Box2D checks, 59 trilingual checks plus relocated Debug/Release process cases,
  C++ signature/hierarchy/class/DLL recovery, C# reload and default runtime.
  Packaged Debug listen host (four simultaneous visible windows) and Release
  dedicated server (three visible clients) both pass WAN manual reconnect.
- Release lab command with matching editor: `python
  misc/scripts/launch_egp_network_lab.py --engine
  C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe
  --editor C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
  --mode dedicated --clients 3 --visible --preset wan --duration 20
  --server-restart-at 6 --server-restart-mode abrupt --output
  .build/integration-receive-restart` passes at
  `1791284713741972100/receipt.json`: server PID 27840 becomes 19604 while client
  PIDs persist, checkpoint counter 106 is restored and reaches 112, caches clear,
  fresh ownership inputs and both replicated server identities are checked.

Frozen engine/native test source is `56b38cf0b8e1cbf020121a1565039efb63300157`.
The editor SHA256 is
`b0286e2b389c3480ae6d37686c567d30157577abb3381a28c11d5d3e8678defc`;
Debug template is
`df14cf19dca8bc69b766b5c3c9bb2acf3edf86b4aa80665de8eb471d14ac65c9`;
Release template is
`dd837bd15c5a6fd969505914a2ab3f95b84dca41c1f3aea2f6144b34ba5f8df4`.
The plain API fingerprint remains
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`.
The display/NuGet development version is still the historical checkout identity;
these source and binary receipts provide the qualification identity.

`.build/canonical-receive-artifacts.json` verifies 86 installed engine/managed
files against eighteen passed receipts; twenty files changed, including freshly
regenerated managed documentation/assemblies/packages, with backups retained.
`.build/integration-receive-canonical-runtime/receipt.json` then runs the installed
root-bin engine and passes all 59 trilingual checks. Final canonical/local/remote
heads, merge ancestry, seven-tree inventory and preserved unrelated files belong
in `.build/integration-receive-publication.json`. All four original handoff chats
are idle/completed, recorded in `.build/integration-receive-handoffs.json`.

Next concrete C++ gate: isolate cached native MethodBind call/validated_call/
ptrcall after missing/invalid DLL reload, then verify compatible repair and class
removal/signature rejection. Source inspection shows prepare_reload marks
is_reloading, while the call guards/is_valid currently test valid; failed library
open returns before finish_reload. The passing dynamic lookup/Callable recovery
fixtures do not qualify that raw cached-binding path. Reproduce it in a supervised
child process before changing guards or claiming safety. Larger WAN worlds,
transport decode/allocator peaks, sustained queue excess, soak, wider C# state,
physics shape/scaling/platform parity, production persistence/authentication/
gameplay, lean server, package/default template identity and performance remain
open. This increment does not establish complete AAA readiness; the loop stays
ACTIVE.


## Cached native binding recovery, 2026-10-06

The published preceding engine crashed with Windows access violation `0xC0000005`
when an independent native observer called its cached target MethodBind after a
missing-DLL reload. Exact PID, pre-call checkpoint, executable/fixture/DLL hashes,
commands and failed logs remain at
`.build/integration-cached-binding-baseline/1791285657971754300/receipt.json`.
The observer DLL stays loaded while the victim DLL reloads, so target instance
binding cleanup cannot invalidate or refresh the observer's raw cache.

Editor-build MethodBind calls now reject unavailable/reloading libraries and
unavailable extension instances. Regular calls return NIL with invalid-method
error; validated calls and ptrcalls initialize declared builtin defaults, including
clearing initialized string/collection storage. Compatible retries reuse temporary
bindings; signature/class retirement keeps old bindings invalid until callers
refresh their cache. State is captured before all of the library's methods are
blocked, allowing property getters to save live extension state before teardown.
The first guarded attempt exposed blocked state-capture getters, retained at
`.build/integration-cached-raw-bindings/1791286662788095600/receipt.json`.
A subsequent fixture correction distinguishes typed null Objects from untyped NIL
in Array equality; its failed receipt is retained at
`.build/integration-cached-raw-bindings-final/1791286837282717400/receipt.json`.

The final fixture passes 250 assertions in six isolated supervised processes:
raw call and ptrcall instance/static paths, plus typed GDScript validated
instance/static paths. Integer returns cover all six; String, Vector3, Array,
Object, PackedByteArray, Variant, bool and float instance returns exercise builtin
defaults and repair, including nonempty ptrcall storage. Missing/invalid DLL retry
preserves ObjectID, counter 91 and native-parent edits. Signature retirement is
checked in all six cases; class removal/cache refresh is checked in the four raw
cases. This is Windows Debug SDK evidence, not arbitrary ABI/layout, concurrent
reload, every builtin/ref-counted return or platform/soak qualification.

Exact cached-binding command:

```powershell
python misc/scripts/validate_egp_cached_bindings.py --engine C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe --sdk C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/sdk/4bc13481314e7023 --sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib --output .build/integration-cached-raw-bindings-qualified
```

The passed receipt is
`.build/integration-cached-raw-bindings-qualified/1791286936639935800/receipt.json`.
Frozen compiled engine source is `ec4d846b72224f75e78e12eb8225615719f21292`;
`3c2cbb46f2` subsequently corrects only the GDScript typed-null assertion. No engine
code changes after the engine freeze. Build inputs and exact commands are in
`.build/integration-cached-native-build-final/{source,receipt}.json`.

Fresh combined qualification passes at
`.build/integration-cached-native-qualification/receipt.json`: API/ClassDB/exact SDK
and managed contract audit, matching compiled reload help and regenerated C#
documentation, 19 Box2D runs, 28 Box3D cases, twelve relocated Box2D export checks,
59 trilingual checks and separate Debug/Release language processes, C++ and C#
reload/recovery and default runtime, packaged Debug host/four visible windows and
Release dedicated server/three visible clients with WAN manual reconnect.
The API fingerprint remains
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`.

`.build/qualify_cached_session.py --configuration Debug --engine
C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
--output .build/integration-cached-native-session-final` reruns the matching-engine
24 GDScript/eight raw-channel cases, Box3D clock, secure admission/quarantine,
29 prediction checks, lifecycle and separate processes. The unchanged networking
native Debug/Release ten-case CTest/101-check receipts are reused only after source
hash verification; these native suites are not claimed as freshly rerun.

The sequential packaged Release abrupt-server-restart repeat passes at
`.build/integration-cached-restart-final/1791287134201767100/receipt.json`: server
4596 becomes 2792, all three visible client PIDs persist, explicit checkpoint 106
is restored and reaches 112, caches clear and fresh owner inputs/server identities
are verified. Command: `python misc/scripts/launch_egp_network_lab.py --engine
C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.template_release.x86_64.mono.exe
--editor C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
--mode dedicated --clients 3 --visible --preset wan --simulate-on both --duration 20
--server-restart-at 6 --server-restart-mode abrupt --port 47153 --output
.build/integration-cached-restart-final`. The same settings failed during heavier
concurrent qualification at
`.build/integration-cached-restart/1791287000608831300/receipt.json`, including a
client fixed-clock catch-up rejection. That failure is preserved; the successful
repeat does not establish recovery from scheduling stalls or sustained overload.
No catch-up budget, impairment settings or watchdog was increased.

The editor SHA256 is
`1ee3cc12ec3ee455269e9877f676c657fff29c5506a253b9977b03afdbd0823d`;
Debug template is
`92402882453cdf2b2e9886e02c4d6b3e762a59645ac6050b20a64b5a0a890f68`;
Release template is
`07bc66869f32811cb3913553eab9f60ff371500bf67fb3833fd4de89ea4a93a6`.
`.build/canonical-cached-artifacts.json` verifies 86 installed engine/managed files
against nineteen passed receipts; twenty files changed, with backups retained.
`.build/integration-cached-canonical-runtime/receipt.json` passes all 59 trilingual
checks again on installed root-bin binaries. Publication/provenance is verified
separately by `.build/integration-cached-publication.json`: canonical/local/remote
heads, original handoff ancestry, all seven trees/113 refs, no pending PR, preserved
historical/unrelated work, idle handoff chats and the engine/fixture-only source
delta. Current chat snapshots are `.build/integration-cached-handoffs.json`.

Next concrete networking gate: inject a scheduling stall that actually exceeds the
fixed-clock catch-up budget, then verify explicit disconnect/resynchronization and
fresh admission/ownership/state recovery. Reproduce and preserve the failure before
changing policies; do not increase budgets or weaken watchdogs to make it pass.
Other builtin/ref-counted/ABI/concurrent reload coverage, larger worlds, peak memory,
soak, platform/physics parity, wider C# state, lean-server/default-package identity,
production persistence/authentication/gameplay and performance remain open.
The loop stays ACTIVE; full feature completion and AAA readiness are not claimed.

## Client clock startup and explicit stall recovery receipt (2026-10-06)

The previously recorded scheduling-stall gate is now covered for one selected
client per run. `88988e5bd0` adds opt-in lab fault/recovery controls and public
poll documentation; `309b569a6d` fixes native client clock startup and adds a
real encrypted UDP regression. All compiled inputs are frozen at `309b569a6d5ed228c9f6520fae2551ec1a6ed672`.

Two independent failures were reproduced and retained before repair:

- `.build/integration-stall-baseline/evidence/1791287542193769100/receipt.json`
  uses the preceding published editor and original failure policy with a 750 ms
  connected-client gap. The native catch-up rejection stops the client and clears
  its clock/cache; the original lab exits instead of recovering.
- `.build/integration-stall-stall-dedicated/1791288802999029600/receipt.json`
  catches a different startup failure before intentional injection: a client
  synchronizing at tick zero rejects the preceding delayed handshake poll.
  `.build/integration-stall-startup-baseline/receipt.json` isolates this against
  the old native core with delayed encrypted handshake polling: build passes and
  execution exits 1 at the handshake-clock assertion. Exact old sources and
  executable/library hashes are retained.

The first admitted client poll now starts a fresh simulation clock; transport
handshake time is not simulation debt. Subsequent connected poll gaps retain the
eight-tick-per-poll and 0.5-second accumulated-time limits. The native regression
also verifies that a 750 ms connected gap still returns `Failed`, emits one
diagnostic, stops, clears entities and resets ticks. Neither budget nor watchdog
was increased.

The lab waits for an authenticated reply and owner-input acknowledgment before
delaying the chosen child poll. It independently verifies failure/clear/reset,
then explicitly closes/reconfigures the facade after polling returns and requests
fresh admission from the trusted local test backend. The unchanged server revokes
old ownership, removes the old entity and creates a new peer-owned entity. A stale
old-entity command must not reach gameplay; the new owner command is acknowledged
once. Original client PIDs persist, healthy clients retain their first connection,
and server counter/tick progress continue. This is an application recovery example
with fresh identities, not automatic reconnect or persistence.

Final recovery evidence uses the new matching editor/templates, WAN impairment
(100 ms latency, 25 ms jitter, 3% loss) on both endpoints and a 20-second run:

| Run | Injection | Process/window evidence | Receipt |
| --- | --- | --- | --- |
| Mono editor dedicated server, two headless clients | Client 0, 5000 ms at 4 s | 3 original processes; 0 simultaneous visible windows | `.build/integration-stall-final-editor-long/1791289641457290600/receipt.json` |
| Packaged Debug listen host, three visible clients | Client 1, 1000 ms at 4 s | 4 original processes; 4 simultaneous visible windows | `.build/integration-stall-final-host/1791289671041735800/receipt.json` |
| Packaged Release dedicated server, three visible clients | Client 0, 750 ms at 4 s | 4 original processes; 3 simultaneous visible windows | `.build/integration-stall-final-dedicated/1791289708018917500/receipt.json` |

Commands are retained verbatim in the receipts and
`.build/qualify_stall_faults_final.py`; reproducible user command for the Release
dedicated case:

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.template_release.x86_64.mono.exe --editor bin/godot.windows.editor.dev.x86_64.mono.exe --mode dedicated --clients 3 --visible --preset wan --simulate-on both --duration 20 --client-stall-at 4 --client-stall-ms 750 --client-stall-index 0
```

The updated scene also passes the packaged Release abrupt-server-restart control
with three persistent visible clients at `.build/integration-stall-final-restart/1791289744153585400/receipt.json`.

Fresh native Debug and Release suites each pass ten CTest cases and 109 assertions:

```powershell
python misc/scripts/validate_egp_net.py --configuration Debug --output .build/integration-stall-final-native-debug
python misc/scripts/validate_egp_net.py --configuration Release --output .build/integration-stall-final-native-release
```

`.build/qualify_stall_session_final.py --configuration Debug --engine
C:/Users/Rose-X/.codex/worktrees/net-trilingual-api/EGP/bin/godot.windows.editor.dev.x86_64.mono.exe
--output .build/integration-stall-final-native-session` runs fresh matching-engine
24 GDScript/eight raw-channel checks, Box3D clock, token quarantine, 29 prediction
checks, lifecycle and separate processes. This second stage reuses only the fresh
Debug native suite from this run after checking every native source input hash.

`.build/integration-stall-native-build-final/{source,receipt}.json` records the
complete frozen source overlay, native/Mono editor, glue/managed assemblies and
Debug/Release template build commands. Final combined qualification at
`.build/integration-stall-final-native-qualification/receipt.json` freshly passes
77-class API/ClassDB/SDK/managed contracts, compiled native/C# poll and reload help,
19 Box2D runs, 28 Box3D cases, twelve relocated Box2D export checks, 59 trilingual
checks plus separate language processes/relocated exports, C++/C# hot reload and
recovery, default runtime, 250 cached-binding assertions, and packaged Debug host
and Release dedicated WAN/manual-reconnect controls. All use the final editor and
matching templates; the preceding pre-clock-fix receipts remain separate.

Final editor SHA256: `0ef40d51b6caa83aaebdb6bb19c75dd8e92dccc6ed0a26e289a725ec8a5a15de`.
Debug template SHA256: `ba9c53afdbde949ddcede154a63d4e1e1b3fcf697776cee17b331449f5bcd4b4`.
Release template SHA256: `608af42b3571ffd1f6f4f55f35a2580c7a645b46479aae324e2bb77761b66519`.

`.build/canonical-stall-artifacts.json` verifies 86 installed engine/managed
files against 22 passed receipts; 20 files changed and backups are retained.
`.build/integration-stall-canonical-runtime/receipt.json` passes 59 trilingual
checks again on the installed root-bin editor. Publication is verified separately
by `.build/integration-stall-publication.json`: canonical/local/remote heads,
original handoff ancestry, seven preserved worktrees/113 refs, no pending PR,
four inactive/completed handoff chats and only this checklist changed after engine
freeze. Unrelated `Temp Plan.md` and `tagged_query_test.b3rec` remain untouched.

Next clock/recovery gates: repeated client stalls, authoritative server clock
rejection and explicit recovery, longer outages and physics rollback/state restore.
Editor controls, wider mixed-language scenarios, larger worlds, peak memory/load,
soak, platform/physics parity, arbitrary ABI/concurrent reload, broader C# state,
lean server/default package identity, production authentication/persistence/gameplay
and performance remain open. The loop stays ACTIVE; full feature completion and
AAA readiness are not claimed.

## Repeated recovery and baseline clock receipt (2026-10-06)

`0bf7296d7e` adds `--client-stall-count 1..8` and `--client-stall-interval`
(default 8 seconds). Every gap waits for verified prior recovery, uses immutable
per-generation admission files and produces a chronological proof record. The
launcher checks identity continuity against authoritative server admissions,
revoked-entity input rejection, exact owner-input counts and healthy-client progress.
`57866fe77a` adds semantic evidence checks. Twenty-two restart/recovery unit tests,
eight rejected CLI combinations, help, Ruff/format and typing checks pass in
`.build/integration-repeat-tool-checks/receipt.json`.

`4d64b38c55` fixes a second startup boundary: client simulation waits for its
complete authoritative baseline and `Connected` state. Transport polling continues
while connecting/synchronizing; neither phase accumulates gameplay ticks. Server
clock startup and the eight-tick/0.5-second active simulation budgets are unchanged.
Native and generated C# help document this boundary. All compiled inputs freeze at
`4d64b38c554ab3dc491285f4ffa5119c001da56f`.

Preserved failures:

- `.build/integration-repeat-baseline/evidence/1791290081700199300/receipt.json`:
  original one-gap policy fails on a second 750 ms gap after successful recovery
  and owner-input acknowledgment; both catch-up diagnostics are retained.
- `.build/integration-repeat-development/1791290315028727300/receipt.json`:
  development indentation error sends repeated initial hello messages; corrected
  before freezing or final qualification.
- `.build/integration-repeat-dedicated/1791290773296070300/receipt.json`:
  the preceding Release template rejects a scheduling gap while still synchronizing,
  before any requested injection or gameplay tick.
- `.build/integration-repeat-sync-baseline/receipt.json`: old native core plus a
  64-entity rate-limited encrypted baseline reproduces that synchronization failure.
  Exact old source, executable/library hashes and failing command/log are retained.

Fresh Debug and Release native suites each pass ten CTest cases and 120 assertions.
The new regression verifies a delayed incomplete baseline never ticks, completing
it starts a fresh clock, and a later 750 ms active-clock gap still rejects and
clears all 64 replicated entities. Commands:

```powershell
python misc/scripts/validate_egp_net.py --configuration Debug --output .build/integration-repeat-native-debug
python misc/scripts/validate_egp_net.py --configuration Release --output .build/integration-repeat-native-release
```

The final matrix uses WAN impairment (100 ms latency, 25 ms jitter, 3% loss on both
endpoints) except the default local control. Exact commands and source/binary/PID
evidence are in `.build/integration-repeat-final-qualification/{commands,receipt}.json`:

| Case | Requested fault/control | Visible windows | Receipt |
| --- | --- | --- | --- |
| Mono editor / two clients | 1 gaps of 750 ms; client 0; 8.0 s interval | 0 | `.build/integration-repeat-final-single/1791291624505120400/receipt.json` |
| Packaged Debug host / three clients | 3 gaps of 750 ms; client 1; 8.0 s interval | 4 | `.build/integration-repeat-final-host/1791291653860886000/receipt.json` |
| Packaged Release dedicated / three clients | 3 gaps of 1000 ms; client 2; 8.0 s interval | 3 | `.build/integration-repeat-final-dedicated/1791291704450830100/receipt.json` |
| Mono editor / two clients | 2 gaps of 5000 ms; client 0; 12.0 s interval | 0 | `.build/integration-repeat-final-long/1791291754313143300/receipt.json` |
| Mono editor / two clients | 8 gaps of 550 ms; client 0; 7.0 s interval | 0 | `.build/integration-repeat-final-limit/1791291795667609200/receipt.json` |
| Packaged Debug host / three clients | Manual reconnect | 4 | `.build/integration-repeat-final-manual/1791291868852195200/receipt.json` |
| Packaged Release dedicated / three clients | Abrupt server replacement | 3 | `.build/integration-repeat-final-restart/1791291895194634100/receipt.json` |
| Mono editor / two clients | No fault / local default | 0 | `.build/integration-repeat-final-default/1791291931086405800/receipt.json` |

Repeated gaps retain all original processes and the same server PID; each recovery
has a distinct peer/owned-entity identity and an acknowledged new-owner input.
Healthy clients retain one connection and observe the exact final counter. The
eight-gap case is a bounded short run, not soak, scale or peak-memory qualification.

`.build/integration-repeat-native-qualification/receipt.json` freshly passes the
combined API/ClassDB/exact SDK and compiled native/C# help checks, 19 Box2D runs,
28 Box3D cases, twelve relocated Box2D export checks, 59 trilingual checks and
separate language/relocated exports, C++/C# reload/recovery, default runtime, 250
cached-binding assertions and packaged WAN/manual-reconnect controls.
`.build/integration-repeat-native-session/receipt.json` passes 24 GDScript/eight
raw-channel checks, Box3D clock, token quarantine, 29 prediction checks, lifecycle
and separate processes. It reuses only this run’s fresh Debug native suite after
verifying every native input hash. Build/source commands are retained at
`.build/integration-repeat-native-build/{source,receipt}.json`.

Editor SHA256: `20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`.
Debug template SHA256: `b8a7f178491ba56e02a8950a7ec337f33c6197a16a27b3cf295b85448cd91c82`.
Release template SHA256: `9e0c7883787974351153cc5bc30c92d4425b81184c264f4a4f9b74f995a68910`.

`.build/canonical-repeat-artifacts.json` verifies 86 installed engine/managed files
against 28 passed receipts; 20 files changed with backups retained.
`.build/integration-repeat-canonical-runtime/receipt.json` passes all 59 trilingual
checks again on installed root-bin binaries. `.build/integration-repeat-publication.json`
verifies canonical/local/remote master, ancestry, seven preserved engine trees,
current refs/PR inventory and all installed identities.

The user’s branch-cleanup chat moved the canonical checkout to sole `master` and
published README simplification `42d7358f49`; inherited release tips remain in local
`refs/archive/github-branch-cleanup-2026-10-06`. Documentation chat commit
`75d197f2ec` adds separate site-fork/synchronization tooling and guides. Those
independent changes are preserved; only documentation/tooling follows the compiled
engine freeze. The active documentation chat owns its remaining site validation.

Next: authoritative server clock rejection and explicit state/admission recovery,
longer outages and physics rollback/state restore. Larger worlds/load/soak, peak
memory, platform/physics parity, arbitrary ABI/concurrent reload, broader C# state,
lean server/default package identity, production authentication/persistence/gameplay
and performance remain open. The loop stays ACTIVE; full feature completion and
AAA readiness are not claimed.


## Same-process authoritative server clock recovery — 2026-10-06

Committed lab/tool source: `643aff51f6f75cfb779cd0f035c874d4f5f7bcbf` (initial change
`843d7053d2`, retired-admission proof refinement `643aff51f6`). Native engine source
remains `4d64b38c554ab3dc491285f4ffa5119c001da56f`. Editor SHA-256:
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`.
Debug template: `b8a7f178491ba56e02a8950a7ec337f33c6197a16a27b3cf295b85448cd91c82`;
Release template: `9e0c7883787974351153cc5bc30c92d4425b81184c264f4a4f9b74f995a68910`.
The API signature fingerprint remains
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`.

The new `--server-stall-at`/`--server-stall-ms` controls delay an admitted dedicated
server or listen host after all initial owner inputs are acknowledged. Poll must
return `FAILED`, emit the catch-up diagnostic, stop and clear peers/entities/ticks,
and reject entity creation/updates. After poll returns the fixture keeps the
configured Session, rebinds the original port, explicitly restores its application
counter, creates fresh root/owned entities and issues fresh tokens. Retaining the
Session keeps its peer/entity handle generations; old handles stay retired.
The listener generates a fresh secure key with the default key configuration.

An isolated unused-account probe tries its retired token before readiness is
published, with the listener slots empty. It must finish disconnected, without
synchronization/entities/peers. This excludes duplicate-account and full-server
rejection as false proof of key rotation. The original clients then disconnect,
clear caches and reconnect to the new baseline. Each deliberately sends a retired
entity input and a fresh owner input; exact counter/acknowledgment checks require
only the fresh command to apply once. Every server and client PID remains the same.
The clock budget, three-second transport timeout and original watchdogs are unchanged.

Preserved failure and negative-control evidence:

- `.build/integration-server-stall-baseline/evidence/1791292566339348300/receipt.json`:
  the preceding lab exits on an injected 750 ms server gap; native `FAILED`, stopped
  state, zero entities/ticks and the catch-up diagnostic were captured before edits.
- `.build/integration-server-stall-retired-key-control/receipt.json` verifies the
  deliberately retained test key is rejected by the new recovery gate. Its isolated
  launcher exits 1 with `retired admission reached recovered authority`; the retired
  unused-account token can authenticate when key rotation is disabled.
- `.build/integration-server-stall-proof-development/1791293256953549400/receipt.json`
  retains a GDScript inference parse error; explicit `bool` fixes it before the final
  source freeze. The preceding five-case development matrix remains separate in
  `.build/integration-server-stall-qualification/receipt.json`; it stops at the
  source-hash guard when the stronger retired-admission probe changes the fixture.

Fresh final qualification, all nine cases pass:

| Case | Original server/client PIDs | Server gap | Visible windows together | Final counter |
| --- | --- | --- | --- | --- |
| editor | 33256 / 30760 / 14152 | 750 ms | 0 | 106 |
| host | 16188 / 23200 / 14900 / 11608 | 750 ms | 4 | 112 |
| dedicated | 5244 / 18812 / 31400 / 22340 | 1000 ms | 3 | 112 |
| long | 31296 / 24500 / 15932 / 31436 | 5000 ms | 0 | 112 |
| minimum | 21640 / 27124 | 550 ms | 0 | 102 |
| client-control | 29612 / 34028 / 24592 | control | 0 | - |
| manual-control | 1656 / 28264 / 33320 / 12124 | control | 4 | - |
| restart-control | 4352 / 18572 / 26820 / 26252 / 2008 | control | 3 | - |
| default-control | 30116 / 10140 / 13524 | control | 0 | - |

The editor WAN test covers 750 ms with two clients; packaged Debug host and Release
dedicated tests cover three visible clients and 750/1000 ms gaps. The headless WAN
case covers a 5000 ms server gap and three clients. The minimum case uses one
client, a 550 ms gap and outgoing server-only WAN impairment. Controls recheck
two sequential client gaps, packaged host manual reconnect, packaged Release abrupt
server replacement and the default local lab with the modified scene/tool.

Exact commands, frozen fixture/helper/launcher hashes, matching template/editor and
exported runtime/PCK hashes, PID/window observations and all semantic proofs are in
`.build/integration-server-stall-final-qualification/{source,commands,receipt}.json`
and each referenced receipt. `.build/integration-server-stall-tool-checks/receipt.json`
records 43 semantic tests, 17 rejected argument combinations, help, Ruff checks/
formatting and mypy (exit zero; the existing Python 3.9 config emits a compatibility
warning). No tests or timing limits were relaxed.

All 86 installed artifacts still match `.build/canonical-repeat-artifacts.json`.
The engine-source delta contains only docs, the lab fixture and Python tooling;
compiled C++/C#/GDScript helpers, APIs and physics sources are unchanged. The prior
combined API/SDK/managed/physics/trilingual/reload qualification remains applicable
to these identical engine inputs/binaries; no duplicate engine build was started.
Old lab receipts are retained as historical fixture results, while these nine cases
qualify the current scene and launcher. Native Debug/Release retain all ten CTest
cases and 120 assertions each; installed root-bin trilingual qualification remains 59.

Merge ownership and publication are verified in
`.build/integration-server-stall-publication.json`: canonical/local/remote sole master,
original handoff ancestry, seven preserved trees and current refs/PR inventory.
The four original handoff chats remain inactive with completed turns; snapshots are
in `.build/integration-server-stall-handoffs.json`. Documentation/website forks were
published by their owner; their existing API reference is unchanged by this fixture
increment, and that chat owns synchronization of the updated lab guide.

Next: authoritative physics checkpoint/rollback and broader C#/C++ clock/lifecycle
recovery, repeated server faults and longer outages. Arbitrary application-state
restoration, larger worlds/load/soak, peak memory, platform/physics parity, arbitrary
ABI/concurrent reload, broader managed state, lean server/default package identity,
production authentication/persistence/gameplay and performance remain open.
The loop stays ACTIVE; full feature completion and AAA readiness are not claimed.
