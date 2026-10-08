# Vendored godot-cpp provenance

This source snapshot comes from the official
[godot-cpp repository](https://github.com/godotengine/godot-cpp), pinned at
`507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`.

EGP preserves the C++ sources, public headers, interface descriptions, tests,
and MIT license. Build definitions and executable-language generators were
replaced by EGP's native xmake graph and Lua SDK emitters under
`build/xmake/generators`. The SDK is generated from the actual engine API dump.

The port was checked against the pinned generator for all combinations of
32-bit/64-bit pointers and single/double precision. Each configuration produced
2,201 files with identical bytes. Keep ABI validation and the upstream license
when updating this snapshot.
