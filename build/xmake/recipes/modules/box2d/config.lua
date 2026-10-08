-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = not R.truthy(R.index(env, "disable_physics_2d")); if not R.truthy(v) then return v end; local v = ((R.index(env, "precision") == "single")); if not R.truthy(v) then return v end; local v = (R.contains({"x86_64", "arm64"}, R.index(env, "arch"))); if not R.truthy(v) then return v end; return (R.contains({"windows", "linuxbsd", "macos"}, platform)) end)()
end
function configure(env)
end
function get_doc_classes()
    return {"Box2DDirectSpaceState2D", "Box2DPhysicsServer2D"}
end
function get_doc_path()
    return "doc_classes"
end
