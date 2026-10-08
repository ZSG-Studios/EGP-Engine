-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = R.index(env, "vulkan"); if R.truthy(v) then return v end; local v = R.index(env, "d3d12"); if R.truthy(v) then return v end; return R.index(env, "metal") end)()
end
function configure(env)
end
