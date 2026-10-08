# Managed engine assemblies

Build the native engine with `module_mono_enabled=y` and install the .NET SDK
selected by `global.json`. The Windows launcher performs native and managed
builds together:

```powershell
.\misc\scripts\build_egp.ps1 -Target editor
```

For a direct cross-platform build, generate glue with the exact final Mono
editor, then invoke the native Lua managed launcher from the engine root:

```sh
<editor> --headless --generate-mono-glue modules/mono/glue
xmake lua build/xmake/managed.lua <editor> <platform> single
```

`<platform>` is `windows`, `linuxbsd`, `macos`, `android`, or `ios`. Replace
`single` with `double` only for a matching double-precision executable. The
launcher builds Debug/Release API assemblies, editor tools, and SDK packages
under `bin/GodotSharp`. Regenerate glue after changing the ClassDB API.

For a local NuGet source:

```sh
dotnet nuget add source <package-directory> --name EGPDevelopment
xmake lua build/xmake/managed.lua <editor> <platform> single "push-nupkgs-local=<package-directory>"
```

Pass `no-deprecated` for a native engine built with `deprecated=n`, and `werror`
to treat managed warnings as errors. Keep editor binaries, generated glue,
assemblies, extension SDKs and export templates from the same engine revision.
