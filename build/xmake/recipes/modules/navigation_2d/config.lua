-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return not R.truthy(R.index(env, "disable_navigation_2d"))
end
function configure(env)
end
