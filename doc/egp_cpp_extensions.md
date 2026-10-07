# Built-in C++ extension workflow

EGP includes a pinned godot-cpp SDK in the editor executable. Extension authors
do not need to download godot-cpp, generate bindings, or install Python/SCons.
The native compiler and CMake 3.20 or later are required. On Windows, use Visual
Studio with the C++ desktop workload; CMake selects the installed Visual Studio
toolchain automatically. Linux and macOS use the host CMake/compiler toolchain.
Windows is the initial validation platform.

## In the editor

1. Open **Project â†’ Project Settings â†’ Addons â†’ GDExtension**.
   Select **Check Toolchain** to verify CMake, C++17 compilation, and linking.
   Select **Install Tools** if dependencies are missing; installer output appears
   in the panel. Complete any operating-system installation/admin prompts.
2. Enter a lowercase C++ identifier, such as `movement`, and select **Create Extension**.
3. Edit `res://extensions/movement/src/extension.cpp`. The sample registers
   `EGP_movement_Node` with a callable `get_message()` method.
4. Select **Build Debug and Load**. The first build compiles the bundled bindings; later
   extensions reuse the SDK library cache across projects.
5. Compiler diagnostics link to the source line in the editor's text editor.
6. A successful build loads or reloads the extension. If loading needs a restart,
   use **Save Scenes and Restart Editor**. Build failures retain the previously
   published descriptor and library.
7. Select **Build Release for Export** before a release export. Standard GDExtension export
   handling includes the library selected by the project's export features.

The panel groups the workflow into Toolchain, Create, and Edit and Build. The
selected source path appears below the extension selector; **Open Source** opens
it directly, and **Refresh** discovers extensions added outside the editor.
Build controls require an extension and honor the running-game reload rules.
A persistent status line reports configuration, compilation, load/restart, or
failure with the next action. **Copy Output** copies the compiler log for reports.
The first Debug build compiles the SDK and may take longer than later builds.

The CMake executable defaults to `cmake` on PATH and can be changed in the panel.
The editor also detects the standard Windows CMake installation, the macOS CMake
app, and Homebrew's Apple Silicon installation. Windows setup uses WinGet and
reuses an existing Visual Studio C++ workload. Linux setup supports apt, dnf,
pacman, and zypper through polkit. macOS requests Apple's Command Line Tools and
uses an existing Homebrew installation for CMake, or opens its official installer.
Running-game Debug rebuilds require opting in to runtime reload before launch;
see [the reload contract](egp_api_contract.md#runtime-reload). Stop the game before
Release rebuilds. Extension source files and CMake settings
belong in source control; generated `bin/` files and editor caches do not.
The descriptor is created after the first successful build. Hashed library copies
avoid overwriting a DLL already loaded by the editor. Older copies may be removed
after closing the editor; keep the copies referenced by the descriptor.

## Engine maintainers

Initialize the pinned dependency before building EGP:

```sh
git submodule update --init --depth 1
```

To build bindings for the exact engine API, including fork-specific classes, use:

```sh
python misc/scripts/build_egp_cpp_editor.py -- platform=windows -j4
```

Pass normal SCons flags after `--` (for example `module_mono_enabled=yes`). This
builds a bootstrap editor, dumps its API, then embeds matching bindings in the
final editor. Packaged C++ CI editors use this path. It is an engine-maintainer
step; extension authors need neither Python nor binding generation.

Direct SCons editor builds automatically generate and embed the default SDK. Export templates
do not include the editor SDK or its UI. The default bindings target the bundled
Godot 4.7 API snapshot, which is supported by the 4.8 engine baseline. To expose
new or fork-specific APIs, dump `extension_api.json` from the intended EGP binary
and rebuild the editor with `egp_cpp_api=<absolute path to the JSON>`. Retain an
API snapshot as a release-build input when publishing the corresponding editor.
Bindings match the engine build's pointer size and real-number precision. The
workflow initially builds for the local editor platform/architecture, rather
than configuring cross-compilation toolchains.

The SDK is extracted into the editor's cache directory under `egp_cpp/sdk`.
Short paths avoid Windows compiler path limits. Build caches are separate for
each project/extension; library caches are shared by SDK, compiler, platform,
architecture, and Debug/Release configuration. Extension projects remain ordinary
shared-library GDExtensions; changing an extension does not require rebuilding EGP.

Hot reload preserves existing extension objects when the native base remains
compatible. Changing method arguments or return types invalidates cached native
method bindings and prints a diagnostic; dynamic method lookup and ordinary
`Callable` lookup use the new signature. Update callers to match the signature.
Code holding raw cached method bindings needs a restart.

Changing a class's native base requires a restart. Removing a class, or rejecting
its base change, retains the registration record for compatible repair, including
in an editor without live instances. Existing objects remain as their original
native parent and retains their saved extension properties. The reload result is
`LOAD_STATUS_NEEDS_RESTART`. Restore the compatible class and reload to recover
its state, or restart to apply the new hierarchy. Parent properties can still be
edited during recovery. This does not guarantee recovery for arbitrary binary
layout, inheritance, or binding changes.

## Command line

The editor executable provides the same workflow without a project script:

```sh
EGP --headless --editor --path /path/to/project -- --cpp-check
EGP --headless --editor --path /path/to/project -- --cpp-install
EGP --headless --editor --path /path/to/project -- --cpp-create=movement --cpp-build=movement:debug
EGP --headless --editor --path /path/to/project -- --cpp-build=movement:release
```

Use `--cpp-cmake=/absolute/path/to/cmake` before a command to override discovery.
Use `--cpp-help` for usage. Commands run in order, stream output, and exit with 0
on success or 1 on failure. The project must contain `project.godot`. Builds wait
for the editor's file scan before advancing or exiting. Install commands can
require administrator approval; headless CI should provision tools in advance.

Installer references: [Microsoft C++ Build Tools](https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc),
[Apple Command Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/),
and [CMake downloads](https://cmake.org/download/).

## Validation

The disposable editor fixture exercises scaffolding, Debug/Release compilation,
shared SDK reuse, custom-node registration, compiler failure recovery, source-line
navigation, saving, and reloading changed code. Run it against your built editor:

```sh
python misc/scripts/validate_egp_cpp_ui.py --engine <EGP editor>
```

Add `--capture-ui` when the editor includes a renderer. On Windows, the fixture
starts its own hidden window and captures the ready panel, a deliberate compiler
error, and the completed Release build. It does not inject input. Successful
receipts require orderly editor shutdown without engine errors or leaked objects.
Receipts, compiler output, and screenshots remain under `.build/egp-cpp-ui`.

`misc/scripts/validate_egp_cpp.py` independently checks CLI error exit codes and
Debug/Release exported games using their matching templates. The **EGP C++ editor
and exports** workflow runs these checks, the headless panel fixture, direct typed
C++ API compilation, and engine networking fixtures on Windows x64, Linux x64,
and macOS ARM64. The Mono Linux build separately compiles the same typed C# API
surface against its freshly generated `GodotSharp.dll`. This direct assembly
reference avoids accidentally validating an older package from a NuGet cache.

To compile the networking API examples locally:

```sh
python misc/scripts/validate_egp_generated_api.py --cpp
python misc/scripts/validate_egp_generated_api.py --managed-assembly bin/GodotSharp/Api/Debug/GodotSharp.dll
```

These compile probes cover Superposition resources and nodes, pushed capture,
priority and interest settings, and snapshot interpolation. Runtime fixtures
exercise replication, interpolation, deterministic physics replay, and failure
paths separately. A successful compile receipt alone does not establish runtime
performance or parity across platforms.
