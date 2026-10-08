# Native integration qualification

The engine's public build entrypoint is `xmake lua misc/scripts/build_egp.lua`; see [the native build guide](../../doc/egp_xmake.md) for editor and template builds.

`qualification.lua` defines isolated native library and test targets used by verification scripts. It builds the checked-in sources directly with xmake and shares neither project configuration nor output directories with the primary engine build. Run the relevant validator for its selected targets and bounded runtime checks. Passing a standalone check does not establish editor, platform export or managed assembly compatibility.

`sdk-qualified.lua` packages the native SDK using an actual engine API dump and the native host compressor. Its receipt records the API, generated header and compressor identities. The SDK's ABI and ClassDB metadata must match the final engine; compiled extension loading and managed binding checks remain separate qualification steps.
