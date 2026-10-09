# EGP Box3D integration

Box3D is EGP's sole native 3D physics backend. Jolt and Godot Physics 3D
modules and the Jolt vendor dependency have been removed. The native scene
adapter is always built when 3D physics is enabled and is the default backend.
Box2D replaces Godot Physics 2D; see [the 2D integration](egp_box2d.md).

`EGPBox3DWorld` provides explicit fixed stepping, canonical entity commands and
local full-world rollback. The ordinary scene adapter remains experimental:
backend removal does not establish full API parity, scene rollback or AAA readiness.

## Pinned deterministic profile

- Upstream: https://github.com/erincatto/box3d
- Revision: `e77352cd606dc1a34209094076199549a52ea0a1` (0.1.0 development).
- Vendored source is unchanged, with license and a normalized source hash manifest.
- C17 solver, float32, 64-bit desktop architectures; fixed SIMD width 4, AVX2
  dispatch disabled; MSVC precise math; GCC/Clang fast math and FMA contraction disabled.
- Immutable rate (default 60 Hz), substeps (default 4), gravity, sleep and CCD.
  Workers may differ; trajectory equality must pass qualification.
- Meters with Box3D's global length scale fixed at 1; round-to-nearest required.
  Application code must preserve the default floating-point environment in solver workers.
- Positive signed 64-bit application entity IDs. Commands execute in ascending
  `(entity_id, sequence)` order; creation order is therefore independent of packet arrival.
  Arrival-dependent sequence assignment would still break determinism: networking
  must supply authoritative sequences. No native IDs, pointers or scene traversal
  order enter the replication schema.

