# Native desktop EGP networking profile. SCons: profile=misc/egp/litenet_profile.py
# These modules are excluded from editor/export binaries. Upstream sources stay
# in Git for merges; no second transport or scene replication executes at runtime.
module_mono_enabled = True
module_litenet_enabled = True
module_box3d_enabled = True
module_enet_enabled = False
module_multiplayer_enabled = False
module_webrtc_enabled = False
# WebSocket also serves the editor debugger and generic web APIs, so keep it.
# Its overlapping multiplayer peer is excluded by SCsub in LiteNet builds.
