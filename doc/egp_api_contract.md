# EGP API exposure contract

EGP's native networking API is Superpos. `SuperposSession`, `SuperposWorld`,
`SuperposField`, `SuperposSchema` and `SuperposUInt64` are native ClassDB classes.
Generated GDScript/C#/C++ consumers share this implementation; the old networking
helper facades and Superposition nodes are retired.

See [Superpos networking](egp_superpos.md) for checked ownership/counter reads,
canonical publication, packet lifecycle and unsigned identifier contracts.
The independent core uses C++23 privately; engine/SDK public headers use C++17.
Schemas are explicit declarations. No gameplay script executes during baking.

## Runtime reload

Runtime reload is opt-in for editor-run games through
`debug/hot_reload/enable_runtime=true`. Export templates disable it.
Rebuild managed glue and C++ extensions from the same editor API; development
version numbers alone do not establish ABI compatibility.

Compatible object identity, state and callback retention require matching
engine reload contracts. Application threads and static state need application
lifecycle management. Removing native classes or changing a base/ABI can require
repair or restart. See [C++ tools](egp_cpp_extensions.md).

Superpos has its own owner retirement and managed reload integration. Historical
reload receipts from the old transport do not qualify Superpos. Full current
engine, SDK, managed reload, templates and application behavior are separate
checks; consult the current cutover receipt before making a runtime claim.
