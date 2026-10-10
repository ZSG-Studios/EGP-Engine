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

EGP contains only Superpos. The previous `modules/egp_net` module, its build
option and recipe, the Yojimbo vendor sources, language helpers and dedicated
validators were removed on 2026-10-10. To compare against the old stack, check out
an engine revision from before that removal. Historical integration receipts remain
evidence for their recorded revisions only.

Use the [current network lab guide](egp_network_lab.md) for Superpos fixtures
and application demos. The examples project canonical state explicitly and
do not restore the former RPC, spawner or networking physics contracts.

See [the native networking guide](egp_superpos.md) for supported APIs and limits.
