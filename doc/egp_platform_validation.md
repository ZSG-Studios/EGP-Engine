# Platform builds and validation

EGP uses one native Xmake build graph for Windows, Linux/BSD, macOS, Android,
iOS and experimental visionOS. Rendered projects use Forward+ with a supported
RenderingDevice driver. Headless tools and servers use the dummy backend.
Web rendering and exports are unsupported.

## Source ownership and build layout

| Location | Responsibility |
| --- | --- |
| `xmake.lua` | Public engine targets: editor, debug template and release template |
| `build/xmake/options.lua`, `defaults.json`, `platform_defaults.lua` | Declared options and platform defaults |
| `build/xmake/graph.lua`, `common.lua` | Shared engine source graph and capability policy |
| `build/xmake/platforms/` | Host/compiler normalization, SDK selection, compile/link policy and packaging |
| `build/xmake/recipes/platform/<platform>/` | Platform source and generator recipes |
| `platform/<platform>/` | Platform runtime, export integration and required platform packaging projects |
| `build/xmake/recipes/modules/`, `modules/` | Build capability contracts and module runtime code |
| `drivers/`, `servers/rendering/` | Graphics APIs and shared Forward+ rendering |
| `build/xmake/tests/`, `tests/build/` | Native graph, compiler, generator and packaging contracts |
| `misc/scripts/build_egp.lua` | Shared command-line and CI build launcher |
| `.github/actions/godot-build/` | Compatibility-named action that calls the shared launcher |
| `.github/workflows/runner.yml` | Static gate, native contracts and six reusable platform workflows |

Android Gradle and Apple Xcode projects are platform packaging inputs. They do
not replace the engine's native Xmake graph. Documentation uses Sphinx, the
website uses Jekyll, and managed C# code uses its supported .NET tooling.
Retained Web recipe fixtures test historical build contracts; they do not enable
a supported Web platform or add it to the shipping CI matrix.

## Configured platform coverage

This table describes checks wired into CI, not a claim that every configuration
has passed at the current source revision. Inspect the run and its receipts.

| Platform | Native build coverage | Runtime coverage and remaining boundary |
| --- | --- | --- |
| Windows | Editor/template variants and selected MSVC, clang-cl and MinGW configurations | Native unit-test summaries; GPU backend and physical device qualification need separate receipts |
| Linux/BSD | Linux editor/template variants, sanitizers and native contracts | Linux headless unit tests and startup checks; software Vulkan stereo in the visionOS workflow; BSD needs its own native host evidence |
| macOS | x86_64 and arm64 editor/release-template builds | Editor unit tests; arm64 Metal capability and external stereo checks in the visionOS workflow |
| Android | arm32/arm64 debug templates, pinned NDK and Gradle packaging | Firebase instrumentation is guarded to upstream Godot, so it does not run for EGP; Android device rendering and app lifecycle remain separate requirements |
| iOS | arm64 release template | Compilation is not a signed app, installation or device runtime test |
| visionOS | arm64 release template plus opt-in immersive debug library/template and unsigned app | Hosted Vulkan/Metal external-texture checks and Xcode linking; physical Vision Pro compositor, tracking, signing and lifecycle remain unverified |

Android export CI explicitly disables EGP's desktop physics backends. A successful
template build does not establish physics parity on mobile. Inspect each module's
capability contract before promising a feature on another platform.

The experimental visionOS workflow records native library compilation, template
integrity, stereo colour/depth/resize results and unsigned app export/linking
separately. Its initial Metal runtime profile uses native hazard tracking
(`GODOT_MTL_SYNC_MODE=none`). Default manual-barrier synchronization and physical
headset operation require separate qualification. See the
[visionOS guide](egp_visionos_experimental.md).

## Outputs, caches and diagnostics

Publish current runnable binaries and platform handoff packages under `bin/`.
Keep compiler objects, generated files, platform/configuration variants and
dependency caches beneath `.build/`. The shared launcher's option-derived cache
key separates incompatible configurations; it is not an archive of successive
complete runnable packages.

Reuse the same configuration and diagnostics location for subsequent runs.
Keep compact logs, receipts and necessary captures in `.build/diagnostics/`;
replace superseded packages after a verified replacement. Do not retain downloaded
SDKs, duplicate source trees, binaries or artifact ZIPs solely as evidence.
Preserve required build inputs, credentials and outputs used by live processes.
Review absolute cleanup targets before deleting anything; never retry a rejected
cleanup action.

For compatible Windows MSVC projects, use the configured Xmake distributed
compiler. Connect under the same `XMAKE_CONFIGDIR` used by the build. Count actual
`distc compiling` jobs and inspect preprocessing/fallback counts; a connection or
cache hit alone does not establish remote compilation. Local resource compilation
and linking are expected. Keep service configurations and SSH keys private.

## Validation gates and evidence

1. Run static formatting, XML/API and source-policy checks.
2. Run native build contracts: options, six platform graphs, module capability,
   generated headers/objects, SDK bootstrap, compiler/linker policy and packaging.
3. Compile each supported target on its required host/SDK. Retain the compiler,
   architecture, source commit, effective options and artifact SHA256.
4. Run executable tests and validate their summaries. Exit code zero with no
   executed tests is insufficient; logs and receipts must identify the tested scope.
5. Qualify graphics, export/import and application lifecycle separately. Record
   unavailable runner capabilities as not run, never as a runtime pass.
6. Complete physical-device testing for signing/install, XR tracking/compositor,
   input, lifecycle, performance, thermals and long sessions before release claims.

Useful focused checks from the engine root:

```powershell
xmake lua tests/build/test_xmake_workflows.lua
xmake lua build/xmake/tests/forward_only.lua
xmake lua build/xmake/tests/recipes.lua
xmake lua build/xmake/tests/engine_tests.lua
```

The complete native contract workflow calls
`misc/scripts/validate_xmake_contract.lua` with the project's built compression
and ZIP tools. Its receipts live under `.build/xmake-contract/`. For actual build
commands and SDK requirements, use the [Xmake guide](egp_xmake.md).
