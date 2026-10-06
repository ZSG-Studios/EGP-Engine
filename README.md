# EGP — Godot Engine Fork

EGP is a fork of [Godot Engine](https://github.com/godotengine/godot) built around faster engine systems and regular integration of upstream `master`.

## Goals

- Keep the fork up to date with Godot `master`, validating upstream updates before promoting them to the working engine.
- Make [Box3D](https://github.com/erincatto/box3d) the sole 3D physics backend, with engine integration and compatibility checks before removing existing backends. The 2D physics direction remains undecided.
- Use [LiteNetLib](https://github.com/RevenantX/LiteNetLib) and [LiteEntitySystem](https://github.com/RevenantX/LiteEntitySystem) for multiplayer transport and entity replication.
- Build native editor tooling for [godot-cpp](https://github.com/godotengine/godot-cpp) GDExtensions directly into EGP: project scaffolding, compiler/toolchain configuration, editor-driven builds, clickable diagnostics, extension reload where supported, and export integration.
- Investigate scene processing, C# interop, threading, resource streaming, rendering, navigation, and dedicated-server performance. Select further replacements using measured results.
- Keep fork changes modular and reviewable so upstream updates remain manageable.

## Current status

The fork is being established. Native C++ extension tooling is built into the editor, with a bundled godot-cpp SDK, project scaffolding, Debug/Release builds, clickable diagnostics, and extension reload. See [the C++ extension guide](doc/egp_cpp_extensions.md). Box3D and the LiteNet stack are separate integrations. No general performance improvements or production readiness are claimed.

The first milestone is a working stock Godot baseline: build the C# editor and Windows export template, run a minimal C# scene, and verify its exported executable. Engine replacements follow that baseline.

## Validation approach

### Native C++ extension workflow

The goal is to create a C++ extension project, configure its toolchain, build it, and navigate compiler errors from the editor. Match godot-cpp bindings to EGP's extension API, support debug and release builds, reload compatible extensions after successful builds, and offer an editor restart when reload is unsupported. Package the appropriate extension libraries with exported games.

The editor includes the C++ SDK and its generated bindings. Project extensions remain standard GDExtension libraries; developing one does not require rebuilding the engine or downloading godot-cpp. A platform compiler and CMake are still required.

Acceptance includes a generated extension whose custom node appears in the editor, a compiler error that opens the correct source location, a successful rebuild/reload or restart, and an exported game that loads the extension.

### Performance and runtime checks

Compare stock Godot and EGP with identical scenes and settings. Track CPU/GPU time, p95/p99 frame times, memory, allocations, and server tick time. Validate physics behaviour and multiplayer operation under latency, packet loss, disconnects, and reconnects.

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
