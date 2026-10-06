# Native Box2D scene adapter

Adapted from [erincatto/godot-box2d](https://github.com/erincatto/godot-box2d),
revision `66260bc0eb77a9e6eb78b80cc163ed2c912b448b`, MIT, Andrew Song.
Original source hashes and license are retained. EGP's source is modified;
`thirdparty/box2d` remains pinned and unchanged at the adapter's exact solver
submodule revision `56edae79f2949d86142b03450d5d60f63bcf5a6f`.

EGP uses the native PhysicsServer2D interfaces, RID ownership, engine types,
WorkerThreadPool and native query structures. The port adds fixed stepping,
stable RID traversal, process serialization, query exclusion preservation,
stable sensor overlap records, survivor detachment and deferred-delete cleanup.
It supports fixed-world pin/spring anchors and primitive shape collision queries.
Concave segment arrays use disjoint point pairs, matching Godot's shape data API.

The backend is mandatory and default whenever 2D physics is enabled. See
[the integration contract](../../doc/egp_box2d.md) for known parity gaps.
