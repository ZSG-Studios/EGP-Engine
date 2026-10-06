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
| Physics/network | Explicit fixed clock, fingerprint validation, authoritative state, commands, prediction/correction/replay and recovery | Trusted local Box3D checkpoint restore/replay and stable body mapping across same-process server recovery pass in editor and packaged Debug/Release; three-language helper compatibility passes; automatic client physics rollback and full game contract remain open |
| Networking | Encrypted admission, account/peer/entity identities, authority, ownership, interest, lifecycle, reconnect and backpressure | Native/language fixtures, matching-budget 64-entity fairness, bounded receive bursts and eight encrypted WAN clients with changing 4096-byte states pass; baseline, interest, revocation and reconnect are covered; larger worlds, peak load and soak remain |
| Advanced networking | Field deltas, bounded bandwidth/queues, input acknowledgments, lag compensation, scale/soak, malicious input rejection | Implementation/qualification gaps remain |
| Network lab | Dedicated server, listen host, N clients, visible windows, latency/jitter/loss, directional simulation, reconnect, logs/watchdog/cleanup | Native host and packaged Mono Debug host/Release dedicated server pass simultaneous visible clients, WAN simulation and reconnect; broader matrix remains |
| Network lab expansion | Editor controls; mixed GDScript/C#/C++ clients; packaged games; IPv6; server restart; interest/ownership checks; load/soak and adverse-condition matrix | Packaged Debug graceful and Release abrupt dedicated-server replacement pass with three persistent visible clients under WAN impairment; eight-second headless outage passes; single and repeated selected-client recovery and same-process server clock rejection/checkpoint/fresh admission/ownership recovery pass in editor and packaged Debug host/Release dedicated WAN runs; retired-token and retired-entity input rejection are covered; broader controls/scale/soak remain |
| C++ GDExtension | Scaffold, compiler errors/navigation, Debug/Release, exact SDK, reload, ABI/restart path, exported load | Matching SDK/editor controls, mixed-language exports, dynamic signature changes and rejected hierarchy/class repair pass on Windows; six cached instance/static call paths and nine return kinds now pass on Windows Debug SDK; arbitrary ABI changes and other platforms remain |
| C++ hot reload | Changed behavior in editor and running game, live instances/state/signals, failed build retains working code, repeat reload/unload cleanup | Two live Debug rebuilds preserve existing IDs, property state, callables and signals; failed compile retains published code; Missing/invalid DLL recovery and rejected base/extension-parent/ancestor/class-removal repair preserve extension and editable parent state; cached binding failure/default-return and compatible repair checks now pass; arbitrary ABI changes and soak remain |
| C# hot reload | Build/watch notifications, live running-game change, scene/state/event preservation, failed build recovery, repeated reload/ALC cleanup | Combined native/managed reload and corrupted-DLL/blocked-unload repair preserve instances, properties and events; public low-level C# session handoff, high-level NetNode reload and public C# Box3D adapter/world/body-map transfer preserve connected/stopped sessions and exact signal counts; high-level tree exit/reentry, fresh session traffic, retired callback isolation, reentrant close/stop replacement and freed codec replacement pass; default/feature overrides and no-change command pass; broader script-type/state/long-session matrix remains |
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
the initial test incorrectly reused 3D meter dimensions and was corrected. The
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

## Stable Box3D bodies and trusted server checkpoint recovery — 2026-10-06

Source freeze: `9576710ffffca3a0a45b3555673ac52e07adcb63`, including implementation
`3e31f3200bf25923d15dc8479f2e1ed8b7c40b50`. The later documentation commit records
these results. Native engine source remains `4d64b38c554ab3dc491285f4ffa5119c001da56f`;
editor SHA-256 remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`.
Debug/Release template hashes remain respectively
`b8a7f178491ba56e02a8950a7ec337f33c6197a16a27b3cf295b85448cd91c82` and
`9e0c7883787974351153cc5bc30c92d4425b81184c264f4a4f9b74f995a68910`.
The ClassDB signature fingerprint remains
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`.

`track(entity, body_id = 0)` / `Track(entity, bodyId = 0)` now accepts a stable
Box3D body ID separately from the replaceable network entity handle in GDScript,
C# and C++. Omitted/zero preserves the existing entity-as-body convention; a
negative body ID rejects the call without replacing a valid mapping. Installed
sample helpers are synchronized, and installer backups are preserved under
`.build/integration-physics-helper-backups`.

`--physics --server-stall-at 4` adds a bounded authoritative-physics recovery lab.
The server captures trusted local solver bytes at a command-free boundary, fails
the network clock after an injected scheduling gap, mutates the local world for
six ticks, rejects damaged bytes without changing that world, restores the valid
checkpoint with its exact hash/tick/body IDs, and repeats the six-tick branch with
the same diagnostic hash. It restores again before rebinding the retained Session
and maps fresh owned entities to the existing stable bodies. Fresh owner impulses
produce replicated finite positions/velocities in both epochs; stale entity inputs
cannot apply. Physics continues from its saved tick while transport restarts at
zero; the receipt checks the offset and continued stepping explicitly.

All seven final runtime cases passed with unchanged watchdogs and timeouts:

| Case | Original server/client PIDs | Server gap | Windows together | Final counter |
| --- | --- | --- | --- | --- |
| editor physics | 33876 / 33100 / 27420 | 750 ms | 0 | 106 |
| packaged Debug host physics | 31044 / 26900 / 34164 / 31056 | 750 ms | 4 | 112 |
| packaged Release dedicated physics | 30508 / 32132 / 9108 / 34900 | 1000 ms | 3 | 112 |
| long physics | 19152 / 20600 / 31780 / 35620 | 5000 ms | 0 | 112 |
| server control without physics | 12840 / 14812 / 21136 | 750 ms | 0 | 106 |
| repeated client-gap control | 31740 / 30048 / 31640 | control | 0 | 105 |
| default local control | 35452 / 9480 / 34232 | control | 0 | 100 |

Physics cases use the WAN preset (100 ms latency, 25 ms jitter, 3% loss in both
directions), two or three clients and 20/24-second runs. The editor checkpoint has
three bodies, tick 239, 6849 bytes and hash `60d48e816c12146e`; the three-client
checkpoints have four bodies and 8150 bytes. Source/helper/launcher hashes, exact
commands, runtime/PCK identities, windows, PIDs and per-epoch body mappings are in
`.build/integration-physics-mapping-qualification/{source,commands,receipt}.json`
and its seven referenced receipts. The reproducible matrix command is:

```powershell
& 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' .build/qualify_physics_mapping.py
```

`.build/integration-physics-mapping-languages/receipt.json` records fresh C++ Debug
and Release extension and C# builds against the installed SDK/managed packages,
60 interop assertions each in editor and relocated Debug/Release games, plus
separate encrypted C#/GDScript/C++ process fixtures in all three configurations.
The changed helpers and isolated copies of `InteropFixture.cs`/`probe.cpp` are
hash checked. Debug extension SHA-256:
`57bd391d3c81d1663861ee919f7a032501ebc65e018fe4018004a5c203fe5a58`;
Release: `6976b1902269fb3fa0a7854db0328ac77a87796b9a99bfeea5279ff690a9f803`;
editor fixture assembly:
`370e6f59fa76faa13c3aa058f87f8a40870208c94052dc87e8ae9c43be363634`.
`.build/integration-physics-mapping-default/receipt.json` separately passes the
existing one-argument adapter scene. Tool checks record 56 semantic tests,
18 rejected argument combinations, Ruff/format/help and mypy exit zero (existing
Python 3.9 configuration warning) in
`.build/integration-physics-mapping-tool-checks/receipt.json`.

Preserved controls and remaining diagnostic:

- `.build/integration-physics-mapping-baseline/evidence/1791294209347180400/receipt.json`
  retains the failed stable-body fixture using the preceding one-argument adapter:
  admission/counters recover, but clients receive no mapped physics history.
- `.build/integration-physics-mapping-retired-key7-control/receipt.json` passes the
  negative-control qualification: deliberately retaining a nonzero fixed test key
  allows the retired unused-account token to admit, causing the recovery gate to
  fail with `retired admission reached recovered authority` as required.
- `.build/integration-physics-probe-fresh/evidence/1791295116287389300/receipt.json`
  retains a fresh-token positive control that reaches Synchronizing in the same
  probe slot; its deliberate recovery-gate failure confirms admission is possible.
- The all-zero fixed test-key control unexpectedly finishes disconnected and its
  launcher passes, unlike the nonzero retained-key control. This also reproduces
  without physics in `.build/integration-physics-normal-key-control/evidence/1791295072271788600/receipt.json`;
  the physics result is `.build/integration-physics-mapping-retired-key-control/evidence/1791294709939559300/receipt.json`.
  Both were preserved as an OPEN diagnostic, not passing key-revocation evidence.
  The controlled admission-boundary qualification below resolves the key-value
  discrepancy; these historical runs did not record timestamps.
  Default generated-key rotation passes the final matrix. Explicit private-key
  reuse requires an application admission-revocation contract.

All 86 installed engine artifacts retain their prior hashes. Native core, physics,
ClassDB, SDK and generated managed glue are unchanged, so prior combined native
Debug/Release 120-assertion and engine physics/reload results apply to those inputs.
External helpers and sample fixture inputs changed and are qualified by the fresh
60-assertion builds above; the old 59-assertion fixture is historical evidence.
No duplicate engine build was started.

`.build/integration-physics-mapping-publication.json` verifies sole canonical/local/
remote master, original handoff ancestry, fresh seven-tree/ref/PR inventory and
unchanged foreign patches/untracked hashes. Unrelated root files remain preserved.
The original four chats have completed turns; the documentation/website owner is
responsible for synchronizing this published helper/lab increment.

Next: diagnose the zero-key recovery control, broader C#/C++ clock/lifecycle
recovery, repeated server faults and larger authoritative worlds. This fixture
uses trusted in-memory local snapshots and a small sphere/floor world. Automatic
scene persistence, client physics rollback, untrusted solver-byte ingestion,
arbitrary game-state recovery, cross-platform determinism, scale/soak, production
authentication, broader reload/ABI state and performance remain open.
The loop stays ACTIVE; full feature completion and AAA readiness are not claimed.

## Admission timestamps and retained-key recovery — 2026-10-06

