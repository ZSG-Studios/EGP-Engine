-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    env:module_add_dependencies("svg", {"jpg", "webp"}, true)
    return true
end
function configure(env)
end
