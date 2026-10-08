-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = env.editor_build; if R.truthy(v) then return v end; return R.index(env, "cvtt_export_templates") end)()
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("cvtt_export_templates", "Enable CVTT image compression in export template builds (increases binary size)", false)}
end
function configure(env)
end
