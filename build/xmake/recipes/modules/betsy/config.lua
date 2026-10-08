-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = (function() local v = env.editor_build; if R.truthy(v) then return v end; return R.index(env, "betsy_export_templates") end)(); if not R.truthy(v) then return v end; return R.index(env, "rendering_device") end)()
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("betsy_export_templates", "Enable Betsy image compression in export template builds (increases binary size)", false)}
end
function configure(env)
end
