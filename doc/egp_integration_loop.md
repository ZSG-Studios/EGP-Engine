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
| Physics | Box2D/Box3D scene integration, joints, characters, queries, events, serialization, deterministic stepping and restore; unsupported capabilities exposed honestly | Combined native/Mono editors passed 14 Box2D runs and all 28 Box3D cases; wider parity/scale/platform gates remain |
| Physics/network | Explicit fixed clock, fingerprint validation, authoritative state, commands, prediction/correction/replay and recovery | Existing limited fixtures; full game contract pending |
| Networking | Encrypted admission, account/peer/entity identities, authority, ownership, interest, lifecycle, reconnect and backpressure | Native and language fixtures exist |
| Advanced networking | Field deltas, bounded bandwidth/queues, input acknowledgments, lag compensation, scale/soak, malicious input rejection | Implementation/qualification gaps remain |
| Network lab | Dedicated server, listen host, N clients, visible windows, latency/jitter/loss, directional simulation, reconnect, logs/watchdog/cleanup | Native host and packaged Mono Debug host/Release dedicated server pass simultaneous visible clients, WAN simulation and reconnect; broader matrix remains |
| Network lab expansion | Editor controls; mixed GDScript/C#/C++ clients; packaged games; IPv6; server restart; interest/ownership checks; load/soak and adverse-condition matrix | Pending |
| C++ GDExtension | Scaffold, compiler errors/navigation, Debug/Release, exact SDK, reload, ABI/restart path, exported load | Matching SDK/editor controls, mixed-language exports and live compatible Debug reload pass on Windows; incompatible ABI/restart recovery and other platforms remain |
| C++ hot reload | Changed behavior in editor and running game, live instances/state/signals, failed build retains working code, repeat reload/unload cleanup | Two live Debug rebuilds preserve existing IDs, property state, callables and signals; failed compile retains published code; Missing/invalid DLL recovery also preserves extension and editable parent state; incompatible class/ABI changes and soak remain |
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
