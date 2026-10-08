set_xmakever("3.1.1")
includes("../../swift_rules.lua")
target("swift_metadata")
 set_kind("static")
 add_rules("egp.apple.swift")
 add_files("../../../../platform/ios/*.swift", "../../../../drivers/apple_embedded/godot_swiftui_view_controller.swift")
 on_load(function(target)
  target:data_set("egp.apple.swift.settings", {module="godot_swift_module",header="godot_swift_module-Swift.gen.h"})
 end)
 on_config(function(target)
  local batch=assert(target:sourcebatches()["egp.apple.swift"])
  assert(#batch.sourcefiles==2)
  assert(#target:objectfiles()==1)
  for name, other in pairs(target:sourcebatches()) do
   if name~="egp.apple.swift" and other.sourcekind=="sc" then
    assert(#(other.objectfiles or {})==0)
   end
  end
  print("NATIVE_SWIFT_OBJECT_GRAPH_CHECKS=3")

  print("NATIVE_SWIFT_OBJECT_GRAPH_PASS",#batch.sourcefiles,#target:objectfiles())
 end)
target_end()
