# EGP

EGP is a fork of [Godot Engine](https://github.com/godotengine/godot),
maintained by ZSG-Studios and based on upstream `master`.

## Features

- Box2D and Box3D physics.
- Yojimbo networking with GDScript, C# and C++ APIs.
- Superposition property replication configured through the Inspector.
- Built-in C++ extension tools and a bundled godot-cpp SDK.
- Native xmake builds with platform toolchains and incremental dependencies.

## What EGP replaces, and why

EGP keeps Godot's editor, scene system and scripting model, while choosing one
physics backend per dimension and one multiplayer transport. This gives the fork
one set of simulation, networking and lifecycle contracts to integrate and test
across GDScript, C# and C++.

| Removed system | EGP replacement | Reason for the change |
| --- | --- | --- |
| Godot Physics 2D | Native Box2D integration | Use a pinned solver with an explicit fixed-step profile, substeps and worker settings, while keeping `PhysicsServer2D` and ordinary 2D physics nodes as the game-facing API. |
| Godot Physics 3D and Jolt, including the Jolt vendor dependency | Native Box3D integration | Use the Box solver family for both dimensions and expose an explicit 3D world with ordered entity commands, fixed ticks and local full-world snapshots for authoritative simulation and replay. |
| Godot's high-level multiplayer/RPC stack, ENet, WebRTC and `WebSocketMultiplayerPeer` | Native Yojimbo transport with `EGPNetSession` and shared networking helpers | Make encrypted token admission, authoritative entity ownership, interest, replication and bounded traffic part of one explicit protocol, with matching GDScript, C# and C++ APIs. |

### Physics: consistent integration and explicit simulation

Box2D is the sole 2D backend and Box3D is the sole 3D backend. Old backend
selections migrate to the corresponding Box backend. Scene physics continues to
use Godot's physics clock; `EGPBox3DWorld` separately gives applications control
over fixed ticks, stable body identities, ordered commands and capture/restore.
The networking adapter connects that explicit world to the authoritative server
clock and checks the simulation profile and tick rate.

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

### Networking: one native protocol across languages

Yojimbo supplies the native transport, reliable messaging and encrypted admission
foundation. EGP adds session lifecycle, entity replication, ownership, interest,
traffic budgets and diagnostics. Higher-level helpers share the same codec,
prediction and physics adapters across languages; low-level native session APIs
can operate without those GDScript helpers.

Superposition adds property selection to the Inspector: add the component beneath
a gameplay node, point it at the network session and select the properties to
replicate. It shares the native session's entity lifecycle and validation. Use
the native snapshot interpolator for fast motion and Superposition for gameplay
values. See the [Superposition guide](modules/egp_net/SUPERPOSITION.md).

This is a deliberate API break: existing `Node.rpc`/`rpc_id`, multiplayer
authority calls, `MultiplayerAPI`, scene replication nodes and legacy multiplayer
peers need migration to EGP's explicit messages and entity APIs. General-purpose
HTTP, TCP/UDP and `WebSocketPeer` remain available, including WebSocket support for
editor debugging. Production accounts, token delivery and game persistence still
belong to the application/backend. Field deltas, lag compensation, automatic
client physics rollback and broader scale/platform qualification remain open.
See the [networking guide](modules/egp_net/README.md),
[network lab](doc/egp_network_lab.md) and
[API contracts](doc/egp_api_contract.md) before porting a multiplayer project.

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
- [Networking](modules/egp_net/README.md)
- [Superposition replication](modules/egp_net/SUPERPOSITION.md)
- [52-player physics arena](demos/box3d_arena/README.md)
- [Deterministic prediction and replay demo](demos/box3d_deterministic/README.md)
- [Network lab](doc/egp_network_lab.md)
- [Box2D physics](doc/egp_box2d.md)
- [Box3D physics](doc/egp_box3d.md)
- [API and runtime reload contracts](doc/egp_api_contract.md)
- [xmake builds](doc/egp_xmake.md)
- [Godot documentation](https://docs.godotengine.org)

## Contributing

Report bugs and suggest changes through [GitHub issues](https://github.com/ZSG-Studios/EGP/issues).
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

EGP uses Godot's [MIT license](LICENSE.txt). See [AUTHORS.md](AUTHORS.md)
and [COPYRIGHT.txt](COPYRIGHT.txt) for credits and third-party licenses.
