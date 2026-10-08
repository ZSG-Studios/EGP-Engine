# EGP's pinned C++ SDK source

This folder contains the native godot-cpp sources used by EGP's offline extension
SDK. The pinned upstream revision and port qualification are recorded in
[UPSTREAM.md](UPSTREAM.md); the original [MIT license](LICENSE.md) is preserved.

Build the engine and its matching SDK from the repository root:

```sh
xmake lua misc/scripts/build_egp.lua windows editor 8 .build/xmake-cache "module_mono_enabled=y"
```

The native graph exports the actual engine API and runs EGP's Lua binding emitter
before packaging the editor. Standalone extension projects use the packaged
`xmake.lua` and pre-generated headers. See
[the C++ extension guide](../../doc/egp_cpp_extensions.md) for editor setup,
builds, runtime reload, and export.

The upstream test extension can also be built with
`xmake lua misc/scripts/build_egp_cpp_extension.lua`, supplying the generated SDK,
project directory, platform, architecture, output name, and optional native
documentation compressor. No generated bindings are checked into this folder.