New fixture/tool source: `96757d1596810f1f996b80da91c4d76f6e466553`, following
`cafb5ae21d` (validator), `19cff5aa04` (export preset/artifact checks), and
`96757d1596` (preserve exact template identity). The later checklist commit records
qualification. Native engine source is unchanged at
`4d64b38c554ab3dc491285f4ffa5119c001da56f`; editor SHA-256 remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`.
Debug/Release templates remain
`b8a7f178491ba56e02a8950a7ec337f33c6197a16a27b3cf295b85448cd91c82` and
`9e0c7883787974351153cc5bc30c92d4425b81184c264f4a4f9b74f995a68910`.
Native ClassDB APIs, SDK, generated managed glue and external language helpers
are unchanged.

The preceding zero/nonzero retained-key discrepancy is explained by a separate
transport admission rule, rather than a demonstrated key-value distinction.
Bundled `thirdparty/yojimbo/netcode/netcode.c` sets the listener's minimum token
expiry to its UTC start second plus `max_connect_token_lifetime` (line 4372),
then rejects earlier expirations before decrypting the token (line 1942).
The issuer uses UTC seconds and `expiry = creation + lifetime` (line 5614).
EGP configures the maximum to its issued lifetime. Crossing a second between
issuance and rebind therefore rejects even a token authenticated by a retained
key. Within the same second, an unused token can still admit. The old failed/
unexpectedly passing controls remain preserved; their receipts lacked these
timestamps, so their exact historical timing is not retrospectively asserted.

The new user-facing `misc/scripts/validate_egp_net_admission.py` runs an isolated
native/GDScript fixture. `--api`, `--key`, `--boundary` and `--fault` select cases;
`--editor` supports packaged Windows template qualification. Defaults test both
APIs, all three key modes and both boundaries under clock failure. The full matrix
also includes graceful stop/rebind. Fixed test keys and tokens remain in memory;
receipts contain only public creation/expiry/restart times, process identities,
diagnostics, connection histories, source hashes and artifact hashes. The fixture
waits for a UTC boundary while polling, then deliberately pauses 550/1100 ms.
It requires the requested boundary before accepting evidence; a scheduling miss
fails without retries or relaxed time limits. Each new listener starts with empty
peers/entities and admits an unused account, excluding capacity/duplicate masking.

Final qualification: 24 cases each in the Mono editor, packaged Debug and packaged
Release, 72 in total. Every combination of API, key mode, boundary and clock/
graceful fault passes:

| Server key | Token creation vs new listener second | Observed admission |
| --- | --- | --- |
| retained zero test pattern | same | Connected, one peer |
| retained nonzero test pattern | same | Connected, one peer |
| retained zero/nonzero test pattern | earlier | Disconnected, no peers/baseline |
| generated per listener | same or earlier | Disconnected, no peers/baseline |

Clock cases require native `FAILED`, exactly the catch-up diagnostic, stopped/
cleared authority and the original port rebound. Graceful controls require normal
stop with no diagnostic. Generated-key same-second rejection now excludes the
restart-timestamp gate as its explanation. This closes the bounded zero-key
diagnostic and provides reproducible retained-key controls; it does not implement
backend revocation for servers configured to retain a key.

Exact commands, hashes and 72 process identities are in
`.build/integration-admission-boundary-verified-qualification/{source,receipt}.json`
and its three referenced receipts.

Representative clock-fault process/timestamp observations:

| Configuration | Retained zero, same second PID / creation / restart | Retained zero, cross second PID / creation / restart |
| --- | --- | --- |
| editor | 34600 / 1791296891 / 1791296891 | 2308 / 1791296895 / 1791296896 |
| Debug | 19272 / 1791296997 / 1791296997 | 6084 / 1791296999 / 1791297000 |
| Release | 5060 / 1791297096 / 1791297096 | 19620 / 1791297098 / 1791297099 |

Packaged executable hashes equal their respective templates above. Debug PCK:
`8680402edfc8db0e1716e8288bb70797c2fc3a39ea76849f444424d795d4890e`;
Release PCK: `790aa60de721a70c9490c7fbcbc8cbe979d6c1f00be208d6f4477d527fd8d848`.
Reproduce the matrix with:

```powershell
& 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' .build/qualify_admission_boundaries.py
```

The public single-configuration command is:

```powershell
python misc/scripts/validate_egp_net_admission.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --fault both
```

`.build/integration-admission-boundary-tool-checks/receipt.json` records 13 semantic
evidence tests, six rejected argument combinations, help, Ruff/format and mypy
exit zero (existing Python 3.9 configuration warning). Tests reject timestamp
masking, ambiguous restart boundaries, short lifetimes, occupied slots, uncleared
authority and missing clock failures. Development receipts retain a fixture API
call error, the missing export fields and optional Windows resource rewriting
detected by artifact verification. The final export supplies the required fields,
disables resource rewriting and checks exact executable/template equality plus
PCK presence. No engine fault, timeout, assertion or native admission rule was
weakened to make these tests pass.

`.build/integration-admission-boundary-publication.json` verifies combined source,
all 86 installed artifacts, seven preserved worktrees, refs/PR inventory, original
handoff ancestry and sole canonical/local/remote master. The prior seven physics
lab cases and 60-assertion trilingual builds retain identical fixture/helper/native
inputs; their evidence is reused for that unchanged scope. No duplicate engine
build was needed. The four original engine chats remain inactive with completed
turns. The documentation/website chat owns its pinned publication and subsequent
sync, with root edits kept separate.

Next: broader C#/C++ clock/lifecycle state, repeated authoritative server failures
and larger physics worlds. These admission tests use two sessions in one local
process on Windows. Remote/back-end authentication, arbitrary game/ABI state,
automatic client physics rollback, cross-platform parity, scale/soak and
performance remain separate gates. The loop stays ACTIVE; full feature completion
and AAA readiness are not claimed.

### Repeated C#/C++ clock recovery — 2026-10-06

Source `4cd4f9b317606a387cdde587be23785a96a9ba7f` fixes the C++ trilingual
sample's `poll()` binding: it now returns the first high-/low-level native error
instead of discarding it. C# fixture pumps check that result. Native clock failure
already stopped authority; this fixes its visibility to sample callers. Public
engine/library/ClassDB/generated glue and external helper APIs are unchanged.

Fresh C++ Debug/Release extensions and C# assemblies pass all 21 language
validator steps. Editor, relocated Debug and relocated Release each pass 93
interop assertions, including the existing encrypted separate-process
C#/GDScript/C++ fixtures. The new recovery sequence exercises three successive
clock failures on each language's high-level and low-level session: 12 faults per
configuration, 36 total. Each intentional polling gap is at least 550 ms.

| Configuration | Interop PID | Assertions | Clock faults |
| --- | --- | --- | --- |
| Mono editor | 23324 | 93 | 12 |
| relocated Debug | 3364 | 93 | 12 |
| relocated Release | 26436 | 93 | 12 |

Every cycle requires `FAILED`, the native catch-up diagnostic, cleared authority,
rejected work while stopped, the same retained session and port, and a fresh
entity handle with the old handle absent/rejected. High-level C# and C++ also
detach their Box3D adapter, deliberately advance the solver, restore a trusted
local checkpoint with exact hash/tick equality, then attach a fresh entity to the
same stable physics body (10000/20000). Eight new authority ticks must advance the
world from its checkpoint offset. Across the three cycles, C# checkpoint ticks
are 30/38/46 and C++ ticks are 8/16/24. These local snapshots never enter network
messages or receipts. Both high- and low-level sessions require exactly three
diagnostics; session retention and monotonically fresh handles are independently
checked by the Python evidence reader.

`.build/integration-language-clock-final/{source,receipt}.json` freezes the exact
command, source hashes, SDK archives, fixture assemblies/extensions, exported
runtime/PCK hashes and process identities. Engine source remains
`4d64b38c554ab3dc491285f4ffa5119c001da56f`; installed editor SHA-256 remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`.
Debug/Release templates remain
`b8a7f178491ba56e02a8950a7ec337f33c6197a16a27b3cf295b85448cd91c82` /
`9e0c7883787974351153cc5bc30c92d4425b81184c264f4a4f9b74f995a68910`.
SDK fingerprint `4bc13481314e7023`, ClassDB signature
`e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271`.
Fresh Debug extension SHA-256
`1153f528705d6af22ab1882065b5f3c446d79ce4ae5b2ea7af3403b2be2c16a0`;
Release extension
`e33fcb74a1e7b7c7edf1b8fa62c910f44fa7b8aa5e1e95210df5b3fe03a0af10`;
editor fixture assembly
`7282ff98411a3d5beff825fbbb7df34b1d47fa7c0e6b681550b3ad381d822872`.

Reproduction command (choose a new output directory to preserve this evidence):

```powershell
& 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' misc/scripts/validate_egp_net_languages.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --sdk C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/sdk/4bc13481314e7023 --sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib --release-sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Release/egp_godot_cpp.lib --packages bin/GodotSharp/Tools/nupkgs --template bin/godot.windows.template_debug.x86_64.mono.exe --release-template bin/godot.windows.template_release.x86_64.mono.exe --output .build/integration-language-clock-new
```

`.build/integration-language-clock-poll-control-final/receipt.json` compares the
preceding and corrected C++ DLLs against the same installed engine and isolated
GDScript fixture. Both stop at the intentional clock fault; the preceding dynamic
`poll` call returns NIL and masks the native error, while the corrected call
returns `FAILED` (1). The preceding typed-call fixture's script error/watchdog
logs remain preserved separately; the final control uses dynamic `call` to
observe both return values directly. No engine timeout or clock rule changed.

`.build/integration-language-clock-tool-checks/receipt.json` records eight semantic
tests plus passing Ruff/format and mypy (the existing Python 3.9 configuration
warning remains). Tests reject incomplete cycles, handle reuse, session loss,
physics clock reset, missing failures and incorrect diagnostics.

`.build/integration-language-clock-publication.json` is the publication gate:
canonical/remote master equality, original handoff ancestry, all 86 installed
artifacts, seven preserved worktrees, branch/PR inventory and current source/binary
evidence. The former 60-assertion language fixture is historical; the fresh
93-assertion runs supersede it. Prior native/physics/admission/lab/reload evidence
is reused only for unchanged inputs. No duplicate engine build was necessary.

Remaining: peer/client reconnect during these repeated C#/C++ faults, hot reload
during faults, larger authoritative physics worlds and arbitrary application/ABI
state recovery. These new fault cycles have no connected remote peers. Existing
separate-process networking evidence remains a separate, bounded check. Windows
editor/Debug/Release results do not establish other platforms, scale/soak,
production admission or performance. The loop stays ACTIVE.

### Live C#/C++ clients across authority clock faults — 2026-10-06

Source `04961fdbc5` extends the trilingual recovery fixture with live high-level
C#/C++ clients. Evidence-reader correction
`96d2d3f2d4c5550c58fdeed15c0a763876886a3a` requires the native `Synchronizing`
stage in every admission history. All 21 validator steps now pass with fresh
Debug/Release C++ extensions and C# assemblies. The editor, relocated Debug and
relocated Release each execute 133 counted interop assertions.

| Configuration | Interop PID | Assertions | Live-client recoveries / total clock faults |
| --- | --- | --- | --- |
| editor | 29968 | 133 | 6 / 12 |
| relocated Debug | 17292 | 133 | 6 / 12 |
| relocated Release | 9208 | 133 | 6 / 12 |

The high-level authority misses at least 550 ms of polling while its authenticated
UDP client continues polling and stays Connected. On each native `FAILED`, the
authority stops/clears, the fixture explicitly stops the client and verifies
empty baselines and rejected input while stopped (`ERR_UNCONFIGURED`). It restores
the trusted Box3D checkpoint, rebinds the original port and attaches the physics
clock before starting admission. This order avoids advancing the transport clock
while the restored physics world is detached.

