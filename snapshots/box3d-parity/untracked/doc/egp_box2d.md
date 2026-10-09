# EGP Box2D integration

Box2D is EGP's sole native 2D physics backend. Godot Physics 2D sources,
registration, editor build choices and obsolete solver settings are removed.
PhysicsServer2D and ordinary physics scene nodes remain the public integration
surface. The engine migrates old backend selections to `Box2D Physics` at startup.
Enabled physics requires the corresponding Box module; a missing module fails
the build rather than registering a dummy backend.

## Source and deterministic profile

- Solver: [erincatto/box2d](https://github.com/erincatto/box2d), pinned unchanged
  at `56edae79f2949d86142b03450d5d60f63bcf5a6f` (3.2 development).
- Adapter origin: [erincatto/godot-box2d](https://github.com/erincatto/godot-box2d),
  `66260bc0eb77a9e6eb78b80cc163ed2c912b448b`, MIT, Andrew Song. EGP's modified
  native port uses engine RID registries and avoids GDExtension and godot-cpp.
- C17 solver, single precision, x86_64/arm64 desktop builds. Four-wide SIMD;
  precise arithmetic and disabled FMA contraction under Clang/GCC.
- Main-thread physics entry, process guard, fixed engine tick rate and four
  substeps by default. Time scaling and runtime tick-rate changes are rejected.
- `physics/box_2d/worker_count` defaults to 1, accepts 1–8, and is independent of
  hardware CPU count. `physics/box_2d/substeps` accepts 1–16. Both require restart.
- `physics/box_2d/pixels_per_meter` defaults to 100 and must be positive. The
  solver works directly in pixels with its tolerances scaled at initialization.
- Space traversal, shared-shape updates and callback delivery use RID order.
  Area overlaps retain RID/index records so shape-vector changes and callback
  deletion do not leave pointers to freed geometry.
- Custom integrators disable automatic gravity, damping and applied forces while
  retaining impulses and collision response. Direct-state environment getters
  retain the combined, gravity-scaled area values for application integration.

Determinism also depends on application creation order, identical geometry,
gameplay inputs, solver profile and floating-point environment. Scene RIDs are
local handles, not authoritative network entity identifiers. Removing the old
backends does not establish scene replication ordering or rollback.

## Verification

The pinned upstream MSVC Debug and Release suites passed locally, including
determinism, worker scheduling, recording and snapshot tests. The initial Debug
timeout is preserved; a retry used the same 120-second watchdog and passed.
The native port compiled all 25 C++ translation units against the engine APIs.
The rebuilt native Windows editor passed all twelve scene runs, including exact
300-tick one/four-worker trajectories and old-backend migration. Repeat those
checks for each newly qualified executable:

```powershell
python misc/scripts/validate_box2d_scene.py --engine <editor.exe> --output .build/box2d-scene-cutover
python misc/scripts/validate_box3d_scene.py --engine <editor.exe> --output .build/box3d-scene-cutover
```

Scene fixtures cover sole backend registration, exact twin-world trajectories,
contacts and impulses, body replacement, query/motion exclusions, shared-shape
lifetimes, deletion during area callbacks, character floors and fixed-world
joint anchors. Each process has a 60-second watchdog; the trajectory fixture
also runs with four workers. Receipts identify the executable and fixture hashes.
Additional fixtures cover compound shape collision, rigid-body contacts, area
replacement, custom integrators and migration of old backend selections.
A relocated Windows debug template also passes a 90-tick probe of ordinary
2D/3D rigid bodies, default Box server classes and absence of legacy backends.
The matching native networking export passes fresh import, export, relocation,
five runtime fixtures and separate authenticated server/client processes.

## Remaining parity gates

The inherited adapter does not implement infinite world boundaries or separation
rays. Space solver parameters, ray CCD, one-way rigid-body penetration margins
and moving-platform behavior need replacements or further qualification.
Convex solver polygons support at most eight vertices. Joint-space transfers,
full geometry/scaling semantics, network lifecycle ordering and scene rollback
also require work. Upstream solver serialization does not itself restore Godot
scene nodes or application state. Cross-platform and performance qualification
remain separate release gates. Packaged networking evidence covers the Windows
debug template and tested fixtures; it does not establish scene rollback or
release-template/platform coverage.
