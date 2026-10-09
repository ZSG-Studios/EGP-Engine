# LiteEntitySystem provenance

Source: https://github.com/RevenantX/LiteEntitySystem
Commit: d7448a8ac1353259a6feb07c9612d4bfc733f329
Library version: 1.2.2
License: Apache-2.0, retained with NOTICE.

C# sources, RefMagic.il/RefMagic.dll and the shipped Roslyn analyzer are pinned.
They compile into EGP.Networking. RefMagic.dll remains a separate packaged runtime
dependency. Do not reference a second LiteEntitySystem assembly in an EGP game.

EGP changes:

- EntityManager simulation tick start/completion hooks; completion is before
  server serialization and runs during both client replay loops.
- Client physics rollback, authoritative state and baseline hooks.
- Client short-header/type checks and bounded baseline decompression metadata.
- Bounded pending diffs, validated diff fragment metadata, RPC payload framing,
  schema-sized entity deltas before unsafe copies/interpolation, and finite input
  interpolation factors (ClientEntityManager, ServerEntityManager, ServerStateData).
- EntityManager.Reset walks retained entity slots, including destroyed entities
  awaiting replication acknowledgement, instead of only live entity filters.
- RPCRegistrator validates parameterless, fixed-value and span RPC payload sizes
  before invoking game handlers or dereferencing unmanaged values.
- EntityManager reserves an empty human-controller base filter for controller-free
  replication schemas, which the upstream client tick/replay paths enumerate.

These changes are engine integration points, not proof that all upstream unsafe
decoders are hardened against hostile traffic. Review each upstream update and
rerun networking and physics integration qualification.
