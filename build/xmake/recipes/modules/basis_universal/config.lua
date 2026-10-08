-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy(env.editor_build) then
        env:module_add_dependencies("basis_universal", {"tinyexr"})
    end
    return true
end
function configure(env)
end