Upstream explains the creation-order and compiler requirements in
[its determinism documentation](https://github.com/erincatto/box3d/blob/e77352cd606dc1a34209094076199549a52ea0a1/docs/simulation.md#determinism).
Application gameplay, random seeds, lifecycle commands, asset geometry and
entity inputs must also be identical for a deterministic simulation.

## Tick and networking contract

The native class is in `modules/box3d/egp_box3d_world.h`. `configure` runs once;
`get_tick` initially returns zero. `step_tick(get_tick() + 1)` performs exactly
one step after validating the full ordered command batch. Wrong ticks, duplicate
entity sequences, nonexistent bodies, nonfinite inputs and invalid quaternions
are rejected. `clear_pending_commands` discards a rejected batch.

Bodies can currently be boxes, spheres or Y-axis capsules, with static,
kinematic or dynamic body type. The command API supports destruction, center
impulses, linear velocity and pose/velocity corrections. `apply_queued_commands`
applies an ordered batch without stepping, for baseline creation and authoritative
state application. Simulation and all reads belong to the constructing thread.
Entries from independent worlds and managed destruction are serialized around
Box3D's process-global world slots and replay length scale. Each step can still
use multiple solver workers; concurrent independent world stepping is not enabled.

The networking chat now owns the native Yojimbo session and GDScript
`EGPNetBox3D` helper. Its server simulation-tick callback steps the explicit
world before publishing body states. Network and physics rates must match;
the helper checks the immutable profile fingerprint and rate on attachment.
Network entity lifecycle and command sequence assignment must still be
authoritative. The physics module does not assign sequences from packet arrival.

For client rollback: restore the local snapshot associated with the processed
input tick, apply authoritative lifecycle/pose/velocity corrections, flush the
correction commands, snapshot that boundary, then replay buffered input ticks.
Read native position, rotation and velocities through `get_body_state` when
publishing entity state. Rendering interpolation must not feed back into physics.

Full snapshots use Box3D's public recording seed/player APIs, including internal
contact, island, allocator and warm-start state. A retained player owns the
restored world and geometry. Body names reconstruct the entity map through
public handles; the adapter never rewrites opaque ID fields.

Snapshot bytes are trusted local rollback history for a compatible binary/ABI.
They are not a network wire format, portable save format or authenticated data.
The 64 MiB cap and checksum detect ordinary damage; they do not make upstream
deserialization safe for hostile input. Captures/restores require no queued
commands and restores validate a candidate before replacing the live world.

`get_state_hash` hashes profile, tick, stable IDs, types, awake state, poses and
velocities. It is a diagnostic, not a digest of all hidden solver state.
Compare hashes only at matching simulation ticks, profiles and entity topology.
Authoritative pose/velocity corrections do not reproduce hidden remote contact
caches. Full client/server convergence under collision corrections remains an
integration qualification requirement even though exact local replay passes.

## Repeatable checks

```powershell
python misc/scripts/validate_box3d.py --upstream-tests
```

The script verifies vendored source hashes, builds the native fixture, compares
all 600 ticks with a committed reference trajectory, and writes an evidence
receipt under `.build/box3d-validation/Release` (or `Debug`). It optionally builds/runs the full
upstream suite. Every subprocess has a timeout. The native test covers contact
stacks, reordered spawn/impulse batches, one/four workers, destroy/recreate,
full-state restore at tick 150 followed by 450 replay comparisons, repeated
snapshot ownership/lifetime, concurrent owner lifecycle, rejection without world mutation, and corrections
without tick advancement. Cross-platform CI runs this same golden trajectory.
CI configuration is not evidence that those machines have passed.

For an editor that includes the module:

```powershell
.build/venv/Scripts/python.exe -m SCons platform=windows target=editor module_box3d_enabled=yes module_mono_enabled=yes accesskit=no d3d12=no angle=no
bin/godot.windows.editor.x86_64.mono.console.exe --headless --path tests/physics/box3d/godot --script res://smoke.gd
```

Match the executable name to the build's `dev_build`/suffix settings. This smoke
test exercises the actual registered class and snapshot replay. A separately
compiled object is compilation evidence only; it does not prove editor or
exported runtime integration. The networking chat owns the combined native Yojimbo/GDScript integration fixture.
Earlier C# and LiteEntitySystem receipts are historical evidence.

Pass `--engine path/to/the/editor.exe` to the validation script to include this
native Godot smoke in its receipt. Release and Debug evidence are kept separately.

Pre-cutover Windows qualification also covered an editor with the scene backend: the explicit-world smoke passes 180 ticks and exact local replay.
The scene determinism fixture compares two independent scene spaces at every
one of 300 ticks, including contacts, impulses and body replacement. Their
poses and linear/angular velocities match exactly. This establishes the tested
Windows trajectory, not cross-platform agreement or scene rollback.

## Native scene backend

The native adapter is mandatory when 3D physics is enabled. New projects use
`Box3D Physics`; old backend selections migrate at startup. No extra scene flag is required.
The native port and its provenance are in `modules/box3d/scene_backend`.
It supports rigid/static/kinematic bodies, primitive and mesh shapes, areas,
character motion, direct queries, contacts, and pin/hinge/slider joints.
It uses the fixed engine timestep, four substeps, one worker by default, precise
float math, the shared process guard and RID-ordered owner callbacks.
Changing tick rate or scaling the timestep while running is rejected.

This scene world has separate ownership from `EGPBox3DWorld`. It does not yet
implement the stable network entity command and rollback contract for stock
scene nodes. RID ordering requires identical scene creation order; it does not
make arbitrary network arrival order deterministic. Use the explicit world for
authoritative network physics; qualify the current Yojimbo helper separately.

```powershell
python misc/scripts/validate_box3d_scene.py --engine path/to/the/editor.exe
```

The runner requires the actual Box3D singleton, success markers and clean exits,
with a 60-second watchdog per regression. Fixtures cover basic scene behavior
and native shape indices/exclusions, not full engine parity. All 24 pre-cutover
Windows regressions passed, including world-anchored joints, shared-shape and
body/space lifetime, and the 300-tick independent-world comparison. Receipts
retain the editor hash and each exercised script hash; the first joint failure
is preserved separately from the corrected run. The inherited
concave-sensor fixture records a known divergence rather than parity.
The refreshed 28-case native suite also passes ConeTwist/6DOF and soft-body
fixtures; receipts are recorded in `doc/egp_integration_loop.md`. Remaining gaps
include separation rays, broad soft-body collision parity, margins, penetration
depth, concave sensor visitors, per-shape area signals, material
combination rules and true infinite world boundaries (currently a finite plate).
Cylinders use a hull approximation; scaling/geometry behavior needs qualification.

## Replacement and release gates

| Capability | Current implementation / gate |
| --- | --- |
| Fixed native world, canonical commands, full local rollback | Implemented; MSVC Debug/Release and Clang Release match the native Windows trajectory |
| Godot explicit-world binding | Actual headless GDScript and C# runtime replay checks passed; receipts preserve the qualified source version |
| Existing RigidBody3D/StaticBody3D/Area3D nodes | Sole default native scene adapter; refreshed 28-case native Windows suite passes; full parity remains |
| CharacterBody3D motion, floors, slopes, moving platforms | Basic motion regression passes; slopes and moving-platform compatibility still require fixtures |
| Compounds, hulls, meshes, height fields, scaling and margins | Native conversion implemented; compound indices and basic mesh/cylinder tests pass; scaling and margin parity remain |
| Collision filters, sensors, contact ordering, shape/ray/overlap queries | Basic filters, areas, contacts and exclusions pass; concave visitors, per-shape signals and penetration semantics remain |
| Godot joints and unsupported constraint semantics | Pin/hinge/slider/ConeTwist/6DOF implemented and covered by focused Windows fixtures; broad parameter and solver parity remain |
| Soft bodies, vehicles and ragdolls | Focused soft-body Windows fixture passes; broad soft collisions, vehicles and ragdoll parity remain |
| Authoritative physics under prediction and corrections | Native Yojimbo fixed-clock replication, bounded correction/input replay and packaged separate-process tests pass; scene rollback and broader adverse-network qualification remain |
| Windows/Linux/macOS, x64/arm64 and export templates | Windows x64 editor and native debug template qualified for tested fixtures; other platforms and release templates remain |
| Long runs, large scenes, allocation/memory/performance budgets | Measured limits and regressions required |
| Removal of Jolt and Godot Physics 3D | Source modules, vendor dependency, registrations, build choices and obsolete solver settings removed |

The legacy solver removal is implemented. PhysicsServer3D remains the public
scene API. The rebuilt native Windows editor passes all 24 scene regressions,
the 180-tick explicit-world replay, and the sole-backend/migration checks shared
with Box2D. Its actual fork API matches its embedded C++ SDK byte-for-byte.
The matching Windows debug template passes ordinary 2D/3D body simulation,
fresh networking import/export/relocation, five networking fixtures and separate
authenticated server/client processes. Editor shutdown documentation callbacks
are guarded after their owner is destroyed. Runtime receipts from before the
cutover remain separate historical evidence.
Full parity and AAA readiness remain gated by the capabilities above.
