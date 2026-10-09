# Building EGP with xmake

EGP uses xmake 3.1.1 for its native engine, bundled C++ SDK and extension builds.
The Lua graph owns compiler detection, generated-file dependencies, compilation,
archives and linking. Native Lua generators and a compiled host compression tool
produce engine source and embedded assets. .NET/MSBuild and Android Gradle build
their language-specific assemblies and packages. Python is used only by optional
verification scripts.

## Windows editor and templates

In the maintained EGP workspace, a temporary policy requires the entire build
workflow to execute on the remote build PC, including generators, preprocessing,
compilation, linking, managed assemblies and packaging. Run these commands there
and reuse its canonical output. Xmake distributed compilation alone does not
satisfy this policy because client stages can still execute locally. Source
editing and checks that do not build remain permitted locally.

Install Visual Studio's C++ desktop workload and Windows SDK, and
the .NET SDK when building Mono. From the repository root:

```powershell
.\misc\scripts\build_egp.ps1 -Setup
.\misc\scripts\build_egp.ps1 -Target editor -Jobs 8
.\misc\scripts\build_egp.ps1 -Target all -Jobs 8
```

Setup verifies the pinned version; on Windows x64 it can download the official
self-contained binary and verifies its SHA-256 before installation. The launcher
builds editor, debug template and release template sequentially. Configuration
and object directories are separated by platform, architecture, target and
options, so switching variants does not reuse incompatible objects.

The launcher enables Mono and disables optional AccessKit and Direct3D12
by default. These dependencies must be installed before enabling their options.
For a native-only editor, or explicit compiler options:

```powershell
.\misc\scripts\build_egp.ps1 -Target editor -XmakeArgs @('module_mono_enabled=no')
.\misc\scripts\build_egp.ps1 -Target editor -XmakeArgs @('use_llvm=yes', 'debug_symbols=no')
.\misc\scripts\build_egp.ps1 -Target template_release -XmakeArgs @('use_mingw=yes')
```

Install optional Windows SDKs before enabling their build options:

```powershell
xmake lua misc/scripts/install_build_dependencies.lua d3d12
xmake lua misc/scripts/install_build_dependencies.lua accesskit
.\misc\scripts\build_egp.ps1 -Target editor -XmakeArgs @('d3d12=yes', 'accesskit=yes')
```

The installer and engine share the same dependency root. Set `EGP_BUILD_DEPS`
to choose it explicitly; otherwise native Windows uses
`%LOCALAPPDATA%/Godot/build_deps`, while MSYS or hosts without `LOCALAPPDATA` use
the repository's `bin/build_deps`. Relative overrides resolve from the engine
repository. For example:

```powershell
$env:EGP_BUILD_DEPS = 'D:/EGP-SDKs'
xmake lua misc/scripts/install_build_dependencies.lua d3d12
.\misc\scripts\build_egp.ps1 -Target editor -XmakeArgs @('d3d12=yes')
```

`mesa_libs`, `agility_sdk_path`, `pix_path` and
`accesskit_sdk_path` override individual SDK locations. Mesa selects
an installed architecture/compiler variant when given an unsuffixed base;
an explicit selected directory is retained. Source recipes, linking and DLL
packaging use these same resolved paths. Clang-cl uses the MSVC SDK variant;
MinGW uses the GCC or LLVM variant selected by `use_llvm`.

For MinGW with Direct3D12, install `gendef` and a compatible x64 GNU `dlltool`
or `llvm-dlltool`, then require GNU WinPix import-library conversion:

```powershell
xmake lua misc/scripts/install_build_dependencies.lua d3d12 install gcc
.\misc\scripts\build_egp.ps1 -Target template_release -XmakeArgs @('use_mingw=yes', 'd3d12=yes')
```

The MinGW installer fails if required x64 conversion cannot complete. MSVC and
clang-cl use the package's native import libraries; unavailable optional GNU
conversion does not prevent their installation.

After a Mono editor build, its own executable generates matching managed glue;
the .NET build scripts then compile GodotSharp. `-SkipManaged` skips this final
stage. Regenerate managed assemblies after changes to exposed native APIs.

## Rendering support

Rendered EGP projects use Forward+ through a supported RenderingDevice driver:
Vulkan, Direct3D12 or Metal, depending on the platform and enabled build features.
Compatibility, Mobile, OpenGL/OpenGL ES and ANGLE backends are removed. Existing
projects must set `rendering/renderer/rendering_method` and its mobile override
to `forward_plus`. Devices that cannot run a supported RenderingDevice driver
have no fallback renderer.

Headless servers and tooling retain the dummy backend through `--headless`.
WebGL and WebXR are removed; Web builds and exports are unsupported until a
RenderingDevice web backend is available.

## Platform configuration

The portable command configures and invokes actual native xmake targets:

```sh
xmake lua misc/scripts/build_egp.lua linuxbsd editor 8 .build/xmake-cache "arch=x86_64 module_mono_enabled=no"
xmake lua misc/scripts/build_egp.lua macos editor 8 .build/xmake-cache "arch=arm64"
xmake lua misc/scripts/build_egp.lua android template_debug 8 .build/xmake-cache "arch=arm64"
xmake lua misc/scripts/build_egp.lua ios template_release 8 .build/xmake-cache "arch=arm64"
xmake lua misc/scripts/build_egp.lua visionos template_release 8 .build/xmake-cache "arch=arm64"
```

| Engine platform | xmake platform/toolchain | Required SDK |
| --- | --- | --- |
| Windows | windows/MSVC or clang-cl; mingw/MinGW | Windows SDK and selected compiler |
| Linux/BSD | linux/GCC or Clang | Host compiler, platform headers and enabled driver dependencies |
| macOS | macosx/Xcode | Xcode macOS SDK; separate x86_64 and arm64 builds |
| Android | android/NDK | Android SDK and NDK 29.0.14206865, API 24 or newer |
| iOS | iphoneos/Xcode | Xcode iPhoneOS or iPhoneSimulator SDK |
| visionOS | cross/egp-visionos | Xcode xros/xrsimulator SDK; arm64 |

Platform definitions and build recipes do not by themselves establish successful
cross-platform compilation or runtime support. Retain receipts from each actual
platform build. Existing mobile CI variants disable the desktop-only EGP
physics backends explicitly; that capability boundary remains unchanged.

Options retain engine names such as `dev_build`, `debug_symbols`, `precision`,
`use_asan`, `use_ubsan`, `use_tsan`, `lto`, `vulkan`, `metal`, `d3d12` and
`module_mono_enabled`. Unknown options fail configuration instead of being
silently ignored. Sanitizer combinations must be supported by the compiler;
ThreadSanitizer and AddressSanitizer cannot be combined.

Use `compiledb=yes` to export `compile_commands.json` for clangd tools. Compiler
failures propagate to the launcher and CI; inspect their original diagnostics.
Append `dry-run` after the option string to print configuration/build commands
without starting compilation. CI installs the same pinned xmake and preserves separate
matrix variants and their runtime checks.
