# Box3D source receipt

Source: https://github.com/erincatto/box3d
Revision: e77352cd606dc1a34209094076199549a52ea0a1
License: MIT (LICENSE retained).

The include, src, test and shared source directories and LICENSE are retained.
The single EGP patch (modules/box3d/upstream_patches/egp-box3d.patch: Generic6DOF and
cone joints, wheel-joint angular separation, world contact tuning and speculative
getters) is recorded in UPSTREAM.json with its upstream and patched file hashes. Upstream CMake
build definitions were removed for the native xmake migration; their original
digests remain in excluded_upstream_files, including the previously patched
src/CMakeLists.txt digest. This removal does not recertify or alter vendor code. Samples, graphics dependencies and docs are not
vendored. EGP's adapter and compile options live in modules/box3d and
tests/physics/box3d. UPSTREAM.json lists SHA-256 hashes with CRLF normalized to LF
so Git line-ending conversion does not invalidate the receipt. Update source,
profile fingerprints, reference trajectory and qualification evidence together.
