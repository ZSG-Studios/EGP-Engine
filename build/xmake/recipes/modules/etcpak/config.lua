-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return true
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("etcpak_export_templates", "Enable S3TC and ETC image compression in export template builds (increases binary size)", false)}
end
function configure(env)
end
