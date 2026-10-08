-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = env.editor_build; if not R.truthy(v) then return v end; return R.index(env, "rendering_device") end)()
end
function configure(env)
end
