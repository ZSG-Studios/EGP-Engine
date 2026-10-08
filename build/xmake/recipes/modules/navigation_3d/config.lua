-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy(R.index(env, "disable_navigation_3d")) then
        return false
    end
    env:module_add_dependencies("navigation", {"csg", "gridmap"}, true)
    return true
end
function configure(env)
end
