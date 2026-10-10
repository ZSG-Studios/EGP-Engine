# EGP

EGP is a fork of [Godot Engine](https://github.com/godotengine/godot),
maintained by ZSG-Studios and based on upstream `master`.

## Features

- Box2D and Box3D physics.
- Native Superpos networking with matching GDScript, C# and C++ APIs.
- Explicit schemas, canonical state, checked ownership and bounded packet delivery.
- Built-in C++ extension tools and a bundled godot-cpp SDK.
- Native xmake builds with platform toolchains and incremental dependencies.
- Forward+ rendering exclusively, using Vulkan, Direct3D 12 or Metal.

## What EGP replaces, and why

EGP keeps Godot's editor, scene system and scripting model, while choosing one
physics backend per dimension and one multiplayer transport. This gives the fork
one set of simulation, networking and lifecycle contracts to integrate and test
across GDScript, C# and C++.

| Removed system | EGP replacement | Reason for the change |
| --- | --- | --- |
| Godot Physics 2D | Native Box2D integration | Use a pinned solver with an explicit fixed-step profile, substeps and worker settings, while keeping `PhysicsServer2D` and ordinary 2D physics nodes as the game-facing API. |
| Godot Physics 3D and Jolt, including the Jolt vendor dependency | Native Box3D integration | Use the Box solver family for both dimensions and expose an explicit 3D world with ordered entity commands, fixed ticks and local full-world snapshots for authoritative simulation and replay. |
| Godot's high-level multiplayer/RPC stack, ENet, WebRTC and `WebSocketMultiplayerPeer` | Native Superpos core and `SuperposSession` / `SuperposWorld` | Integrate explicit schemas, canonical ownership, authenticated associations and bounded delivery through matching native language APIs. Scene projection and broad gameplay qualification remain separate work. |

### Rendering: Forward+ only

EGP removes the Compatibility and Mobile renderers, OpenGL/OpenGL ES, ANGLE,
and WebGL/WebXR support. Rendered projects require Forward+ through a supported
RenderingDevice backend. The dummy backend remains available for headless servers
and tooling. Projects that explicitly select a removed renderer must change
`rendering/renderer/rendering_method` and any mobile override to `forward_plus`.
Devices below Forward+ requirements cannot fall back to another renderer.
Web exports are unsupported until a RenderingDevice web backend is available.
Immersive visionOS has an [experimental, explicitly enabled Forward+ path](doc/egp_visionos_experimental.md); foveation is disabled and device qualification is pending.

### Physics: consistent integration and explicit simulation

Box2D is the sole 2D backend and Box3D is the sole 3D backend. Old backend
selections migrate to the corresponding Box backend. Scene physics continues to
use Godot's physics clock; `EGPBox3DWorld` separately gives applications control
over fixed ticks, stable body identities, ordered commands and capture/restore.
Superpos simulation-provider integration must be qualified separately before
attaching a solver world to an authoritative network clock.

Solver-specific controls from the removed backends are also retired where the
Box solvers have a different contract. Examples include old solver-bias settings,
3D shape margins and collision/joint priorities, 2D ray-based CCD and
`SeparationRayShape3D`. Box-native contact, joint, sleep and continuous-collision
controls are exposed where supported. CSG and GridMap remain available; their
obsolete collision-priority properties were removed.

These choices aim to make simulation behavior explicit and repeatable. They do
not establish cross-platform determinism, complete Godot physics parity or
automatic scene rollback. The ordinary Box3D scene adapter remains experimental.
See the [Box2D guide](doc/egp_box2d.md) and [Box3D guide](doc/egp_box3d.md) for
supported behavior, unsupported shapes and qualification limits.

### Networking: native Superpos

Superpos is an independent C++23 core integrated directly through
`modules/superpos`. Native `SuperposField` / `SuperposSchema` resources declare
bounded schemas; `SuperposSession` owns canonical state and packet delivery,
and `SuperposWorld` supplies scene ownership and optional physics-phase ticks.
GDScript, generated C# and generated C++ call the same native implementation.

Superpos is the only networking stack. The former Yojimbo transport, its
Superposition layer and `EGPNetSession` were removed; see the
[migration guide](doc/egp_superpos_migration.md) for the replacement APIs.
Automatic scene replication, solver prediction/recovery and production/WAN
behavior are not yet qualified.

Current application examples are the [physics showcase](demos/physics_superpos_showcase/README.md)
and [remote courier arena](demos/superpos_100_player_lab/README.md). Each documents
its tested workload, explicit scene projection and remaining qualification limits.

See [Superpos networking](doc/egp_superpos.md) and
[networking migration](doc/egp_superpos_migration.md) for actual APIs and limits.

### C++ tools and builds: extend the existing workflows

GDExtension and godot-cpp remain the C++ extension foundation. EGP bundles a pinned
SDK and adds editor tools for extension creation, toolchain checks, Debug/Release
builds and compatible reload, reducing the setup required by extension authors.
C# remains supported through Mono, with opt-in runtime reload and explicit state
handoff. Reload compatibility and recovery have limits documented in the
[runtime reload contract](doc/egp_api_contract.md#runtime-reload).

xmake owns native source generation, compilation, library creation and linking.
Native Lua generates engine source and embedded data; .NET/MSBuild compiles matching
managed assemblies after the native editor has generated its glue. The bundled
C++ SDK and extension projects also use xmake.

The [integration checklist](doc/egp_integration_loop.md) records the current
combined-engine evidence and remaining acceptance work. EGP is under active
development; backend replacement and passing focused tests do not mean every
feature is complete or the engine is release-ready.

## Building from source

On Windows, install Visual Studio's C++ tools, the Windows SDK and the .NET SDK,
then run from the repository root:

```powershell
.\misc\scripts\build_egp.ps1 -Setup
.\misc\scripts\build_egp.ps1 -Target editor
```

See the [xmake build guide](doc/egp_xmake.md) for editor/template variants,
platform SDK requirements, compiler choices and current qualification scope.

## Documentation

- [EGP documentation source and website](doc/egp_documentation.md)
- [Documentation fork](https://github.com/ZSG-Studios/EGP-docs) · [Website fork](https://github.com/ZSG-Studios/EGP-website)
- [C++ extensions](doc/egp_cpp_extensions.md)
- [Superpos networking](doc/egp_superpos.md)
- [Networking migration](doc/egp_superpos_migration.md)
- [Box2D physics](doc/egp_box2d.md)
- [Box3D physics](doc/egp_box3d.md)
- [API and runtime reload contracts](doc/egp_api_contract.md)
- [xmake builds](doc/egp_xmake.md)
- [Platform structure and validation coverage](doc/egp_platform_validation.md)
- [Experimental visionOS Forward+](doc/egp_visionos_experimental.md)
- [Godot documentation](https://docs.godotengine.org)

## Contributing

Find source, build workflows and project history in the [EGP-Engine repository](https://github.com/ZSG-Studios/EGP-Engine).
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

EGP uses Godot's [MIT license](LICENSE.txt). See [AUTHORS.md](AUTHORS.md)
and [COPYRIGHT.txt](COPYRIGHT.txt) for credits and third-party licenses.
