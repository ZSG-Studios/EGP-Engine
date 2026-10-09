# Temp Plan — EGP Godot Fork

## First: establish a working engine

- Populate the empty EGP repository from official Godot `master`, preserving upstream history.
- Configure `upstream` as https://github.com/godotengine/godot.git. Leave `origin` unset until an EGP fork remote is supplied.
- Create `codex/egp-setup` for fork-specific work.
- Inspect current Windows build requirements and verify installed dependencies.
- Build the unmodified C# editor and matching Windows debug export template using native Windows tooling.
- Launch the editor and verify a minimal C# project in-editor and as an exported executable.
- Record the upstream commit, exact build commands, build logs, runtime logs, and unresolved failures.

Acceptance: the editor starts, a C# scene runs, and its Windows export launches successfully. Establish this stock baseline before replacing engine systems.

## Keep the fork current

- Track Godot `master`; keep fork changes in small modules and adapters to reduce merge conflicts.
- Update by fetching upstream, merging `upstream/master`, rebuilding, and repeating editor, export, and runtime smoke checks.
- Promote upstream updates to the working fork only after validation passes.

## Intended replacements after setup

- **Box3D:** intended sole 3D physics backend. Investigate integration through `PhysicsServer3D`, retaining existing scene nodes where practical. Audit character movement, joints, queries, collision events, and unsupported features before removing other 3D backends. Decide 2D physics separately.
- **LiteNetLib + LiteEntitySystem:** intended multiplayer stack. Investigate C# integration, authoritative servers, prediction, reconciliation, and scene/entity lifetime. Native-code or GDScript access requires an explicit bridge.

Sources:

- https://github.com/erincatto/box3d
- https://github.com/RevenantX/LiteNetLib
- https://github.com/RevenantX/LiteEntitySystem

## Native C++ extension editor support

- Build the tooling directly into EGP's editor for standard godot-cpp GDExtensions.
- Provide extension scaffolding, toolchain configuration, editor-driven debug/release builds, and clickable compiler diagnostics.
- Generate or select bindings against EGP's extension API; track compatibility as upstream master changes.
- Reuse Godot's loading and reload hooks. Reload compatible extensions only after successful builds; provide a restart path when reload is unsupported.
- Integrate extension libraries into game exports and report missing platform binaries clearly.
- Keep project extensions as shared libraries so extension changes do not require rebuilding the engine. Platform compilers and build tools remain prerequisites.
- Validate creation of a custom node visible in the editor, navigation to a deliberate compiler error, rebuild/reload or restart, and an exported executable that loads the extension.

This is a planned feature after the working stock engine baseline, not an implemented capability.

## Performance investigation shortlist

| Priority | System | Investigation |
| --- | --- | --- |
| 1 | Scene processing | Measure traversal, callbacks, and transform updates; evaluate batched entity processing for heavy workloads. |
| 2 | C# integration | Profile native/managed crossings, allocations, and GC spikes; investigate bulk APIs and buffer reuse. |
| 3 | Threading | Inspect scheduling, contention, and main-thread stalls across physics, loading, and rendering. |
| 4 | Resource loading and streaming | Measure background loading, scene instantiation, shader compilation, and memory peaks. |
| 5 | Rendering | Profile draw submission, culling, shadows, and shader stalls before selecting changes. |
| 6 | Navigation | Measure pathfinding and avoidance under crowds; evaluate batched queries and update budgets. |
| 7 | Dedicated servers | Investigate lean headless builds, simulation timing, replication costs, and long-session memory behaviour. |
| 8 | Build configuration | Identify optional modules that can be excluded without extensive source deletions. |

## Evidence and decisions

For each system, record the current implementation, measured bottleneck, candidate improvement, compatibility gaps, upstream maintenance cost, and a keep/replace/defer recommendation.

Compare stock Godot and EGP using identical scenes and settings. Record CPU/GPU time, p95/p99 frame times, memory, allocations, and server tick time. Include physics correctness and networking tests with latency, packet loss, disconnects, and reconnects.

Preserve existing Godot capabilities during investigation. Further removals require a demonstrated benefit and a working replacement. The EGP directory currently has no project content defining a narrower target.
