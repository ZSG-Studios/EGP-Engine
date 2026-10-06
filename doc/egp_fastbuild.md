# EGP builds with FASTBuild

EGP's Windows x86_64 MSVC build supports FASTBuild v1.20. SCons continues to
manage generated headers/sources, dependency scanning, resources, libraries and
linking. Its C and C++ object actions submit dirty compilation batches as native
FASTBuild `ObjectList` nodes. This includes engine modules and their third-party
sources. After an editor build, the launcher generates C# glue with the headless
editor and builds the managed assemblies using Godot's .NET build scripts locally.

## Build on this PC

From the EGP repository root in PowerShell:

```powershell
# Install the pinned project-local tools and check the WireGuard worker.
.\misc\scripts\build_egp.ps1 -Setup -CheckWorker

# Build the C# editor, with distributed compilation enabled by default.
.\misc\scripts\build_egp.ps1 -DistVerbose

# Windows C# export templates.
.\misc\scripts\build_egp.ps1 -Target template_debug
.\misc\scripts\build_egp.ps1 -Target template_release

# Or build the editor and both templates sequentially.
.\misc\scripts\build_egp.ps1 -Target all -Jobs 6
```

The launcher downloads FASTBuild v1.20 from its official distribution if absent,
reuses `.build/venv`, and installs SCons 4.11.1 when needed. Visual Studio's x64 C++
toolchain, Windows SDK and .NET SDK must be installed on the initiating PC.
It uses the current EGP baseline's `angle=no accesskit=no d3d12=no` settings;
Vulkan and OpenGL remain available. Install the respective Godot dependencies
and override these options if those optional drivers are needed.

Pass additional SCons options explicitly:

```powershell
.\misc\scripts\build_egp.ps1 -Jobs 6 -SConsArgs @('dev_build=yes', 'debug_symbols=no', 'optimize=none')
.\misc\scripts\build_egp.ps1 -Local
.\misc\scripts\build_egp.ps1 -SkipManaged # Rebuild only native engine code.
```

FASTBuild also has checked-in entry points:

```powershell
$env:FASTBUILD_WORKERS = '10.77.64.1'
.\.build\fastbuild\tool\FBuild.exe -dist egp-editor
.\.build\fastbuild\tool\FBuild.exe -dist egp-template-debug
.\.build\fastbuild\tool\FBuild.exe -dist egp-template-release
.\.build\fastbuild\tool\FBuild.exe -dist all
```

Those entry points invoke the same SCons launcher, which dispatches compilation
to FASTBuild. Use the launcher's `-Jobs`, `-Local`, `-DistVerbose` and `-SConsArgs`
parameters to customize the nested compilation; outer FASTBuild command-line
options do not propagate to the nested build. The root entry points always run
SCons, letting its dependency checks decide whether any work is needed.
`all` runs the editor and the two template targets sequentially so their shared
SCons configuration and managed glue metadata cannot race.

Direct SCons use is also supported:

```powershell
.\.build\venv\Scripts\python.exe -m SCons -j6 platform=windows target=editor arch=x86_64 module_mono_enabled=yes angle=no accesskit=no d3d12=no fastbuild=yes fastbuild_distverbose=yes
```

Normal SCons builds remain available with `fastbuild=no`. Do not combine this
backend with Ninja, compilation-database generation, or compiler launchers.
The current backend supports MSVC x64; use the normal build for other toolchains.
Embedded debug information (`/Z7`, Godot's default) is supported; shared compiler
PDBs (`/Zi`), PCH, `/clr` and `/GL` are rejected. Resources and final linking run
locally. SCons incremental checks are authoritative: each submitted dirty batch
uses FASTBuild `-clean`, while up-to-date objects are never submitted.

## WireGuard worker

The default worker is `10.77.64.1`, TCP port `31264`, over the existing
`build-pc-wireguard` tunnel. Keep the tunnel active and the remote
`FBuildWorker.exe` v1.20 running in a mode that accepts work. The client config
has already been imported on this PC. Keep private VPN configuration outside
the repository.

```powershell
Test-NetConnection 10.77.64.1 -Port 31264
.\misc\scripts\build_egp.ps1 -Setup -CheckWorker
```

`FASTBUILD_WORKERS` is read by EGP and written to FASTBuild's explicit
`Settings.Workers` array. No SMB share or brokerage directory is required.
Override it with `-Workers '10.77.64.1;10.77.64.3'` or the environment variable.

The initiating PC synchronizes `cl.exe` and its supporting compiler DLLs to the
worker. SDK headers are preprocessed locally. The worker therefore does not need
an independently installed matching MSVC toolchain for this integration.
If the worker is unavailable, normal distributed builds can compile locally.
`-ForceRemote` disables that fallback and requires a successful connection check;
use it for diagnostics only.

## Validate real remote compilation

```powershell
.\.build\venv\Scripts\python.exe tests\python_build\validate_fastbuild.py --remote
```

The fixture requires remote compilation with local racing disabled. It checks C
and C++, generated-header ordering/invalidation, duplicate source basenames,
paths containing spaces, SConscript-local include directories, libjpeg-style
object variants, quoted header macros, command-line definition changes, no-op rebuilds, and
compiler-error propagation. Outputs live in `.build/fastbuild validation`.
Engine compilation batches and their generated BFF/database files live under
`.build/fastbuild/batches`. This fixture validates the build integration; it does
not qualify engine runtime behavior.

Setup verification completed a distributed Windows x64 C# development editor
build with `dev_build=yes debug_symbols=no optimize=none`, including native
linking, headless glue generation, Debug/Release API assemblies, editor tools
and SDK packages. Real compilation results were returned by `10.77.64.1`.
The forced-remote integration fixture also passed.

The follow-up build completed the Windows x64 C# development editor plus debug
and release export templates from a fixed source copy in `.build/fb-source`.
The editor includes the embedded C++ SDK API; its Debug/Release C# API assemblies,
editor tools and SDK packages are built. All three executables passed hidden,
headless startup checks with Box2D and Box3D active. Template checks load a small
PCK beside each executable, matching the default restriction on path overrides.
Editor project import exits successfully after guarding a deferred documentation
callback during shutdown, but reports unsupported Box physics features including
soft bodies, separation rays and world boundaries.

Outputs are in `.build/fb-source/bin`. Exact source and executable hashes are
recorded in `.build/fastbuild-build-receipt.json`; import diagnostics are preserved
in `.build/fastbuild-editor-import-receipt.json`. Other chats continued modifying
the main checkout during this build. These outputs use the captured LiteNet
implementation and do not include the later Yojimbo migration or subsequent
feature changes. Rebuild the main checkout to incorporate those changes.

References: [FASTBuild v1.20 downloads](https://www.fastbuild.org/docs/download.html),
[distributed compilation](https://www.fastbuild.org/docs/features/distribution.html),
[compiler synchronization](https://www.fastbuild.org/docs/functions/compiler.html),
[explicit workers](https://www.fastbuild.org/docs/functions/settings.html).
