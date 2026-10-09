# Migrating networking to Superpos

Superpos replaces Yojimbo in EGP's active desktop build. It is the native
`modules/superpos` integration of the independent Superpos core, not a renamed
Yojimbo dependency and not the old Superposition property/RPC layer.

| Previous integration | Superpos migration |
| --- | --- |
| `EGPNetSession`, `EGPNet`, `EGP.Networking`, `egp::networking` | Use native `SuperposSession` / `SuperposWorld` and matching generated language bindings. |
| `SuperpositionWorld`, property components, spawners and typed RPC | Author `SuperposField` / `SuperposSchema`; implement application scene projection and message dispatch explicitly. Automatic equivalents are not available. |
| Yojimbo connect tokens and host/join APIs | Provision a new native association through `configure_udp()` and check actual admission readiness; old tokens are incompatible. |
| Implicit entity/counter integer assumptions | Preserve exact unsigned bits and check result availability with `SuperposUInt64` and checked reads. |
| Old networking Box3D adapter / complete-world prediction | Register and qualify a native simulation provider; no automatic solver migration is promised. |
| Old network lab and 52-player arena results | Rebuild fixtures against Superpos. Earlier transport receipts do not qualify the replacement. |
| Old session detach/resume helpers | Follow Superpos owner retirement and managed reload contracts; do not reuse legacy tickets or facades. |

Back up application projects, rebuild the editor/templates and regenerate both
managed and C++ SDKs from the same API. Migrate schemas, credentials, protocol,
ownership, lifecycle and presentation together. Test local canonical behavior,
authenticated separate-process communication, reconnect/recovery, exports and
application gameplay independently.

The previous module is excluded from the build graph, its build target and
public build option are removed, and its ClassDB API is retired. Physical
legacy sources remain on disk because automatic approval rejected directory
deletion; they are not compiled by the supported engine.

See [the native networking guide](egp_superpos.md) for supported APIs and limits.