Each client retains the same native session through initial admission and three
reconnects. Every admission uses a newly issued token, authenticated client ID
777 (C#) or 888 (C++), and the exact state sequence
`Connecting -> Synchronizing -> Connected -> Stopped`. Restored authority creates
a fresh owned entity for stable physics body 10000/20000. The client must receive
only that entity, with a physics tick after the checkpoint and continued falling
motion. The authority rejects input for the retired handle and delivers input
for the fresh owned handle exactly once. Hiding then showing that entity must
remove and restore its client baseline. All cycles require zero invalid input
callbacks. Configured client outbound simulation uses 20 ms latency, 5 ms jitter
and zero loss; this is a bounded client simulation, not a bidirectional WAN/soak
qualification. Token bytes and local snapshots remain outside receipts.

Across three configurations: 18 live-client fault recoveries, 24 fresh admissions
(including initial joins), and 36 total clock faults. The low-level cycles retain
their preceding local no-peer scope. Existing separate-process encrypted
C#/GDScript/C++ checks also pass, independently of these same-process fault
cycles. No engine, ClassDB, generated glue or external helper API changed.

`.build/integration-language-client-clock-verified/{source,receipt}.json` records
the exact validator command, source hashes, child PIDs, SDK/archive identities
and relocated export bundles. Reproduce with the preceding full command using
`--output .build/integration-language-client-clock-new`. Installed engine source,
editor/templates, SDK fingerprint and ClassDB signature remain those listed in
the preceding section. Fresh Debug extension SHA-256:
`64dede3830abccb3b31e8c78b6db423637017336eca4898e9647ebea5f5b82ce`;
Release extension:
`dbff0ab7200e199a171aac5ff6c0cc859a5b8f0d1083fd531e2361c17e3fcf68`;
editor C# fixture assembly:
`30f0caa197ae99dd92c3a3287161a4a1c6f49543c29e61554924f584fd2dfdc6`.
Exported PCK SHA-256:
`3a07f59eb1a9796ebd042cc040b961885484e1842b82e4865058f8dcca2fad04`.

`.build/integration-language-client-clock-verified-tool-checks/receipt.json`
records 16 semantic tests and passing Ruff/format/mypy (the existing Python 3.9
configuration warning remains). New negative cases reject a paused client,
uncleared baselines, reused admission, missing synchronization/reconnect history,
old client physics time, accepted retired input and failed visibility restoration.
The first editor run passed its 133 runtime assertions but the reader rejected
the legitimate `Synchronizing` stage. Its failed receipt/logs remain in
`.build/integration-language-client-clock-final`; the corrected reader requires
that stage, and the fresh verified matrix supersedes that run. No runtime gate,
clock budget or watchdog was relaxed.

`.build/integration-language-client-clock-poll-control/receipt.json` repeats the
preceding NIL/current `FAILED` DLL comparison using the fresh Debug extension.
`.build/integration-language-client-clock-publication.json` verifies original
handoff ancestry, canonical/local/remote master equality, 86 installed artifacts,
seven preserved worktrees and no open PRs. Fresh 133-assertion evidence supersedes
the changed 93-assertion fixture. Prior native/physics/admission/lab/reload evidence
is reused only for unchanged inputs; no duplicate engine build was needed.

Next: independent-process stalled servers/client reconnect, automatic recovery
policy, low-level connected-peer failures, hot reload during faults and larger
physics worlds. Explicit same-process client reset/rejoin is now qualified; it
does not establish automatic fault discovery, client physics rollback, arbitrary
application/ABI persistence, platform parity, scale/soak or performance. The
four original engine chats remain completed; documentation/website publication
has separate ownership and receives the committed source handoff. The loop stays
ACTIVE; full feature completion and AAA readiness remain unclaimed.

### Independent authority stalls and client recovery — 2026-10-06

Source `3fbbf5ec8e` adds an isolated GDScript authority and C#/C++ process clients.
`bc87f819d9d56a196b19fc5dacbed4a326ec7120` corrects millisecond evidence handling:
two polls may share a timestamp, but each stall requires at least ten distinct
client observations. Fresh language qualification passes all 27 steps, including
133 interop assertions in each editor/relocated Debug/relocated Release run and
the preceding encrypted separate-process checks.

| Configuration / client | Authority PID | Client PID | Distinct polls during the three stalls |
| --- | --- | --- | --- |
| editor / C# | 15060 | 30300 | 33 / 33 / 33 |
| editor / C++ | 19864 | 28324 | 33 / 33 / 33 |
| Debug / C# | 21828 | 26608 | 33 / 33 / 33 |
| Debug / C++ | 7312 | 18356 | 33 / 33 / 34 |
| Release / C# | 29384 | 25884 | 33 / 33 / 33 |
| Release / C++ | 21812 | 32968 | 33 / 33 / 34 |

Six process pairs complete 18 independent server faults, 18 native client
disconnects and 24 authenticated admissions. Each authority deliberately stops
polling for at least 550 ms; the separate client continues polling. The native
authority returns `FAILED` and clears peers/entities, restores a trusted local
Box3D checkpoint and rebinds its original port/session with the clock attached.
The client discovers the native `Disconnected` state through polling, verifies
an empty baseline and rejected input, explicitly stops, then obtains the next
fresh fixture token and rejoins using the same native session.

Four admissions per client require `Connecting -> Synchronizing -> Connected`;
the three failures include native `Stopped -> Disconnected`, followed by the
fixture's explicit `Stopped` before rejoining. Each new owned entity has a fresh
handle and a physics tick after the restored checkpoint. The authenticated
9876 account sends exactly one owner input per epoch and completes a ready/reply/
ack exchange before the next fault. Entity ownership uses the new authenticated
peer handle, not a retained transport handle. Physics body 10000 persists through
all three trusted checkpoint restores. Token files live only in a temporary,
test-only trusted handoff directory; receipts/logs contain no token bytes or
physics snapshots. This policy demonstrates fixture recovery, not production
account admission, token refresh or retry/backoff policy.

The public runner supports `--engine`, `--project`, `--client-language` and
`--output`; clients are `csharp` or `cpp`. Example using the already built isolated
project (choose a new output directory):

```powershell
python misc/scripts/validate_egp_net_clock_process.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --project .build/integration-independent-clock-qualified/project --client-language cpp --output .build/independent-clock-new
```

For packaged games omit `--project` and point `--engine` to the relocated Mono
executable. The full build/export command is recorded exactly in
`.build/integration-independent-clock-qualified/source.json`; it is the previous
language validator command with a fresh output directory. Its receipt references
all six independent process receipts. Each captures command/PID/exit status,
source and engine hashes, native state histories, UTC poll observations and
checkpoint evidence. The reader cross-checks public UTC gaps against the monotonic
550 ms delay, requires ten distinct observations per stall, bounds observed client
poll gaps at 250 ms and rejects shared process identities. Observed maximum poll
gaps were 17/18 ms in these runs; this is fixture health evidence, not an engine
or networking performance benchmark.

Fresh interop PIDs: editor 18720, Debug 34124, Release 11696. Debug extension
SHA-256 `8df9eb61874b7a3575f0982a06998671bbe1a27c09b054379982febeaf46d7ec`;
Release extension `77b62c7446acd982f8e0114936f307862fc7538451d793533de72c7a11275cae`;
editor C# fixture assembly
`79ca16e8b02ba3e7da192eda8cef8b1b60b2bf55e8f4022eb93c7c4c8bcd4d8e`;
exported PCK `6359a7fde8afa72c640dec43bc709b33fdc49a0b19058f1081158ee435ab50f7`.
Installed editor/templates, engine source, SDK archives, fingerprint and ClassDB
signature remain the preceding verified identities. Engine/core/physics/glue and
external helper APIs are unchanged; the sample adds fixture-specific bindings.

`.build/integration-independent-clock-tool-checks/receipt.json` records 30 semantic
tests (16 language, 14 process), help and passing Ruff/format/mypy with the existing
Python 3.9 configuration warning. Negative cases reject paused clients, shared
PIDs, timestamp jumps/duplicate sample inflation, stale entities/physics time,
missing native disconnects, uncleared baselines, absent owner inputs and reused
admission. The first run's runtime passed but the evidence reader rejected a
coincident millisecond timestamp; its receipt remains preserved. A subsequent
command accidentally supplied the Release SDK archive to Debug compilation;
the linker correctly rejected its runtime/iterator ABI mismatch. That failed
build is retained separately. The final fresh matrix uses matching archives.
No native clock budget, ABI requirement or watchdog was relaxed.

`.build/integration-independent-clock-poll-control/receipt.json` retains the
preceding NIL/current `FAILED` control against the fresh Debug DLL.
`.build/integration-independent-clock-publication.json` verifies canonical/local/
remote master equality, original handoff ancestry, 86 installed artifacts,
seven preserved worktrees and no open PRs. Changed sample/validator inputs have
fresh builds; prior native/physics/admission/lab/reload evidence is reused only
for unchanged inputs. No duplicate engine build was necessary.

Next: production recovery policy, connected low-level faults, hot reload during
faults, process crashes/hard network outages, larger worlds and longer WAN/loss
qualification. These are one authority plus one client per isolated pair on
Windows, with client outbound 20 ms latency/5 ms jitter/zero loss. The new native
disconnect discovery and fixture rejoin checks do not prove client physics
rollback, arbitrary game/ABI persistence, backend admission, other platforms,
scale/soak or performance. The loop remains ACTIVE.

### Connected low-level C#/C++ peers across clock faults — 2026-10-06

Source `9702ab673e9ecd2dfcc24b1bd95d7221a8e9cbb3` replaces the no-peer low-level
clock cycles with authenticated live clients. All 27 language validator stages
pass with fresh C++ Debug/Release extensions and C# assemblies. Editor,
relocated Debug and relocated Release each execute 197 interop assertions.

| Configuration | Interop PID | Assertions | Connected low-level fault recoveries |
| --- | --- | --- | --- |
| editor | 3664 | 197 | 6 |
| relocated Debug | 2152 | 197 | 6 |
| relocated Release | 14192 | 197 | 6 |

Across these three runs, low-level sessions complete 18 connected-client fault
recoveries and 24 fresh admissions, including initial joins. C# uses account
556 and C++ uses 667. Each client keeps polling while the low-level authority
misses at least 550 ms; the native authority returns `FAILED` and clears its
entities/peers. The client detects `Disconnected`, verifies empty peers/entities
and rejects application sends (`ERR_DOES_NOT_EXIST`), explicitly stops, then
rejoins with a fresh token using the same native session. Both authority session
and bound port also persist through all three recoveries.

Each admission has the native `Connecting -> Synchronizing -> Connected` history;
each failed connection includes `Stopped -> Disconnected` and the fixture's
explicit `Stopped` before rejoin. Authority peer handles advance
`257 -> 513 -> 769 -> 1025`, while entity handles advance `1 -> 2 -> 3 -> 4`.
After each rejoin, operations using the retired peer must fail: application send,
disconnect and visibility return `ERR_DOES_NOT_EXIST`, and spawn with that peer
as authority returns `ERR_INVALID_PARAMETER`. Updating the retired entity also
returns `ERR_DOES_NOT_EXIST`. The fresh entity's authority metadata identifies
the new authenticated peer.

Opaque baseline bytes are verified exactly, including zero and 0xff:
`0100ff2a`, `0200ff2a`, `0300ff2a`. Each cycle exchanges one application payload
and one channel-3 ReliableOrdered packet in each direction. Callback checks
require the exact authenticated sender, channel/delivery and bytes; cumulative
counts must be 1/2/3 without duplicates. Hide/show removes and restores the raw
entity baseline with identical opaque state. The resumed authority clock must
advance at least eight ticks. These checks qualify raw-session transport and
ownership metadata; gameplay interpretation/authorization of opaque application
messages remains the application's responsibility.

The high-level C#/C++ physics recovery checks also pass, with 18 high-level local
recoveries. Thus the mixed fixture now has 36 connected high-/low-level recoveries
and 48 admissions across configurations. Fresh independent high-level process
runs retain their separate 18 server faults and 24 admissions:

| Configuration / client | Authority PID | Client PID |
| --- | --- | --- |
| editor / C# | 24952 | 32844 |
| editor / C++ | 14192 | 16788 |
| Debug / C# | 24232 | 22540 |
| Debug / C++ | 27520 | 16652 |
| Release / C# | 29488 | 18356 |
| Release / C++ | 32236 | 18104 |

PIDs identify individual executions; Windows may reuse a PID in a later run.
Each independent pair has distinct authority/client identities, 33/34 distinct
client observations per server stall and a 17/18 ms maximum observed poll gap.
Those observations are bounded fixture health checks, not performance evidence.
The separate-process encrypted C#/GDScript/C++ checks pass as well.

`.build/integration-connected-low-clock-final/{source,receipt}.json` records the
exact command, source hashes, SDK/archive identities, fixture DLLs/assemblies,
runtime/PCK hashes and all referenced process evidence. Reproduce with the
preceding full language validator command and a new output directory. Fresh
Debug extension SHA-256
`c921ce73215998862102b357cc652b72527678fb06e750f6de9a0cbd6de5ec0d`;
Release extension
`fced8b85d7a1252d5becb226b06b46e030df987aec3eb12b8975e6c2046cf70f`;
editor C# assembly
`4b53649d1b0f8f6192373577ab063a39e4cfab7313e83f27d83f97bd86f5a34b`;
exported PCK
`3ef1c3e2bb9e8d11c6a8603b23efd79a50b472bf6967a7e6100557053a06e1f8`.
Engine/core/physics/ClassDB/glue and external helper API inputs remain unchanged,
with installed identities listed above. No duplicate engine build was needed.

`.build/integration-connected-low-clock-tool-checks/receipt.json` records 36
semantic tests (22 language, 14 independent process), help and passing Ruff/
format/mypy with the existing Python 3.9 configuration warning. New negatives
reject reused/accepted retired peer handles, corrupted opaque bytes, duplicate
application delivery, missing channel delivery and absent low-level disconnects.
The refreshed NIL/current `FAILED` DLL control is preserved in
`.build/integration-connected-low-clock-poll-control/receipt.json`.

`.build/integration-connected-low-clock-publication.json` verifies original
handoff ancestry, canonical/local/remote master equality, 86 installed artifacts,
seven preserved worktrees and no open PRs. Fresh 197-assertion builds supersede
the changed 133-assertion fixture; prior native/physics/admission/lab/reload
evidence is reused only for unchanged inputs. Four original engine chats remain
completed; documentation/website publication retains separate ownership.

Next: independent-process low-level faults, hot reload during faults, production
admission/retry policy, crashes/hard outages, larger physics worlds and sustained
WAN/loss qualification. New low-level recovery uses one authority/client pair
per language in one local process on Windows, with 20 ms client latency, 5 ms
jitter and zero loss. It does not establish physics rollback, arbitrary game/ABI
state, backend admission, cross-platform parity, scale/soak or performance. The
loop remains ACTIVE.


### Native sessions retained across C++/C# reload after a clock fault — 2026-10-06

Source `9d52a21143` adds an opt-in `--network-recovery` case to the real editor
debugger/build-panel reload fixture. Source `8a561c504bf370837e438af26e6783349c9ef6a8` advances the
last ABI-repair callback checkpoint before the combined case. Both changes are
committed on canonical master above `642b95db236325c8809ba9af6db084825ab0c31c`.
The engine, installed SDK, ClassDB fingerprint and managed runtime remain
unchanged; no duplicate engine build was required.

The full combined fixture passes at `.build/integration-network-reload-fully-qualified/1791301596805219400/receipt.json`.
Headless editor PID 34936 launches separate running-game PID
7920. Existing checks freshly pass for compile failures, C# assembly
corruption and blocked unload/retry, missing/invalid DLL repair, method argument/
return changes, rejected direct/extension/ancestor base changes, class removal
and compatible restoration. Live object IDs, counter/vector/reference/parent
state, cached dynamic callables, signal/delegate counts and managed lifecycle
hooks survive those repairs. This is dynamic lookup evidence; the preceding
raw MethodBind/ptrcall qualification retains its separate unchanged input scope.

The added local native-session case starts one authenticated authority/client
pair and assigns both session references to serialized dictionaries on the live
C++ and C# objects. C++ uses generated EGPNetSession bindings; C# uses the native
GodotObject call API. The native session emits application callbacks through
method-name Callables into both reloadable objects. This does not qualify
serialization of arbitrary managed facade instances or event closures.

The authority misses 550 ms while the client executes
64 language-driven native polls. The authority returns
`FAILED`, emits the exact fixed-clock diagnostic and clears its peers/entities.
The client discovers native `Stopped -> Disconnected`, clears replicated state
and is explicitly stopped. While both sessions are stopped, the fixture rebuilds
C# to version 6 and C++ to version 4 and uses the editor's real reload notification.
Session references and ObjectIDs remain identical, generated/dynamic poll calls
work after reconstruction, tick/peer/entity state stays empty and reload does
not implicitly restart the authority.

Only explicit rebind and fresh-token admission restarts networking, on the same
port and native sessions. Peer handles advance `257 -> 513`;
entity handles advance `1 -> 2`. Old peer sends
and old entity updates return `ERR_DOES_NOT_EXIST`. Fresh owner metadata and the
client baseline match exactly, including zero/0xff bytes (`0100ff2a`, `0200ff2a`).
Each admission sends one application payload; C++ and C# callback counts are
exactly 1 then 2, with the authenticated sender and byte payload checked. The
new authority advances at least eight ticks after delivery. Raw transport and
ownership metadata do not authorize opaque gameplay messages.

Reproduce the combined gate with a new output directory:

```powershell
python misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery --network-recovery --output .build/integration-network-reload-fully-qualified
```

The receipt records frozen source/fixture SHA-256, executable/runtime identities,
separate editor/game PIDs, generated extension/assembly/descriptor hashes,
samples and debugger diagnostics. Final C++ DLL SHA-256 is `2246fa358c6c1b96eefee4a3150a478cb6ba1bd703e76e51c555f4a1dbe8751f`;
C# assembly is `86f7fe0aefee5982f52578172b5f94c232c40473742a0363caa89da7ded9bbf6`. Native editor SHA-256 remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`, compiled from
`4d64b38c554ab3dc491285f4ffa5119c001da56f` with the matching installed Debug SDK.

`.build/integration-network-reload-final-tool-checks/receipt.json` passes fourteen
semantic evidence tests, two incompatible-option CLI checks, help, Ruff/format
and mypy (the existing Python 3.9 configuration warning remains). Negatives reject
replaced native sessions/game process, lost serialized references, implicit
restart, missing diagnostics/client polls, lost/duplicated callbacks, corrupt
payloads, reused/usable retired handles and missing admission/checkpoints.

The runtime-disabled player also freshly passes with current fixture sources at
`.build/integration-network-reload-default/1791301774032497900/receipt.json`:
the separate game retains a non-collectible assembly. Its input/generated hashes
are checked in the publication gate. No exported-runtime reload claim follows.

Failed fixture iterations remain preserved: initial C# type lookup at
`.build/integration-network-reload-initial/1791301246924050300/receipt.json`, the
incorrect client server-peer argument at
`.build/integration-network-reload-checked/1791301286375451000/receipt.json`, and
the stale post-ABI callback checkpoint at
`.build/integration-network-reload-qualified/1791301424603653900/receipt.json`.
The focused successful precursor is
`.build/integration-network-reload-repaired/1791301352097921700/receipt.json`.
These are fixture failures and their repairs; no catch-up budget, impairment or
watchdog was weakened and no engine behavior fix is claimed in this increment.

`.build/integration-network-reload-publication.json` gates normal publication:
original handoff ancestry, exact canonical/remote master equality, 86 unchanged
installed artifacts, seven preserved foreign/canonical worktrees, original
untracked bytes and no open PRs. The preceding 197-assertion editor/Debug/Release
trilingual runs, 36 semantic networking tests, native 120-check/ten-case suites,
seven physics lab cases and 72 admission cases are retained only after verifying
their unchanged source/binary inputs. Changed reload fixtures use the fresh
combined repair/fault run above. Other worktree leftovers stay preserved rather
than being declared clean or newly merged. Documentation/website publication
keeps separate chat ownership.

Next: active-connection reload under latency/jitter/loss, independent-process
low-level fault/reload, physics checkpoint recovery during reload, production
admission/retry, crashes/hard outages, larger worlds and sustained WAN/scale/soak.
This newly qualified case is Windows Debug, one local pair sharing the game
process, with reload performed after the authority has stopped. It does not
establish arbitrary application/ABI state, automatic physics rollback, exported
runtime reload, platform parity or performance. The loop remains ACTIVE.


### Uninterrupted live C++/C# reload with network impairment — 2026-10-06

Source `66454513346e224151c1656b505505722aac5928` extends the isolated real-debugger/build-panel
fixture with `--network-live-reload` and explicit simulation options. It is
committed on canonical master above `ca35e9c266c1ffaa3163b80e7bc81199a65159e2`.
The public/native APIs, core, physics, ClassDB fingerprint, SDK, installed
engine and managed runtime are unchanged. This increment adds qualification
and testing options; it does not claim an engine behavior repair.

The full live run passes at `.build/integration-live-reload-qualified/1791302303954586800/receipt.json`. Headless
editor PID 9492 launches separate game PID 35252.
Before admission, the same current fixture freshly passes managed corruption,
blocked unload/retry, native missing/invalid library recovery, method signature
changes, rejected direct/extension/ancestor base changes and class removal with
compatible repair. Counters, identities, parent/vector/reference state,
dynamic callables, signal/delegate counts and managed lifecycle hooks survive.

The live stage configures both authority and client outbound simulators with
30 ms latency, 5 ms jitter and 5 percent loss. While connected, deliberately
failed C# and C++ builds retain the preceding live code; the failed C++ build
must leave the published descriptor unchanged. Separate C# reload, separate
C++ reload and combined reload then replace executable method versions. The
C++-only stage must leave managed deserialization count unchanged; the C# and
combined stages must advance it. Final native method version is 5 and managed
method version is 7.

| Checkpoint | Authority tick | Entity revision | Language-driven client polls | C++/C# callbacks |
| --- | --- | --- | --- | --- |
| initial | 33 | 1 | 70 | 1/1 |
| managed-compile-failure | 128 | 2 | 260 | 2/2 |
| native-compile-failure | 220 | 3 | 444 | 3/3 |
| csharp-reload | 424 | 4 | 846 | 4/4 |
| cpp-reload | 656 | 5 | 1310 | 5/5 |
| combined-reload | 947 | 6 | 1890 | 6/6 |

All six checkpoints retain the same game PID, native authority/client ObjectIDs,
bound port, authenticated peer handle 257 and entity handle
1. The only client state history is
`Connecting -> Synchronizing -> Connected`; no stop, disconnect or readmission
is permitted, and no authority diagnostics occur. Native session references in
both languages' serialized dictionaries remain identical after reconstruction.
The fixed clock, entity revision and language pumps advance at every checkpoint.

Each checkpoint exchanges one application payload in each direction. Server
receives `0100ff2a` through `0600ff2a` from the same authenticated peer; client
receives `8100ff2a` through `8600ff2a` from native server peer 0. Callback payload,
sender and cumulative counts must match exactly; native-session method-name
Callables into C++ and C# each fire once per payload. The same replicated entity
receives fresh exact bytes and owner metadata at every stage. Raw transport/
ownership metadata does not authorize opaque gameplay messages.

Reproduce using a new output directory:

```powershell
python misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery --network-live-reload --network-latency-ms 30 --network-jitter-ms 5 --network-loss-percent 5 --output .build/integration-live-reload-qualified
```

Latency and jitter accept finite values in 0–5000 ms; loss is explicitly a
percentage in 0–100, applied to each configured outbound simulator. These
options require live-reload mode. Stopped-authority recovery and runtime-disabled
modes use separate isolated fixtures. Ten invalid-option tests reject conflicting
modes, simulation options outside live mode, nonfinite values and invalid ranges
before creating an output directory.

Final fixture C++ DLL SHA-256 is `130b67973598d32800d0a0d7b722467d236dfc7d63847b200dabd21b880f0565`; managed assembly is `b2fd80fd4cb0359af4049896876df3851409ebb5974afb1c861e330d5d58ac49`.
The receipt records source/input/runtime and generated artifact hashes, editor/
game identities, commands, compile diagnostics and samples. Native editor remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`, compiled from
`4d64b38c554ab3dc491285f4ffa5119c001da56f`; matching Debug SDK/ClassDB/glue
identities remain as recorded above. No duplicate engine build was required.

Current stopped-authority regression passes at
`.build/integration-live-reload-stopped-regression/1791302442657405100/receipt.json` with explicit fresh admission and
retired-handle rejection. Current runtime-disabled regression passes at
`.build/integration-live-reload-default/1791302480570457100/receipt.json` with a non-collectible separate player.
`.build/integration-live-reload-tool-checks/receipt.json` passes 29 semantic
tests (14 stopped recovery and 15 live reload), ten invalid CLI cases, help,
Ruff/format and mypy, retaining the existing Python 3.9 configuration warning.
New negatives reject reconnect/replaced entities/sessions, ignored simulation,
connection interruption, authority faults, lost/corrupt replies, duplicate
managed callbacks, stale baselines and stalled clocks/revisions/language pumps.

`.build/integration-live-reload-publication.json` verifies original handoff
ancestry, canonical/remote master equality after normal publication, all 86
installed artifacts, all actual seven EGP worktrees, preserved foreign patches/
untracked bytes and no open PRs. The unchanged 197-check editor/Debug/Release
language builds, 36 networking semantic tests, native 120-check/ten-case suites,
seven physics lab cases and 72 admission cases remain bounded to verified
unchanged source/binary inputs. Changed reload fixtures use the three fresh
runs above. Other worktree leftovers stay preserved; the four original engine
chats remain completed and documentation/website keeps separate ownership.

Next: independent-process low-level fault/reload, deliberately in-flight callbacks
and concurrent reload, physics checkpoint recovery during reload, managed facade/
event-closure persistence, production admission/retry, hard outages/crashes,
larger worlds and sustained WAN/scale/soak. This is Windows Debug with one local
authority/client pair sharing the game process. Configured loss does not measure
actual dropped packets or real WAN behavior; poll/tick counts are fixture health
evidence, not performance. Arbitrary application/ABI state, physics rollback,
exported runtime reload and platform parity remain open. The loop stays ACTIVE.


### Box3D references and checkpoint state across network reload — 2026-10-06

Source `c026d7bec396fbbb386965f5049645d6350408c2` adds opt-in `--network-physics` to live-reload
and stopped-authority recovery fixtures. It is committed on canonical master
above `4d43a101cf6a55e41c753720b089f348f72e58d2`. Core/native physics/networking,
ClassDB, SDK/glue/public helper APIs and installed binaries remain unchanged;
this increment adds combined qualification and fixture options.

The explicit native Box3D world uses 60 Hz, four substeps, one worker and gravity
(0, -9.8, 0), with one dynamic box at stable body ID 10000. The authority and
client use that world's simulation fingerprint. A fixture GDScript callback
steps the world once per native authority simulation tick and publishes body
position/velocity and physics tick in its own bounded opaque baseline codec.
The network entity maps to body 10000 independently of its replaceable handle.
Snapshots are trusted local bytes; peer payloads never become solver snapshots.

Both live C++ and C# objects retain the world alongside native session references
in serialized dictionaries. C++ generated EGPBox3DWorld bindings and C# native
GodotObject calls return the current position/rotation/linear/angular velocity,
tick and state hash. The fixture requires both language views to match the same
world exactly after reconstruction, including native world ObjectID and body
mapping/count. This does not qualify arbitrary managed facade or closure state.

The full live-physics run passes at `.build/integration-physics-live-reload-qualified/1791303106337802600/receipt.json`.
Headless editor PID 31480 launches game PID 29672.
Full managed corruption/unload and native missing/invalid DLL/signature/base/
ancestor/class recovery freshly pass first. The live pair then retains its
connection through failed C#/C++ builds and separate C#, C++ and combined reload,
with both outbound simulators configured for 30 ms latency, 5 ms jitter and
5 percent loss. Each checkpoint checks the same native world/body, exact
language body state, advancing client baseline, uninterrupted admission,
callback counts and bidirectional application bytes.

| Checkpoint | Network tick | Physics tick | Client physics tick | Authority body Y |
| --- | --- | --- | --- | --- |
| initial | 33 | 33 | 27 | 9998.5068359375 |
| managed-compile-failure | 133 | 133 | 126 | 9975.8779296875 |
| native-compile-failure | 225 | 225 | 222 | 9931.0185546875 |
| csharp-reload | 428 | 428 | 420 | 9750.525390625 |
| cpp-reload | 669 | 669 | 666 | 9390.603515625 |
| combined-reload | 995 | 995 | 991 | 8652.15234375 |

Live physics tick equals the authority network tick; one clock event produces
one world step. The dynamic body's Y falls at every stage and the received
baseline tick stays at or behind the authority. These are bounded fixture
health/correctness observations, not timing or performance qualification.

Fresh stopped-authority checkpoint recovery passes at
`.build/integration-physics-reload-stopped-qualified/1791303281913433300/receipt.json`. Editor PID 29060
launches game PID 8968. Immediately before the 550 ms authority stall,
the fixture captures local checkpoint tick 22, solver
state hash `21a31bd979e10a65` and Y 9999.333984375.
Both native sessions stop and the client clears its obsolete physics baseline.
C++/C# reload while stopped preserves the identical world ObjectID, body 10000,
tick, hash and position; it does not restart the authority or advance the world.

Before explicit fresh admission, the fixture advances the world one step as a
rollback control, then corrupts a copy of the checkpoint. Restore must return
`ERR_FILE_CORRUPT` (16) and leave the changed
world's hash/tick unchanged. Restoring the original trusted bytes must recover
exact tick 22, hash `21a31bd979e10a65` and Y
9999.333984375. The saved snapshot file SHA-256 is
`1dc88dccd116d5852cd9703f722f07c63b9fe54e8b842b74dddbf80c8836fa11`.

After fresh admission, the new entity maps to the same body 10000, while retired
peer/entity handles remain rejected. Physics resumes at world tick
39 and client baseline tick 38; the offset
invariant is `world_tick = checkpoint_tick + restarted_network_tick`.
The fresh received physics baseline must advance beyond the checkpoint, and
the body's Y must be below its checkpoint position. This is explicit trusted
authority restoration; it is not automatic client prediction or rollback.

Reproduce each physics mode in separate fresh output directories:

```powershell
python misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery --network-live-reload --network-physics --output .build/integration-physics-live-reload-qualified
python misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --network-recovery --network-physics --output .build/integration-physics-reload-stopped-qualified
```

The live receipt records frozen source/fixture hashes, executable/runtime/SDK
identities, all generated extension/managed binaries, samples, commands,
process identities and diagnostics. Final live C++ DLL SHA-256 is `7cc967c9f8b316b579824e0c7a3e24e20cffb7cdd5c00852616501802807630f`;
C# assembly is `15b791de94196387e5f0eb7ece487cc73aa0d0bd4cdcfe9f2511ee8d53eb1ece`. Editor remains
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`, compiled from
`4d64b38c554ab3dc491285f4ffa5119c001da56f`. No duplicate engine build was needed.

Physics-off live reload freshly passes at `.build/integration-physics-reload-option-off/1791303319361621900/receipt.json`;
the current runtime-disabled baseline passes at
`.build/integration-physics-reload-default/1791303366800580300/receipt.json`. The focused successful precursor
is `.build/integration-physics-reload-initial/1791302979650546800/receipt.json`.
`.build/integration-physics-reload-tool-checks/receipt.json` passes 47 semantic
tests (29 networking/reload and 18 physics), eleven invalid CLI checks, help,
Ruff/format and mypy with the existing Python 3.9 configuration warning.
Physics negatives reject missing/replaced worlds/bodies, incorrect language
views, lost clock offsets/profiles, nonfinite state, stale/future client ticks,
stalled live physics, accepted corruption, changed state on failed restore and
inexact restored hash/tick/position. The physics flag requires a networking mode.

`.build/integration-physics-reload-publication.json` gates normal publication:
original handoff ancestry, exact canonical/remote master equality, 86 unchanged
installed artifacts, all seven actual worktrees, preserved foreign patches and
untracked bytes, no open PRs and completed original engine chat handoffs.
Unchanged 197-assertion language runs, 36 networking semantic tests, native
120-check/ten-case suites, seven physics lab cases and 72 admission cases keep
their bounded source/binary evidence. Changed reload fixtures use the four
fresh current runs above; documentation/website publication stays separately
owned. Other worktree leftovers remain preserved, not declared clean or merged.

Next: independent-process low-level fault/reload, in-flight callbacks/concurrent
reload, managed facade/event-closure persistence, automatic client prediction/
rollback, production checkpoint/admission/retry, hard outages/crashes, larger
worlds and sustained WAN/scale/soak. This gate uses one local Windows Debug
authority/client pair sharing a game process and one explicit native body,
with a fixture stepper/codec. Configured loss does not quantify actual drops
or real WAN behavior. Exported-runtime reload, arbitrary game/ABI state,
platform parity and performance remain open; the loop stays ACTIVE.


### Public C# session ownership and event handoff across reload

API source `1eb93efec9f982b4e52d90f9becbe97e0d10ec26` and connection-count gate
source `a81045721e45dbc80debe621d15cd7c49c10fb5e` are committed in canonical master. `NetSession.DetachForReload()`
disconnects managed bridges and returns an exported-dictionary capsule while the
native session remains alive. The old wrapper becomes disposed; disposing it later
does not close the transferred session. `NetSession.ResumeAfterReload()` consumes
the capsule, restores the native session wrapper and reconnects its typed event
bridges. Applications resubscribe their own handlers in `OnAfterDeserialize()`.
The module README includes a complete hook example and same-Godot-thread contract.
Schema/type/native-class/claim validation rejects malformed, foreign and consumed
capsules without modifying them; native metadata prevents copied capsules from
claiming the same session again. These are trusted local references, not a disk
checkpoint or admission format. Mirrored sample helpers include the signal bridge.

The investigation retained three precursors:
`.build/integration-csharp-facade-initial/1791304217839876800/receipt.json`
failed a fixture assertion because an unconfigured session was polled as if
configured; a standalone retained game log reproduced that exact assertion.
`.build/integration-csharp-facade-repaired/1791304447830653500/receipt.json`
also exposed signed GDScript versus unsigned C# instance-ID formatting.
Its game log revealed stale delegate callbacks even though messages arrived.
`.build/integration-csharp-facade-native-callable/1791304653049429700/receipt.json`
passed the old message-count gate but retained stale callable errors; it is
explicitly superseded as cleanup evidence. Keeping a native callable Variant
alone did not solve delegate hash/handle changes during teardown. The final helper
uses a private RefCounted bridge with named methods and disconnects it before
rebuilding ownership. Its public C# events still use ordinary typed delegates.
The debugger capture now returns its required bool immediately and sends the
asynchronous network proof later. Each run retains its own game log and hashes.

Current acceptance evidence:

| Check | Receipt and observed result |
| --- | --- |
| Full live connection, failed compiles, C#-only, C++-only and combined reload | `.build/integration-csharp-facade-live-connections-qualified/1791305291957002300/receipt.json`; editor PID 11472, game PID 35016; native session/peer/entity identities remain stable, six exact application payloads each direction, facade callback counts 1..6, handoff/restore histories `[0, 0, 0, 1, 1, 2]` |
| Full stopped-authority fault and explicit recovery | `.build/integration-csharp-facade-stopped-qualified/1791305420873104600/receipt.json`; editor PID 22832, game PID 27144; 66 continuing client polls during 566 ms authority gap; stopped-session handoff is restored before explicit fresh admission; peer 257 -> 513, entity 1 -> 2 |
| Runtime default | `.build/integration-csharp-facade-default/1791305558887462700/receipt.json`; non-collectible default remains unchanged |
| Optional facade/physics disabled | `.build/integration-csharp-facade-option-off/1791305690135549500/receipt.json`; standard dynamic-native live fixture passes with empty facade/physics evidence |
| Public helper compatibility and exports | `.build/integration-csharp-facade-languages/receipt.json`; fresh 27 stages, 197 assertions each in editor PID 27336, relocated Debug PID 17556 and Release PID 16292; independent high-level C#/C++ process fault cycles also rerun |
| Evidence semantics and options | `.build/integration-csharp-facade-connections-tool-checks/receipt.json`; 64 rejection/acceptance tests, 12 invalid CLI cases, help, Ruff, formatting and mypy pass (existing Python 3.9 configuration warning retained) |

Both facade fixtures execute 23 actual managed checks: null/missing/malformed
capsules, full-width unsupported version, forged claim, foreign native class,
consumed capsule and copied claim; detached wrapper access/second detach; rejected
state preservation; native identity, old-wrapper disposal, event disconnect and
one fresh callback. Native signal connection counts are checked for all seven
session signals at every checkpoint, including quiet signals. Server counts are
state 1, peer connected/disconnected 1 each, application 4, packet 1, simulation 2
(facade plus physics stepper), diagnostic 2; client counts are state 2, peers 1
each, application 2, packet 1, simulation 1, diagnostic 1. Counts remain constant
through failed compiles and both managed handoffs. The current game logs reject
stale delegate/ManagedCallableMiddleman errors, script errors and asynchronous
capture return-type errors. Deliberately injected assembly/ABI failures retain
their expected diagnostics and successful repair proofs.

Live Box3D ticks `[33, 133, 246, 452, 688, 1000]` and received physics ticks
`[27, 126, 242, 448, 679, 996]` advance under the same native world.
Stopped recovery rejects corrupt checkpoint data with error
`16` without changing the mutated world; valid
restore exactly returns tick `22` and solver hash
`21a31bd979e10a65` before explicit fresh admission and advancing baseline.

Commands (Python is the bundled runtime used in these receipts):

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-facade-live-connections-qualified --network-live-reload --network-physics --network-csharp-facade --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-facade-stopped-qualified --network-recovery --network-physics --network-csharp-facade --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-facade-option-off --network-live-reload
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-facade-default --disable-runtime
& $egpPython misc/scripts/validate_egp_net_languages.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --sdk C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/sdk/4bc13481314e7023 --sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib --release-sdk-library C:/Users/Rose-X/AppData/Local/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Release/egp_godot_cpp.lib --packages bin/GodotSharp/Tools/nupkgs --template bin/godot.windows.template_debug.x86_64.mono.exe --release-template bin/godot.windows.template_release.x86_64.mono.exe --output .build/integration-csharp-facade-languages
```

No native rebuild: editor SHA `20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342` and compiled native source
`4d64b38c554ab3dc491285f4ffa5119c001da56f` remain pinned with the exact SDK/API,
matching Debug/Release archives and templates. Fresh live reload native DLL SHA
`53dc859fbf43a2343aa83f4f92ed5fb3ad1311abd95612a0386bd5a64c2ac622` and managed assembly SHA `101308f2ba87e8c0f076a6156a22062ca3593293f2cb76fced9314575ffd4b1b` qualify these fixture sources.
Fresh trilingual native Debug/Release DLLs are `cb7fe8b3f3c31e8c790cd8485eaa214902d117628e2c6c251810b526fb90ea8c` /
`8c2e914d956e9a8f8ae1b7fdad09b08da40a4ba011d2ba6b7361f5416ffaf5ae`; C# assembly is `add5ed8dc4083f83dabee8696eaf14397d8a0b9a7847f340f5c0aa0154287313`.
All helper source, runtime, generated artifacts and game/build log hashes are
recorded in the receipts. The language manifest is frozen at API commit 1eb93efec9;
subsequent changes add only hot-reload evidence checks and this checklist.

`.build/integration-csharp-facade-publication.json` is the new normal-publication
gate. It verifies original handoff ancestry, canonical/remote master equality,
all seven actual worktrees, unchanged foreign tracked patches/untracked bytes,
no open PRs, completed original engine chat handoffs and 86 unchanged installed
artifacts. Native 120-check/ten-test suites, 72 admission cases and seven
GDScript-only physics lab cases retain their unchanged executed-input evidence.
The old physics manifest's unused C# helper hash is superseded by the fresh
language/reload builds above. The old C++ NIL/poll control stays pinned to its
actual unchanged source/DLL; the newly built DLL has its own recorded identity.
Historical worktree leftovers remain preserved, not declared clean or merged.
Documentation/website publication is separately owned and receives this qualified
source after normal push.

Next: high-level Net/NetNode/NetBox3D ownership handoff, application handler/state
policies, independent-process low-level faults and reload, concurrent/in-flight
reload, automatic client physics prediction/rollback, production admission and
checkpoint delivery, exported-runtime reload, larger worlds, WAN/scale/soak and
platform/performance gates. This evidence uses one local Windows Debug
authority/client pair and one Box3D body per game. Configured loss does not
quantify real packet drops or WAN performance. Arbitrary ABI/game/closure state
is not covered; full feature acceptance and AAA readiness remain open and the
loop stays ACTIVE.


### 2026-10-06 — high-level C# NetNode reload and forwarding lifecycle

The public helper and its sample mirror now implement serialization hooks using
named Godot object methods for all eleven codec signals. Serialization disconnects
forwarding without closing the codec or native session, then reconnects the same
codec child. The owner's ordinary C# events explicitly resubscribe after reload;
registered message handlers use named Godot methods. Derived NetNode serialization
overrides must call base. Tree exit closes the session before disconnecting
forwarding; reentry reconnects the existing codec child exactly once. Starting a
new session after reentry remains an explicit host/join operation. Replacing a
freed codec child also restores forwarding and the AutoPoll policy.

The preserved legacy control `.build/integration-csharp-node-exit-control/1791306878199218400/receipt.json`
passed all six live traffic phases but failed because tree exit left all eleven
signal connections attached. Its source, generated binaries and logs remain
recorded. An earlier control's packed-byte JSON representation failed the reader;
that was a fixture formatting error, corrected to byte-exact hex evidence.

Current API/fixture source commit: `f408f1eabc38b0db38bfa532784713fd2cd2b08c`. These final receipts
replace the intermediate f5c28f7306 checks; six managed runtime checks also verify
freed-child replacement, callback counts, packet contents/channel/delivery and
retained manual poll policy.

| Check | Exact evidence |
| --- | --- |
| Full live C#/C++ repair and high-level reload | `.build/integration-csharp-node-live-final/1791307825361109600/receipt.json`; editor 19052, game 2944; six message/handler/owned-input/raw-packet exchanges each direction; unowned input rejected; native/codec/node IDs and admission retained; restores [0, 0, 0, 1, 1, 2] |
| Full stopped-authority fault and recovery | `.build/integration-csharp-node-stopped-final/1791307666927699400/receipt.json`; editor 21024, game 12580; 64 continuing client polls during 550 ms gap; native sessions retained; explicit peer 257 -> 513, entity 1 -> 2; retired handles return 33 |
| Tree exit/reentry | Both final node receipts: all eleven connections exactly 1 before exit, 0 after exit and 1 after reentry; same node/codec IDs and one child; closed sessions and zero peer/entity caches |
| Low-level facade and Box3D regression | `.build/integration-csharp-node-low-regression/1791308112944124900/receipt.json`; current fixture retains native sessions, facade handoff/events, all seven native connection counts and authoritative world across six live phases |
| Runtime default regression | `.build/integration-csharp-node-default/1791308181535905100/receipt.json`; non-collectible default unchanged |
| Trilingual/exported helper compatibility | `.build/integration-csharp-node-languages-final/receipt.json`; fresh 27 stages, [('interop', 33160, 197), ('export-interop', 32664, 197), ('export-interop-release', 24212, 197)]; each configuration passes 197 assertions, including independent high-level C#/C++ process fault cycles |
| Reader/options/tools | `.build/integration-csharp-node-tool-final/receipt.json`; 208 semantic tests, 15 invalid CLI cases, help, Ruff/format/mypy pass; existing Python 3.9 configuration warning retained |

Both full fixtures first reject and recover corrupted managed assemblies,
blocked unload and incompatible/missing native classes on their generic live
reload probes, then create the network nodes. The high-level live node phases
cover failed managed/native compilation and C#-only, C++-only and combined reload;
the stopped node phase covers combined reload before explicit fresh admission.
Corrupt-assembly, blocked-unload and rejected ABI repair while those network
nodes are already authenticated remain an additional acceptance item.
Game logs retain the deliberately injected diagnostics and reject stale managed
callables, script errors and invalid asynchronous debugger capture returns.
Live authority ticks [47, 133, 229, 440, 680, 991] and baseline revisions
[1, 2, 3, 4, 5, 6] advance without reconnect. Both outbound simulators
are configured to {'simulated_latency_ms': 30, 'simulated_jitter_ms': 5, 'simulated_loss': 5}; this does not measure actual packet
drop rates or WAN performance.

Exact commands:

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-node-stopped-final --network-recovery --network-csharp-node --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-node-live-final --network-live-reload --network-csharp-node --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-node-low-regression --network-live-reload --network-csharp-facade --network-physics
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-csharp-node-default --disable-runtime
```

Trilingual command is the preceding entry's exact SDK/template command with
`--output .build/integration-csharp-node-languages-final`; its complete argv,
20 input hashes and matching SDK archives are in `source.json` there. Engine SHA
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342` and compiled native source
`4d64b38c554ab3dc491285f4ffa5119c001da56f` remain unchanged; no native rebuild.
Fresh trilingual Debug DLL `2af18ef391c75ba7bb149b03fbb56f78208ec9ac132b7382f2eb40aa4dde24dd`, Release DLL
`297a47ef8acb5dcdfb73421428a96239a9b7a39102dbda6c376129b1fbb5f966` and C# assembly `2ae987788774deb1701018fc32e2d31e873f13f04859c044b6eee8f10d7be731`
qualify the changed external helpers. Every final reload receipt retains helper,
fixture, runtime, generated assembly/DLL and game/build log hashes.

`.build/integration-csharp-node-publication.json` verifies canonical/remote master
equality after normal push, original handoff ancestry, seven actual worktrees,
unchanged foreign tracked patches and untracked bytes, zero open PRs and 86
unchanged installed artifacts. Native 120-check/ten-test suites, 72 admission
cases and seven GDScript-only physics lab cases retain their unchanged executed
inputs. Unused historical C# manifest entries are replaced by these fresh helper
builds. Foreign worktree leftovers remain preserved and accounted for, not
declared clean or universally merged. The original four engine chats are completed;
documentation/website receives a separate qualified handoff.

Next: high-level C++/NetBox3D adapter ownership, explicit application/physics
restoration policy, traffic after tree reentry, independent-process low-level
fault/reload, concurrent/in-flight and exported-runtime reload, automatic client
physics prediction/rollback, arbitrary ABI/game/closure state, production
admission/retry/checkpoint delivery, larger worlds and WAN/scale/soak/platform
performance. One local Windows Debug authority/client pair shares the game
process during reload. Full feature acceptance and AAA readiness remain open;
the loop stays ACTIVE.


### 2026-10-06 — retired native codec callbacks and fresh traffic after tree reentry

The shared GDScript codec now uses seven named native signal callbacks and
disconnects them when close releases its native session. GDScript, C# NetNode and
C++ Net all use this codec. Stop keeps its configured native session and callbacks
for explicit listener/client restart. Close and stop also check session identity
after state callbacks so a reentrant callback that closes and configures a fresh
session does not have that new session or cache cleared by the outer operation.
The shared helper and both GDScript/trilingual sample mirrors match exactly.

The isolated legacy control `.build/integration-native-callback-close-control/1791308718803167500/receipt.json`
reproduced the bug after successful fresh admission: all seven retired native
callbacks were still attached. Injecting a local stale packet/state signal changed
the new session's typed packet count and cleared its codec entity cache to zero.
This is a lifetime test using a retained native reference, not a remote attack
or WAN security claim. The failing generated code, assemblies/DLLs and logs are
preserved. Repaired focused runs also remain separate from final qualification.

API and fixture commit `8198c7159660075d39e7981a956c68a6b8d561e8` is followed by
sample-mirror commit `59bc47dcee2e8b0fc9d79aec463ec90b49542460`. The stopped receipt was captured
before the mirror commit; all of its executed helper/fixture inputs are identical
and the default GDScript sample below executes the updated mirror. No native
engine rebuild: compiled native source `4d64b38c554ab3dc491285f4ffa5119c001da56f` and
engine SHA `20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342` remain pinned.

| Check | Exact evidence |
| --- | --- |
| Full live C#/C++ reload and three fresh-session cycles | `.build/integration-native-callback-live-qualified/1791309289155550500/receipt.json`; editor 34604, game 33668; original six live phases pass, then reentry sequences [7, 8, 9] with new native session IDs and retained node/codec IDs |
| Full stopped-authority repair and three fresh-session cycles | `.build/integration-native-callback-stopped-qualified/1791309154280188700/receipt.json`; editor 14988, game 32356; 66 client polls during 566 ms fault; explicit stopped-session recovery, followed by reentry sequences [3, 4, 5] |
| Native and high-level callback ownership | Every reentry checkpoint verifies all seven retired native callbacks are zero, all seven new native callbacks are exactly one, all eleven typed forwarding connections are exactly one, one retained codec child and zero closed peer/entity caches |
| Managed runtime lifecycle checks | 18 checks per high-level fixture prove replacement from both close and stop state callbacks preserves a fresh listening session and its spawned entity; the previous six freed-codec replacement checks also pass and survive managed reload |
| Low-level facade/Box3D and runtime default | `.build/integration-native-callback-low-regression/1791310317978790600/receipt.json` and `.build/integration-native-callback-default/1791310418152448000/receipt.json`; fresh live facade/world regression and non-collectible default pass |
| Trilingual editor and relocated exports | `.build/integration-native-callback-languages/receipt.json`; fresh 27 stages, [('interop', 29292, 197), ('export-interop', 28140, 197), ('export-interop-release', 9308, 197)]; independent high-level C#/C++ process fault cycles also pass |
| Token admission | `.build/integration-native-callback-admission-qualification/receipt.json`; 72 native/GDScript cases across editor/Debug/Release with configured/generated keys, same/cross-second token creation and graceful/clock failure |
| Physics/network lab | `.build/integration-native-callback-physics-qualification/receipt.json`; seven fresh editor, packaged visible listen-host/dedicated multi-client, impaired/stalled/checkpoint/owner-input cases and controls; default sample `.build/integration-native-callback-physics-default/receipt.json` also passes |
| Evidence and options | `.build/integration-native-callback-tool-checks/receipt.json`; 293 semantic tests, 15 invalid CLI cases, help/Ruff/format/mypy pass; existing Python 3.9 configuration warning retained |

Fresh-session reentry explicitly reapplies options, hosts on the released port,
issues a fresh token for account 424242, joins, spawns a new owned entity, and
checks exact typed messages/handler payloads, owned input, unowned-input rejection,
raw packet counts and advancing native ticks. Registered message handlers remain
on the retained codec. The three successive new sessions are distinct from every
retired session. Numeric peer/entity IDs may repeat across different native
sessions; ownership commands and saved handles must include their issuing session
identity. This differs from monotonic handle generations when restarting a retained
native session after a fixed-clock fault.

Generic corrupt-assembly, blocked-unload and rejected ABI repair still precedes
network-node creation. Live networking phases test failed managed/native builds
and compatible C#-only/C++-only/combined reload; stopped phases reload before
explicit fresh admission. Those injected generic failures while an authenticated
NetNode is active remain open. Application C# events explicitly resubscribe;
arbitrary closures or game state are not automatically persisted.

Commands:

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-native-callback-stopped-qualified --network-recovery --network-csharp-node --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-native-callback-live-qualified --network-live-reload --network-csharp-node --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-native-callback-low-regression --network-live-reload --network-csharp-facade --network-physics
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-native-callback-default --disable-runtime
```

The complete SDK/template trilingual argv is recorded in
`.build/integration-native-callback-languages/source.json`. Exact seven lab and
three admission argv arrays are in their qualification receipts. Current shared
helper bytes require fresh executed-input evidence: the 72 admission cases, seven
physics/lab cases and default GDScript sample above replace the historical reuse
entries. Native 120-check/ten-test suites, ClassDB/SDK/glue and 86 installed
artifacts keep their unchanged bounded evidence. Fresh trilingual Debug DLL
`ecc31bd6c25643bd3e6818c6698fdc11c00fe006418504b700b6414309c4e976`, Release DLL `a8baaa98e609d64eac331acb9c8cf6e0e791b9f65159c599b93f7c24c5c618fc` and C#
assembly `2b1acbea33f104f8bfc9dc8e9e04be8fbf73b23546229bd52259f2ba71ad268f` are recorded with fixture/runtime/log hashes.

`.build/integration-native-callback-publication.json` validates normal publication,
canonical/remote master equality, original handoff ancestry, all seven actual
worktrees, foreign tracked/untracked bytes, no open PRs, matching sample mirrors
and the combined evidence above. The temporary docs source worktree used earlier
in this run belonged to the separate docs owner and was archived after its
b7c02a7275 publication. Foreign historical leftovers remain preserved and
accounted for, not declared clean or universally merged. A new qualified source
handoff follows normal push; the integration loop remains ACTIVE.

Next: authenticated-node assembly/unload/ABI failures, high-level C++/physics
adapter ownership, arbitrary in-flight callback/lifecycle mutations, low-level
independent-process fault/reload, concurrent/exported-runtime reload, automatic
client physics prediction/rollback, production admission/retry/checkpoint delivery
and platform/scale/soak/performance. Configured impairments do not quantify packet
loss or WAN performance; full feature completion and AAA readiness remain open.


### 2026-10-06 — public C# Box3D adapter ownership across reload

`NetBox3D.DetachForReload()` and `ResumeAfterReload()` now transfer ownership of
one existing GDScript adapter. The adapter stays attached to its existing codec,
world and stable entity-to-body map. Named RefCounted signal methods replace
managed delegate callables; the old wrapper disconnects its three managed links
before assembly unload. Its methods reject use after transfer, and disposing it
again does not detach the resumed owner. Resuming consumes one local capsule;
invalid, foreign, malformed, copied-after-consumption and future-version capsules
are rejected without mutation. The resumed wrapper still requires explicit
application event resubscription. Ordinary disposal detaches; the caller retains
ownership of the world and network node. This is not a disk/network state format
or automatic client rollback. The public README includes an ownership example.

API/mirror commit `9098bb5569c9af1eb04c06e0bc3f0044194f6791` is followed by fixture/diagnostic commit
`bf68b565a1bc0f42e341908e60b43b6e1966fc39`. The live receipt started before the latter commit was made;
its frozen executed inputs are byte-identical to the committed fixture. The
stopped receipt and fresh trilingual inputs are pinned to that fixture commit.
Both source/mirror C# helpers and the new signal class match exactly.

| Check | Exact evidence |
| --- | --- |
| Public adapter, six live phases | `.build/integration-box3d-ownership-live-qualified/1791311872001952600/receipt.json`; editor 4372, game 2592; world ticks [46, 143, 248, 469, 719, 1037] and managed transfers/restores [0, 0, 0, 1, 1, 2] |
| Stopped-authority reload and explicit admission | `.build/integration-box3d-ownership-stopped-qualified/1791312022427911200/receipt.json`; editor 20336, game 7828; world ticks [18, 25, 25, 44] and transfers/restores [0, 0, 1, 1] |
| Retained world, body and clock | Same adapter/world IDs, one stable body 10000, C++/C# world references/tick/hash/position reads, one before/after event per completed world tick, three managed connections exactly one, one adapter clock connection and zero adapter failures at every checkpoint |
| Explicit tree lifecycle and fresh sessions | Two exit/reenter checkpoints plus three fresh admission cycles per fixture; stable adapter/world, stopped tree physics unchanged, new entity remapped to body 10000, advancing replicated client physics and exact high-level/retired-native callback counts |
| Capsule/lifecycle self-checks | 22 actual C# capsule checks, six codec replacement checks and 18 reentrant close/stop state checks per fixture; malformed/future/foreign/replayed ownership rejected, old disposed wrapper cannot disconnect resumed ownership |
| Editor and relocated Debug/Release | `.build/integration-box3d-ownership-languages/receipt.json`; 27 fresh stages and 197 assertions in each configuration; independent C#/GDScript/C++ request/reply and six independent high-level C#/C++ fault runs pass |
| Low facade/world and runtime default | `.build/integration-box3d-ownership-low-regression/1791312260385920400/receipt.json` and `.build/integration-box3d-ownership-default/1791312314664724100/receipt.json`; fresh executed fixture inputs replace earlier runner/game sequencing evidence |
| Evidence/options | `.build/integration-box3d-ownership-tool-checks-qualified/receipt.json`; 662 semantic reader tests (293 previous plus 369 adapter/tree cases), 18 rejected CLI combinations, help/Ruff/format/mypy pass |

The isolated controls found a test harness boundary problem: debugger captures
can interrupt GDScript inside a native poll/physics tick. A direct test update
could be overwritten by the unfinished tick's already-copied entity state; direct
tree exit could interrupt a step between before/after events. The harness now
defers network commands/snapshots and native reload requests until that call stack
returns. It also waits for both native test receivers to receive all three test
application packets per sequence, including the rejected ownership packet. Exact
immediate authority, native and client application sequence checks remain enabled.
`.build/integration-box3d-ownership-controls.json` retains the three finalized
failing runs and one incomplete interrupted diagnostic with source/binary/log
hashes. Deferred fixture commands qualify safe-boundary tests; arbitrary
application mutation or reload inside synchronous callbacks remains open.

Commands:

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-box3d-ownership-live-qualified --network-live-reload --network-csharp-node --network-csharp-box3d --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
& $egpPython misc/scripts/validate_egp_hot_reload.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --output .build/integration-box3d-ownership-stopped-qualified --network-recovery --network-csharp-node --network-csharp-box3d --assembly-recovery --unload-recovery --native-recovery --native-abi-recovery
```

The matching SDK/template trilingual argv and 21 frozen source inputs are in
`.build/integration-box3d-ownership-languages/source.json`. Fresh Debug extension
`0352845e965aa5df8fb3e3debba4f977474eaef9586c8290bd61b2e20ac54f09`, Release extension `6ce96b5342d574c02f1c08089e1a9bf840f5351835617278855366931fe01bd3`
and C# assembly `3d8d42aa0922e39bee4954b71f8a58fc78a6be7dc10a2482671a1a81cfda79ea` accompany generated/runtime/log hashes.
The live profile remains configured 30 ms latency, 5 ms jitter and 5 percent loss
on both outbound simulators; this does not count actual drops or quantify WAN
performance. The authority/client pair shares one local Windows Debug game
process during reload. Generic corrupt assembly/unload/ABI recovery still occurs
before authenticated node creation; those injected failures during an active
network node remain open.

Native source `4d64b38c554ab3dc491285f4ffa5119c001da56f`, engine SHA
`20be5396d78b4c9873d4a355132f62366be58fcf9b1519bb595006fb07e74342`, 86 installed
artifacts, ClassDB/SDK/glue and the 120-check/ten-test native suites remain
unchanged. Seven earlier GDS-only physics/lab cases, their default sample and 72
admission cases retain byte-identical executed inputs. Their unused historical
C# Box3D manifest entry is superseded by the fresh C# builds/adapter qualification;
none of those GDS lab/admission runs executes a C# helper.

`.build/integration-box3d-ownership-publication.json` checks committed canonical
source/normal remote equality, original handoff ancestry, all seven worktrees,
foreign tracked/untracked bytes, no open PRs, helper mirrors and the combined
fresh/reused evidence. Current four original chats are completed; the independent
docs owner finished its a5146052e8 builds/deployments and is handed the new
qualified source after publication. Foreign historical leftovers remain preserved
and accounted for, not declared universally clean or merged. The loop stays ACTIVE.

Next: authenticated-node assembly/unload/ABI failures, high-level C++ adapter
ownership, arbitrary in-flight callback/lifecycle mutation, low-level independent
process fault/reload, concurrent/exported-runtime reload, automatic client physics
prediction/rollback, production admission/retry/checkpoint delivery and wider
platform/scale/soak/performance. Full feature acceptance and AAA readiness remain
open.

## 2026-10-06: qualified upstream consolidation and binary scene export repair

Canonical `master` now contains upstream snapshot
`3ea0cf3e72699c5e3b35f7956670ac93b9d1d4a0`, including all 16 commits that were
previously incoming. Merge `95daeae78e` integrates qualified source
`7b57a3b3140cb1c8b0bbcfb6bbe2c1f4eab70161`; merge `5e9239d70b` integrates the
two subsequent compile-time zstd compatibility commits. The README replacement
rationale and the requested removal of obsolete networking development history
are preserved. Source publication and exact remote equality are recorded in
`.build/integration-upstream-publication.json` after the normal push.

The editor is compiled from `7b57a3b3140cb1c8b0bbcfb6bbe2c1f4eab70161`; the
Debug and Release templates are compiled from
`c6a6920685b844ff0ba30d2e794117b776edf72a`. Their runtime source is unchanged
by the editor-only repair. The two later upstream commits only wrap an existing
`static_assert` with `#ifdef ZSTD_WINDOWLOG_LIMIT_DEFAULT`. Three actual MSVC
compile probes accept the pinned header and an absent macro, and reject an
incorrect value. `.build/integration-upstream-zstd-guard/receipt.json` records
that bounded validation. The binaries were **not rebuilt** for those two commits;
a complete engine using system zstd was **not qualified**. Final source and
compiled source identities must not be presented as identical.

| Installed artifact | SHA-256 |
| --- | --- |
| Mono editor | `2fdd2e4d30aecb40b2645e6ed96b757658347e7945a1c2885008ed8a6e7fc313` |
| Mono Debug template | `0404c8ab70a880367579c5428559b33b523d773dfa58d989fc0f6d3a86ec8ff1` |
| Mono Release template | `55b9ca7b3e8fae6f260a0fa6f041c9ba4ab5c6294bd8aa5a031a67149bb0b680` |
| Captured extension API | `e84e140b923451849e88aab8300021fd9d32f95c31825bb6d7e25a4235953271` |
| Matching Debug godot-cpp library | `4784d97057412f1ee36db659765ed6c05ee15fa9493f712cab9d6f9ac60b2ffe` |
| Matching Release godot-cpp library | `fafc0664a21efe4feb1d075dcd41d9719865e943ba1848e7d5cd94ca4bcaac17` |

`.build/canonical-upstream-artifacts.json` pins all 95 installed native/managed
files, their owned-worktree sources and the verified backups of replaced files.
The older 86-file artifact receipt remains historical; its binaries have been
superseded. SDK key `4bc13481314e7023` and its MSVC-19.51.36260.0 libraries match
the freshly captured, byte-identical API. No second native build owner was used.

### Repair and combined qualification

Unmodified TSCN-to-SCN conversion instantiated and repacked an inherited base
scene independently. A base property referencing a child introduced by a derived
scene then lost that reference. The exporter now saves the original PackedScene
when no export plugin modified it, preserving its serialized NodePaths. Modified
scenes still instantiate and repack to retain actual plugin changes.

`.build/integration-upstream-export-qualification/state.json` contains exact
commands, working directories, source/binary identities and log hashes for all
29 effective passing stages. One failed trilingual attempt is retained alongside
its successful replacement; historical failures are not relabelled as passes.

| Scope | Fresh or explicitly unchanged-source evidence |
| --- | --- |
| Native Debug and Release | 120 checks and ten CTest tests each; compiled runtime source unchanged by the export repair |
| Public API and managed documentation | 77 classes, 1,299 methods, 55 signals, 371 enums and 1,358 compiled descriptions; actual SDK/cache identity verified |
| Inherited scene references | 36 assertions in the editor and each relocated Debug/Release game: 108 total, six plain/inherited/layered scenes with GDScript Node and C# NetNode references |
| Customized binary exports | Six checks for plugin-added metadata and renamed children across both relocated templates; `.build/integration-upstream-export-customization-generic/1791316763850032200/receipt.json` |
| Physics | 19 Box2D runs, matching one/four-worker traces; 28 Box3D cases; 12 relocated Box2D runtime cases |
| Trilingual API | 27 stages, 197 assertions per editor/Debug/Release configuration, plus six independent high-level C#/C++ clock/fault processes |
| Reload and recovery | Fresh live/stopped C# Box3D ownership, low session/world reload, default-disabled runtime and opt-in assembly/unload/native/ABI recovery stages |
| Network lab | Packaged dedicated Release and listen-host Debug with three visible clients each, configured WAN simulation and reconnect; seven authoritative-physics/control cases |
| Admission | 24 editor, 24 Debug and 24 Release cases: 72 total, including clock/graceful/fresh/retired-token boundaries |

The original legacy reference failures and the candidate binary-export failure
remain under `.build/integration-upstream-scene-*`. The export-error control
retains the missing-solution logs and invalid bundle; the validator now rejects
export `ERROR:` output even when the exporter exits zero. Fresh isolated fixtures
create a standard solution and NuGet cache. The required trilingual
`modules/egp_net/samples/trilingual/NetInterop.sln` is tracked explicitly so a
fresh checkout does not depend on an ignored local solution.

### Repeatable tools and checkout provenance

Public tools reproduce the focused scene/export fixtures:

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_scene_node_refs.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --packages bin/GodotSharp/Tools/nupkgs --debug-template bin/godot.windows.template_debug.x86_64.mono.exe --release-template bin/godot.windows.template_release.x86_64.mono.exe --output .build/scene-references-repeat
& $egpPython misc/scripts/validate_egp_export_customization.py --editor bin/godot.windows.editor.dev.x86_64.mono.exe --debug-template bin/godot.windows.template_debug.x86_64.mono.exe --release-template bin/godot.windows.template_release.x86_64.mono.exe --output .build/export-customization-repeat
& $egpPython misc/scripts/validate_egp_net.py --verify-vendor-only --output .build/vendor-identity-repeat
& $egpPython -m unittest discover -s misc/scripts -p test_egp_vendor_manifest.py
& $egpPython -m unittest discover -s misc/scripts -p test_egp_scene_node_refs.py
```

The original 82 raw Yojimbo source pins remain unchanged. An explicit secondary
UTF-8/LF pin map accepts Git line-ending conversion only. Non-line-ending edits,
binary changes, malformed pins and unsupported text are rejected. Seven semantic
tests, a fresh Git-blob fixture and a deliberately changed-content rejection are
recorded in `.build/integration-upstream-vendor-portability/receipt.json`.
Vendor-only validation explicitly makes no native/runtime claim. Both new scene
tools match the bytes used in their passing runtime fixtures; their evidence
reader has three additional semantic tests. Ruff and the strict manifest-helper
type check pass.

Fresh inventory accounts for the seven original checkouts plus the owned eighth
`egp-upstream-sync` tree. The six foreign trees retain their exact tracked patches
and untracked file hashes; canonical `Temp Plan.md` and `tagged_query_test.b3rec`
remain untouched. The owned tree's 82 vendor differences are verified line-ending
restorations, with the required solution separately accounted for. Historical
dirty/superseded snapshots are preserved, not blindly merged. There are no open
EGP PRs in this inventory; publication records local/remote refs and provenance.

Acceptance remains open for high-level C++ adapter ownership, arbitrary in-flight
callback mutation, authenticated-node injected recovery, concurrent/exported
reload, full prediction/rollback and lag compensation, production admission,
broader physics parity, platform/hardware coverage (including PowerVR), scale,
soak and performance. Ordinary customized-scene checks do not prove every
customized inherited-reference combination. This qualification does not establish
full feature completion or AAA readiness. The integration loop remains ACTIVE.

## 2026-10-06: explicit C++ networking and Box3D owner transfer

C++ `egp::networking::Net` and `Box3D` now provide `detach_for_reload()` and
`resume_after_reload(Dictionary &, Error *)`. The capsule retains the same
tree-owned bridge/native session or adapter/world/body mapping. Old wrapper
destruction no longer closes or detaches the transferred objects. Version/type,
exact GDScript helper identity and random single-use token checks reject malformed,
foreign, forged, future and copied/consumed capsules without mutation. The caller
must preserve the original parent and explicitly hand off at a Godot-thread safe
boundary before unloading the DLL.

Tracked signal callbacks and message registrations are disconnected before
unload, then resubscribed from new code. Both wrappers expose matching
`connect`/`disconnect` methods; repeated disconnect is harmless. Direct native
callbacks, scene factories, application threads and unrelated closures remain
application responsibilities. C++ capsules are local references and do not share
the C# capsule format. See the networking README's explicit C++ handoff contract.

| Check | Evidence |
| --- | --- |
| Two actual compatible DLL reloads | `.build/integration-cpp-ownership-qualified-source/1791318619798667500/receipt.json`; three loaded code versions on the same live extension node; manually polled authenticated local server/client |
| Retained solver and owners | Same bridge/session/adapter/world/entity identities; body 10000 mapping retained; exact world tick/hash across each unload; advancing replicated body after each resume |
| Callback/traffic ownership | One before/after callback per completed solver tick, one adapter clock link, zero physics failures; one reliable application message per phase after handler re-registration; explicit disconnect leaves no callback |
| Capsule rejection | 30 checks per transfer, 60 total; missing/wrong types, foreign objects, forged token, full-width future version, copied/consumed capsules, unavailable old wrappers and repeated transfer |
| Existing trilingual behavior | `.build/integration-cpp-ownership-languages-final/receipt.json`; 27 passing stages with 197 assertions per editor/relocated Debug/Release configuration plus six independent C#/C++ clock/fault processes; final helper bytes frozen in executed fixtures |
| Evidence reader | Four semantic test methods reject missing fields, altered IDs/hashes/ticks, duplicate callbacks, wrong numeric types, absent physics and ownership counts; signed RefCounted IDs are explicitly accepted; Ruff/format pass |

Exact commands, DLL hashes, fixture/helper input hashes, engine identity and
watchdog-supervised process logs are in the receipts. The original first build
failure (fixture Variant-to-Node cast) and first evidence-reader failure (signed
RefCounted ID) remain under `.build/integration-cpp-ownership`; later semantic
tests also caught a missing final-phase hash check. These controls are preserved,
and only the final passing source-bound receipts support qualification.
The preliminary language run remains separate because it preceded the final
disconnect helper addition; the final namespace compiles and executes those
frozen final helpers.

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$egpSdk = "$env:LOCALAPPDATA/Godot/egp_cpp/sdk/4bc13481314e7023"
$egpLib = "$env:LOCALAPPDATA/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib"
& $egpPython misc/scripts/validate_egp_cpp_ownership.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --sdk $egpSdk --sdk-library $egpLib --output .build/cpp-ownership-repeat
& $egpPython -m unittest discover -s misc/scripts -p test_egp_cpp_ownership.py
```

These are header/helper and fixture changes; the installed 95 combined engine
artifacts, API fingerprint and matching SDK libraries remain unchanged. Editor
source is still `7b57a3b3140cb1c8b0bbcfb6bbe2c1f4eab70161`, template source is still
`c6a6920685b844ff0ba30d2e794117b776edf72a`, with the documented final zstd-guard
source distinction. `.build/integration-cpp-ownership-publication.json` verifies
the committed/pushed helper source, executed inputs, artifact/receipt integrity,
merge provenance and preserved foreign work. Eight engine worktrees remain
accounted for, six foreign snapshots and the two canonical unrelated files
unchanged, with no pending PRs or separate feature branches in the current inventory.

The previous engine pin `8eb540e94d` was also deployed in docs `60cbfec4e51cfbc158d18a07129fdf9bf4bf2e79`
and website `9b4f20f8d88a4a201e5468353a844870e93887cf`: hosted docs CI/Pages and
website deployment all passed. `.build/egp-docs-upstream-publication.json` records
live content checks and compiled snippets. New helper documentation is handed
back to that owner after this source publication; do not infer its deployment
from the earlier receipt.

The explicit C++ high-level owner-transfer item now has focused Debug evidence.
Polling pauses during the controlled unload. Automatic/in-flight owner transfer,
failed-library recovery of these wrappers, independent-process and exported-game
reload, Release-library recreation, arbitrary native layouts, production
prediction/rollback/admission and broader parity/platform/scale/soak/performance
acceptance remain open. The loop continues.

## 2026-10-06: C++ owner recovery through missing and invalid libraries

The public owner fixture now has `--native-recovery`. Before each compatible
reload it attempts a missing library and then an invalid library, without
consuming the saved owner capsules. All four attempts return `LOAD_STATUS_FAILED`.
The same live object remains its native Node parent, both networking children
remain attached, the extension library stays closed and extension methods remain
unavailable. No extension methods are called while its DLL is unavailable.
Parent-name edits made during those failures survive the later repair.

`.build/integration-cpp-owner-recovery-qualified/1791319083802516800/receipt.json`
passes 158 runtime assertions and 60 capsule checks. The two measured fault/repair
intervals were 25 ms and 14 ms. World ticks 44 and 55 and their exact hashes remain
unchanged across unload/repair; repaired code resumes the original sessions,
adapter/world/entity/body 10000 mapping, one callback per completed tick and
reliable application handlers. Subsequent world ticks advance to 55 and 66 with
one newly received message per phase. This is one local Windows Debug
editor-build process with manual polling paused during each controlled interval.
Configured outbound simulation is 10 ms latency, 2 ms jitter and 3 percent loss;
these settings do not measure actual loss or WAN performance.

`.build/integration-cpp-owner-recovery-default/1791319135331172800/receipt.json`
also passes the unchanged no-fault mode using the updated fixture/reader.
Seven semantic test methods cover complete recovery phases, missing/wrong fields,
fault ordering, short interval/parent-edit evidence and strict diagnostic
classification, in addition to the previous identity/physics/capsule checks.
The expected eight loader errors are counted by exact missing/extension messages
and the isolated invalid-DLL path; extra, unrelated or missing errors, script
errors and ownership failure markers still reject the run. Ruff/format pass.

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$egpSdk = "$env:LOCALAPPDATA/Godot/egp_cpp/sdk/4bc13481314e7023"
$egpLib = "$env:LOCALAPPDATA/Godot/egp_cpp/lib/4bc13481314e7023/MSVC-19.51.36260.0-Windows-AMD64-x64/Debug/egp_godot_cpp.lib"
& $egpPython misc/scripts/validate_egp_cpp_ownership.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --sdk $egpSdk --sdk-library $egpLib --native-recovery --output .build/cpp-owner-recovery-repeat
& $egpPython -m unittest discover -s misc/scripts -p test_egp_cpp_ownership.py
```

No native engine or public helper implementation changed for this acceptance
item. The C++ helper/mirror hash remains
`c3aac8bd6fba4af2bdf8ed013ce2fa4afd52a444a1e000dfb58d1cbf64823fb8`.
The prior 27-stage trilingual editor/Debug/Release run and 29 combined-engine
scopes retain byte-identical executed helper/native inputs; the installed 95
artifacts, API and SDK identities remain unchanged. New commands, sources, DLLs,
processes and log hashes are pinned in the two fresh receipts.
`.build/integration-cpp-owner-recovery-publication.json` verifies committed/pushed
fixture/docs, fresh evidence, inherited combined qualification, merge provenance
and the preserved worktree inventory. The docs owner is independently publishing
the preceding helper source pin; each deployment retains its own source identity.

Short missing/invalid-library owner recovery now has focused evidence. Prolonged
failure beyond clock catch-up limits still requires explicit stop/checkpoint,
re-admission and new entity mapping; that combined owner-recovery scenario remains
open. Automatic/in-flight transfer, exported/independent-process reload,
Release-library recreation, arbitrary ABI changes, production prediction and
broader physics/platform/scale/soak/performance acceptance also remain open.

## 2026-10-06: GitHub static checks and Linux receive-budget fixture repair

Networking run `37527377493` at `50b1de3090` passed Windows and macOS but failed
Ubuntu: 9 of 10 CTest cases passed; the jitter fixture sent 128 unreliable
messages, received zero, rejected zero and stayed connected. A frozen source
copy reproduced the same delivery assertion failure under Ubuntu WSL/GCC Debug.
Yojimbo's simulator has 512 packet slots and overwrites an occupied slot on ring
reuse. Pumping every 2 ms with up to 1.6 seconds of simulated delay can overrun
that buffer, despite configuring zero random packet loss.
An additional baseline rebuilt from the exact committed `cd7424f5e3` fixture
received 32 of 128 messages and failed the same assertion; the simulator's
stochastic delay need not produce the same missing-packet count on every run.
`.build/integration-receive-ci-baseline-audit/receipt.json` preserves that source
and failure, then verifies the qualified final Linux executable was restored
byte-for-byte.

Only that jitter fixture now sleeps 10 ms between pumps. Its 12-second deadline,
all 128 required deliveries, no-abuse/connection requirements and every other
receive budget, reliable order, fragmentation and unauthorized-wire assertion
remain unchanged. Three consecutive Linux candidate runs delivered all 128
messages with zero rejection. The source also follows the inherited copyright,
include and clang-format 22.1.5 checks; formatter brace additions are accounted
for separately from the single changed cadence literal.

`.build/integration-receive-ci-qualification.json` pins the controls, commands,
logs, source and executable identities. Full native networking qualification
passes 120 checks and all 10 CTest cases in each scope:

- Windows MSVC Debug: `.build/integration-receive-ci-debug/receipt.json`.
- Windows MSVC Release: `.build/integration-receive-ci-release/receipt.json`.
- Ubuntu WSL/GCC Debug: `.build/integration-receive-ci-linux/receipt.json`.

`.build/integration-receive-ci-final/receipt.json` also records fresh compilation
and a passing receive-budget test of the final formatted fixture on Windows
Debug and Linux Debug. No engine/core/vendor implementation or installed engine
artifact changed. This qualifies native core tests on these local configurations;
it does not qualify Linux/macOS Godot editors, exports, WAN or production scale.
Bounded simulator capacity and overload diagnostics still need broader coverage.

The early Linux runner controls are retained as failures: exit 127 used an
incorrect `/bin` executable path; a later build used an expired WSL `/tmp`
directory. The actual transport reproduction and three passing candidates use
the correct executable in a persistent `/var/tmp` build. Neither runner failure
is counted as a transport failure or successful test.

Committed source provenance includes owner recovery `4038698407`, the required
tracked solution ignore exception `2b7e76be94`, and docs-owner static repairs
`cd7424f5e3`: the networking module has `@ZSG-Studios` ownership, the validator
has its required executable Git mode and the spelling check is repaired.
`.build/integration-receive-ci-static/receipt.json` records the relevant local
style/ownership/tool checks. Native C++ CI now finishes an in-progress editor
and template matrix before starting its successor, preserving useful builds.
Run `37528340019` remains the separately identified engine/platform qualification;
replacement GitHub checks and that matrix are not claimed complete here.

```powershell
$egpPython = 'C:/Users/Rose-X/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $egpPython misc/scripts/validate_egp_net.py --configuration Debug --output .build/receive-ci-repeat-debug
& $egpPython misc/scripts/validate_egp_net.py --configuration Release --output .build/receive-ci-repeat-release
```

The recovery publication receipt verifies the consolidated source, merge
ancestry, unchanged 95 installed artifacts, inherited combined-engine evidence
and all eight current worktrees. Six foreign dirty snapshots and both unrelated
canonical files remain preserved. Full feature and release acceptance remain open.

## 2026-10-06: Hosted networking passes; engine unit-test migration repair

Networking run `37530772200` at `6a3690387a` passes Windows, Ubuntu and macOS.
The downloaded artifacts in `.build/integration-github-network-6a` preserve each
native receipt and build/test logs. The same source's GHA static checks pass,
but all 19 inherited platform build jobs fail before completing engine/runtime
qualification. Desktop builds still include removed dummy physics headers;
unsupported mobile/double profiles request physics unavailable in those profiles;
the minimal template accidentally compiles standalone Box3D executables as
engine unit tests. These are recorded failures, not successful engine builds.

The unit-test runner now requires the configured real backend whenever a physics
dimension is enabled. It has no dummy fallback. The viewport physics-picking
test requires a real 2D server and retains all its assertions and subcases.
No viewport test case was removed. `tests/SCsub` excludes just Box3D's two
standalone `main()` fixtures from engine force-linking: 173 of the original 175
recursive engine-test sources remain, and both excluded programs retain their
independent CMake/CTest targets.

`.build/integration-engine-test-migration/receipt.json` records an isolated
Windows MSVC compiler control reproducing `test_main.cpp:69`'s missing dummy
header, then successful compilation of `test_main.cpp` and `test_viewport.cpp`
with physics enabled in an editor and explicitly disabled in a debug template.
The force-link header retains the viewport registration and omits both standalone
entry points. Four object files and the generated header are preserved in that
evidence directory before archiving the managed fixture. The rejected attempt
to disable physics in an editor is retained: SConstruct's export-only guard is
intentional and remains enforced. These are compiler/graph checks, not a linked
engine or execution of its unit-test runner.

Both standalone native Box3D targets also rebuild and pass CTest in
`.build/box3d-current-validation/Release/native`; the exact build and test logs
are retained in `.build/integration-engine-test-migration`. This preserves their
deterministic replay and joint coverage independently of the engine test runner.

Docs-owner commits `f65c3357c1`, `acaf0b78a7` and `88f35a1802` correct the inherited
CI profiles. Android arm32/arm64, iOS and Web templates explicitly omit physics.
Double-precision Linux remains a sanitizer/debug template with `tests=yes` and
physics omitted; the regression-project check moves to the single-precision
Clang sanitizer editor. Existing supported desktop editor jobs and unit checks
remain. Android and double-precision editors are unavailable until their physics
profiles are implemented and qualified; their omission is not a passing editor
test or evidence of mobile/double physics support.

Production engine/module/API/helper sources and the 95 installed artifacts remain
unchanged. Replacement GHA must still build, link and run the enabled unit tests,
including real Box2D picking. Existing C++ editor/template matrix `37528340019`
continues independently, and its queued successor retains its own source pin.
The loop still owns any subsequent compile/runtime failures and broader feature
acceptance.

## 2026-10-06: Physics profile compiler controls and macOS C++ export evidence

GHA `37533104098` at `8ecf5efa30` finishes with static checks and the minimal
Linux template passing. The remaining profiles fail. Saved job logs in
`.build/github-failures` identify the next concrete causes: the double template's
SCU generator expects the removed editor directory; no-physics mobile/Web CSG
calls an omitted collision method; strict desktop compilers reject Box2D adapter
warnings; Windows clang-cl rejects conflicting floating-point options.

Commit `4db0f856bf` moves SCU coverage to a supported single-precision desktop
editor with GCC sanitizers. The double debug template retains explicit unit tests
and sanitizers. Commit `cb3c35220f` uses consistent non-contracting clang-cl flags
for both physics modules and standalone Box3D tests, retaining MSVC/GCC policies.
`.build/physics-fp-policy/receipt.json` preserves the actual conflicting-option
compiler failure, the repaired optimized LLVM IR without FMA, and a Clang Release
native Box3D build with both deterministic replay and joint CTest targets passing.
This is focused compiler/native solver evidence, not a linked engine result.

The CSG tree-entry condition now calls `is_using_collision()` only when 3D physics
is enabled. `.build/integration-csg-ci-repair/receipt.json` records the original
MSVC no-physics compile failure and successful compilation of the exact patched
translation unit with physics enabled and disabled. CSG rendering/runtime and
mobile exports still require the replacement hosted builds and runtime checks.

Box2D adapter cleanup retains signed negative-index rejection, aligns constructor
initializers with declaration order, supplies virtual destructors and matching
forward-declaration tags, and removes dead locals/functions and shadowed names.
Shape inflation copies the original tagged primitive before adjusting its active
radius, avoiding GCC's inactive-union uninitialized-copy diagnostic. All 26
adapter translation units compile with optimized GCC and Clang `dev_mode=yes`
warnings treated as errors. Baseline controls and failed intermediate candidates
are retained in `.build/integration-box2d-ci-repair/receipt.json`, with exact
commands, source hashes, object identities and logs. Query results populate
`collider_id` and use the upstream `get_collider()` lookup instead of assigning
the deprecated cached pointer; the script-facing collider result remains part
of the upstream wrapper contract and still needs current-engine runtime checks.
Physics module CODEOWNERS entries use the project's existing owner. No blanket
warning suppression or solver capability fallback is added.

Separately, macOS arm64 C++ job `112491597216` in run `37528340019` passes at
`2b7e76be94`. `.build/integration-cpp-hosted-2b7/receipt.json` freezes the downloaded
artifact and job-log identities: two SDK unit tests and 13 CLI/native/export
checks, including four expected diagnostic failures and Debug/Release exported
games reporting `EGP_CPP_GAME_PASSED`. This headless build omits Mono; it does not
qualify hot reload, interactive graphics or the current repaired source. The
artifact contains logs/results rather than engine/template binaries, so binary
SHA256 identities are unavailable for that hosted result.

The 95 installed artifacts retain their existing source/binary identities.
Earlier combined-engine receipts remain evidence for their recorded source
scopes. The repaired source requires a replacement hosted matrix and combined
runtime qualification before its integration is marked validated. Foreign dirty
snapshots and unrelated canonical files remain preserved; full acceptance stays
open.

The first replacement push `72ed3ae33f` fails before platform compilation:
mandatory copyright/header-guard hooks require the standard repository headers
on changed adapter files. The follow-up preserves their SPDX/Andrew Song notices
and places each existing `#pragma once` where the inherited guard check expects
it. The complete `prek run --files` hook set now passes over the full delta from
`8ecf5efa30`; `.build/integration-physics-ci-static/receipt.json` records it.
An exact-source compiler rerun also catches a local shadow warning introduced
when codespell corrects an existing misspelling to `inertia`; the local is
explicitly named `body_inertia`. That failed candidate remains recorded. Final
GCC/Clang source checks include this correction and the required headers; static
success alone does not establish a passing engine matrix.

## 2026-10-06: SCU registry and no-physics CSG mesh scheduling

The `4fb5ee6190` hosted matrix passes static checks and the minimal Linux template;
its SCU editor fails because the generator still registers retired multiplayer
and WebRTC directories. Commits `bbc09aca91` and `7376bd8007` remove those stale
entries. The owner checks all remaining 140 registry paths against tracked files
and runs the actual SCU generator successfully, retaining desktop SCU coverage.
Other useful platform builds remain independently identified by their source.

An additional CSG defect affects no-physics exports: `_make_dirty()` guards both
deferred mesh update paths behind `PHYSICS_3D_DISABLED`, although `update_shape()`
already separates mesh generation from collision generation. Removing just those
four guard lines restores scheduling when physics is disabled and retains the
enabled configuration's behavior. `.build/integration-csg-schedule/receipt.json`
records the baseline failure and six contracts executed against the extracted
actual method with physics enabled/disabled, covering repeated invalidation,
parent propagation, removal and an already-dirty root. Its deferred-call transport
is mocked; this is scheduling evidence, not graphical or mesh-runtime evidence.
The complete CSG translation unit also compiles under MSVC in both profiles;
commands, source/object hashes and logs are retained outside the owned fixture.

Downloaded native physics workflows add separate Debug/Release evidence on
Windows, Linux and macOS: Box2D run `37535945585` at `4fb5ee6190`, and Box3D run
`37535393515` at `72ed3ae33f`. All six profiles in each run pass their native
upstream checks; Box3D also passes its deterministic trajectory/joint targets.
These artifacts qualify the native solvers in their recorded profiles, not Godot
adapter runtime, mobile physics, prediction or complete engine integration.
