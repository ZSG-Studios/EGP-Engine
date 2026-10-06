# Built-in C++ extension workflow

EGP includes a pinned godot-cpp SDK in the editor executable. Extension authors
do not need to download godot-cpp, generate bindings, or install Python/SCons.
The native compiler and CMake 3.20 or later are required. On Windows, use Visual
Studio with the C++ desktop workload; CMake selects the installed Visual Studio
toolchain automatically. Linux and macOS use the host CMake/compiler toolchain.
Windows is the initial validation platform.

## In the editor

1. Open **Project → Project Settings → GDExtensions**.
   Select **Check Toolchain** to verify CMake, C++17 compilation, and linking.
   Select **Install Tools** if dependencies are missing; installer output appears
   in the panel. Complete any operating-system installation/admin prompts.
2. Enter a lowercase C++ identifier, such as `movement`, and select **Create Extension**.
3. Edit `res://extensions/movement/src/extension.cpp`. The sample registers
   `EGP_movement_Node` with a callable `get_message()` method.
4. Select **Build Debug**. The first build compiles the bundled bindings; later
   extensions reuse the SDK library cache across projects.
5. Compiler diagnostics link to the source line in the editor's text editor.
6. A successful build loads or reloads the extension. If loading needs a restart,
   use **Save Scenes and Restart Editor**. Build failures retain the previously
   published descriptor and library.
7. Select **Build Release** before a release export. Standard GDExtension export
   handling includes the library selected by the project's export features.

The CMake executable defaults to `cmake` on PATH and can be changed in the panel.
The editor also detects the standard Windows CMake installation, the macOS CMake
app, and Homebrew's Apple Silicon installation. Windows setup uses WinGet and
reuses an existing Visual Studio C++ workload. Linux setup supports apt, dnf,
pacman, and zypper through polkit. macOS requests Apple's Command Line Tools and
uses an existing Homebrew installation for CMake, or opens its official installer.
Stop a running game before rebuilding. Extension source files and CMake settings
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

`misc/scripts/egp_cpp_editor_smoke.gd` runs the actual editor panel in a disposable
project. It checks scaffolding, compilation, shared SDK reuse by a second extension,
custom-node registration, a deliberate compile failure with descriptor preservation,
source-line navigation, saving edited source, rebuilding/reloading changed code,
and debug/release library mappings. Run it with a locally built EGP editor:

```sh
EGP --headless --editor --path <disposable project> --script <absolute path to egp_cpp_editor_smoke.gd>
```

The test expects a new project without an existing `extensions/smoke` directory.
Windows x64 validation passed with MSVC and CMake. A packaged Debug game also
loaded the extension and verified its rebuilt C++ method using the validation
editor as a custom export template. The editor-script harness reports RID/Object
cleanup warnings at exit; the packaged-game check exits without those warnings.
These checks do not establish memory-cleanup qualification, platform parity, or
production export-template qualification. The validation editor build disabled
Mono, D3D12, and AccessKit; their integration is outside this check.

`misc/scripts/validate_egp_cpp.py` checks the built-in CLI, error exit codes, and
actual Debug/Release exports using separately built templates. The
`EGP C++ editor and exports` workflow runs it on Windows x64, Linux x64, and
macOS ARM64 and retains logs and timings. Its headless validation builds disable
rendering backends, Mono, and AccessKit. Workflow results qualify the tested
architecture and features only; adding a workflow does not establish a passing
result. Native CLI toolchain installation/check and extension Debug/Release builds
have passed locally on Windows without the script-harness cleanup warnings.
