set_xmakever("3.1.1")
includes("../../swift_rules.lua")
target("swift_metadata")
 set_kind("static")
 add_files("../../../../platform/ios/*.swift", "../../../../drivers/apple_embedded/godot_swiftui_view_controller.swift")
 on_load(function(target)
  import("build.xmake.platforms.swift", {rootdir=path.absolute("../../../..",os.scriptdir())}).configure(target, {module="godot_swift_module",header="godot_swift_module-Swift.gen.h"}, {platform="ios"})
 end)
 on_config(function(target)
  local batch=assert(target:sourcebatches()["egp.apple.swift"])
  assert(#batch.sourcefiles==2)
  assert(#target:objectfiles()==1)
  for name, other in pairs(target:sourcebatches()) do
   if name~="egp.apple.swift" and other.sourcekind=="sc" then
    assert(false, "Stock per-file Swift batches must not run without the engine bridging header")
   end
  end
  print("NATIVE_SWIFT_OBJECT_GRAPH_CHECKS=3")

  print("NATIVE_SWIFT_OBJECT_GRAPH_PASS",#batch.sourcefiles,#target:objectfiles())
 end)
target_end()
