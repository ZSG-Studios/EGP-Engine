# EGP — Godot Engine Fork

EGP is a fork of [Godot Engine](https://github.com/godotengine/godot) built around faster engine systems and regular integration of upstream `master`.

## Goals

- Keep the fork up to date with Godot `master`, validating upstream updates before promoting them to the working engine.
- Use [Box3D](https://github.com/erincatto/box3d) as the sole native 3D physics backend and [Box2D](https://github.com/erincatto/box2d) for 2D physics, with continued engine compatibility and runtime qualification. See the [3D](doc/egp_box3d.md) and [2D](doc/egp_box2d.md) integration guides.
- Use [Yojimbo](https://github.com/mas-bandwidth/yojimbo) for native encrypted UDP transport, C++ entity replication and GDScript high-level and raw APIs.
- Build native editor tooling for [godot-cpp](https://github.com/godotengine/godot-cpp) GDExtensions directly into EGP: project scaffolding, compiler/toolchain configuration, editor-driven builds, clickable diagnostics, extension reload where supported, and export integration.
- Investigate scene processing, C# interop, threading, resource streaming, rendering, navigation, and dedicated-server performance. Select further replacements using measured results.
- Keep fork changes modular and reviewable so upstream updates remain manageable.

## Current status

The native networking foundation uses pinned Yojimbo 1.13.5 and does not require Mono. The standalone Windows Debug and Release suites pass 101 UDP, replication, authority, interest, reconnect and token checks, plus separate-process encrypted connections, an interest-memory regression, and upstream suites. The current native editor and relocated debug export pass all five GDScript fixtures and separate Godot server/client checks, integrated with the Box physics chat. See [networking setup and qualification](modules/egp_net/README.md). Native C++ extension tooling is built into the editor; see [the C++ extension guide](doc/egp_cpp_extensions.md). A bounded GDScript prediction/reconciliation helper also verifies native Box3D correction and replay. Game-level prediction integration, physics rollback fidelity, performance and AAA readiness require further qualification.

FASTBuild v1.20 is integrated with SCons on Windows x64/MSVC. Distributed compilation against the WireGuard worker at `10.77.64.1` and the forced-remote integration fixture passed. A fixed source snapshot produced the C# development editor and both Windows export templates, including the editor's embedded C++ SDK, Debug/Release C# API assemblies, editor tools and SDK packages. All three executables passed headless startup checks with Box2D and Box3D active.

Those build outputs are in `.build/fb-source/bin`, with source and executable hashes in `.build/fastbuild-build-receipt.json`. The snapshot contains the earlier LiteNet implementation and excludes the later Yojimbo migration and subsequent changes to the main checkout. Editor project import completes but reports unsupported Box physics features, including soft bodies, separation rays and world boundaries. These checks establish build and startup status; feature parity, C# game export and gameplay behavior need separate qualification.

## Validation approach

### FASTBuild on Windows

Keep the WireGuard tunnel active and FASTBuild v1.20 running on the worker at
`10.77.64.1:31264`. From the repository root, run:

```powershell
.\misc\scripts\build_egp.ps1 -Setup -CheckWorker
.\misc\scripts\build_egp.ps1 -Target editor
.\misc\scripts\build_egp.ps1 -Target template_debug
.\misc\scripts\build_egp.ps1 -Target template_release
```

The editor command completes native linking, C# glue generation, API assemblies,
editor tools and SDK packages. Fresh builds of the current checkout write to
`bin`; the verified snapshot outputs above remain in `.build/fb-source/bin`.
Use `-Local` for local compilation, `-Jobs 6` to limit parallel jobs, or
`-DistVerbose` to inspect remote activity. See [the EGP FASTBuild guide](doc/egp_fastbuild.md)
for toolchain requirements, alternate workers, BFF targets and remote-compilation
validation.

### Native C++ extension workflow

The goal is to create a C++ extension project, configure its toolchain, build it, and navigate compiler errors from the editor. Match godot-cpp bindings to EGP's extension API, support debug and release builds, reload compatible extensions after successful builds, and offer an editor restart when reload is unsupported. Package the appropriate extension libraries with exported games.

The editor includes the C++ SDK and its generated bindings. Project extensions remain standard GDExtension libraries; developing one does not require rebuilding the engine or downloading godot-cpp. A platform compiler and CMake are still required.

Acceptance includes a generated extension whose custom node appears in the editor, a compiler error that opens the correct source location, a successful rebuild/reload or restart, and an exported game that loads the extension.

### Performance and runtime checks

Compare stock Godot and EGP with identical scenes and settings. Track CPU/GPU time, p95/p99 frame times, memory, allocations, and server tick time. Validate physics behavior and multiplayer operation under latency, packet loss, disconnects, and reconnects.

## Upstream and licensing

EGP is maintained by ZSG-Studios. Godot is developed by the Godot community; this fork is not an official Godot release. Original license and attribution files are retained. Official Godot downloads below provide upstream builds, not EGP builds.

---

# Godot Engine

<p align="center">
  <a href="https://godotengine.org">
    <img src="misc/logo/logo_outlined.svg" width="400" alt="Godot Engine logo">
  </a>
</p>

## 2D and 3D cross-platform game engine

**[Godot Engine](https://godotengine.org) is a feature-packed, cross-platform
game engine to create 2D and 3D games from a unified interface.** It provides a
comprehensive set of [common tools](https://godotengine.org/features), so that
users can focus on making games without having to reinvent the wheel. Games can
be exported with one click to a number of platforms, including the major desktop
platforms (Linux, macOS, Windows), mobile platforms (Android, iOS), as well as
Web-based platforms and [consoles](https://godotengine.org/consoles).

## Free, open source and community-driven

Godot is completely free and open source under the very permissive [MIT license](https://godotengine.org/license).
No strings attached, no royalties, nothing. The users' games are theirs, down
to the last line of engine code. Godot's development is fully independent and
community-driven, empowering users to help shape their engine to match their
expectations. It is supported by the [Godot Foundation](https://godot.foundation/)
not-for-profit.

Before being open sourced in [February 2014](https://github.com/godotengine/godot/commit/0b806ee0fc9097fa7bda7ac0109191c9c5e0a1ac),
Godot had been developed by [Juan Linietsky](https://github.com/reduz) and
[Ariel Manzur](https://github.com/punto-) for several years as an in-house
engine, used to publish several work-for-hire titles.

![Screenshot of a 3D scene in the Godot Engine editor](https://raw.githubusercontent.com/godotengine/godot-design/master/screenshots/editor_tps_demo_1920x1080.jpg)

## Getting the engine

### Binary downloads

Official binaries for the Godot editor and the export templates can be found
[on the Godot website](https://godotengine.org/download).

### Compiling from source

[See the official docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling)
for compilation instructions for every supported platform.

## Community and contributing

Godot is not only an engine but an ever-growing community of users and engine
developers. The main community channels are listed [on the homepage](https://godotengine.org/community).

The best way to get in touch with the core engine developers is to join the
[Godot Contributors Chat](https://chat.godotengine.org).

To get started contributing to the project, see the [contributing guide](CONTRIBUTING.md).
This document also includes guidelines for reporting bugs.

## Documentation and demos

The official documentation is hosted on [Read the Docs](https://docs.godotengine.org).
It is maintained by the Godot community in its own [GitHub repository](https://github.com/godotengine/godot-docs).

The [class reference](https://docs.godotengine.org/en/latest/classes/)
is also accessible from the Godot editor.

We also maintain official demos in their own [GitHub repository](https://github.com/godotengine/godot-demo-projects)
as well as the [Asset Store](https://store.godotengine.org/).

There are also a number of other
[learning resources](https://docs.godotengine.org/en/latest/community/tutorials.html)
provided by the community, such as text and video tutorials, demos, etc.
Consult the [community channels](https://godotengine.org/community)
for more information.

[![Code Triagers Badge](https://www.codetriage.com/godotengine/godot/badges/users.svg)](https://www.codetriage.com/godotengine/godot)
[![Translate on Weblate](https://hosted.weblate.org/widgets/godot-engine/-/godot/svg-badge.svg)](https://hosted.weblate.org/engage/godot-engine/?utm_source=widget)
