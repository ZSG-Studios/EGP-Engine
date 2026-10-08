-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return not R.truthy(R.index(env, "disable_3d"))
end
function configure(env)
end
function get_doc_classes()
    return {"CSGBox3D", "CSGCombiner3D", "CSGCylinder3D", "CSGMesh3D", "CSGPolygon3D", "CSGPrimitive3D", "CSGShape3D", "CSGSphere3D", "CSGTorus3D"}
end
function get_doc_path()
    return "doc_classes"
end
