# Experimental immersive visionOS with Forward+

This opt-in prototype keeps EGP Forward+-only. It does not restore the Mobile
renderer. Physical Apple Vision Pro rendering, tracking, comfort, performance,
thermal behavior and lifecycle have not been qualified.

## What changed

Immersive startup is allowed only with `xr/visionos/experimental_forward_plus=true`,
Metal and the visionOS XR module. Startup enables XR shader variants before the
renderer is created, and rejects an explicit `--xr-mode off`.

The Compositor Services layer uses an unfoveated, layered RGBA16F color target and
D32Float+Stencil8 depth. Forward+ renders its ordinary stereo intermediate color
and tone maps into the external color texture; the external depth target is used
for depth rendering. Foveation remains disabled because Forward+'s compute and
screen-space passes do not implement Apple's logical-to-physical coordinate maps.

Texture imports are cached by native texture identity until XR shutdown. This
keeps earlier swapchain images valid for cached render bindings. The cache is
bounded to 32 color and 32 depth imports; it refuses further images instead of
accumulating unbounded imports. Restart the session if repeated resizing reaches
that limit. Dynamic render-quality changes are not qualified.

The first prototype requires HDR 2D, 3D render scale 1.0, and disabled MSAA and
temporal AA/upscaling. The renderer rejects incompatible buffers. Start with a
simple opaque scene and no post-processing. Mixed/progressive immersion, capture,
hand/controller interaction and pause/resume remain device follow-up work.

## Reproduce the automated stereo test

Use an editor built from the same commit as this source:

```sh
python3 misc/scripts/validate_visionos_forward_plus.py \
  --engine bin/godot.linuxbsd.editor.x86_64 --driver vulkan \
  --output .build/visionos-experimental
```

The fixture renders an asymmetric view of a box into two external RGBA16F/D32S8
texture layers, rotating through three texture pairs to mimic a swapchain. It
reads back color and sampled reverse-Z depth, checks both eyes
have visible geometry and different horizontal centroids, changes target size,
and repeats. It retains PNGs, numeric measurements, the binary hash and the full
log. Any engine/script error, timeout or missing success marker fails the test.
The Windows runner keeps the window hidden/offscreen, capped at 60 FPS with a
55-second watchdog. Linux CI uses Xvfb and Mesa's software Vulkan driver.

The `Experimental visionOS Forward+` workflow builds the device library and two
editors from the candidate commit. Linux runs the stereo test. macOS records GPU
and architecture capabilities and only runs the Metal test when the hosted
runner can execute the arm64 Metal editor. A `NOT_RUN.txt` artifact means runtime
validation was unavailable, not passed. Neither fixture exercises Apple
Compositor Services, foveation, ARKit or a headset.

## Hand off to a device owner

1. Build matching visionOS templates and a macOS editor with Xcode; the workflow's
   device archive is a compile artifact, not a signed, installable app.
2. Open `misc/egp/visionos_forward_plus/project.godot`. Export using the visionOS
   preset with application role **Immersive (experimental)**, initially **Full**
   immersion. Supply your own signing team and bundle identifier in Xcode.
3. Run without `--mock-xr`. The project initializes the real visionOS interface.
   Keep the supplied HDR, scale and AA settings. Read the startup warning and
   periodic `EGP_VISIONOS_DEVICE_OBSERVATION` messages; those are liveness signals,
   not successful rendering or performance measurements.
4. Verify each eye sees the box at the same perceived position and depth, then
   move the head and confirm tracking. Capture Xcode/Metal validation errors and
   frame timings. Check background/foreground, recenter, shutdown and reopening.
5. Test mixed and progressive immersion separately, including clear alpha and
   occlusion. Test hands/controllers separately before claiming input support.
6. Report engine commit, Xcode/visionOS/device versions, mode, settings, errors,
   screenshots/capture and a Metal System Trace. Keep `device_validated=false`
   until real evidence justifies changing the qualification status.

Foveation, MSAA, temporal effects, long-session resource behavior and repeated
quality/resize transitions need further implementation or device qualification.
