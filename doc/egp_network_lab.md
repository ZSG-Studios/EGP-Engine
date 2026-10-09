# Superpos network lab

Use the native Superpos module, not the retired network lab helpers.
The former dedicated/listen-host, 52-player arena and three-language helper
fixtures target a different protocol and do not qualify this replacement.

The current engine fixture is `misc/egp/superpos_lab`. It exercises explicit
schemas, local canonical publication, owner retirement and authenticated
separate-process loopback packet delivery with the built engine.
Run `python misc/scripts/validate_superpos.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe`.
Diagnostics are replaced in place under `.build/diagnostics/superpos`.

See [Superpos networking](egp_superpos.md) and [migration](egp_superpos_migration.md).
Broader reconnect/recovery, rendered gameplay, old demo conversion and export
qualification remain separate acceptance work.
