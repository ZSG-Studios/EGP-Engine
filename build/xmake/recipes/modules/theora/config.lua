-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy(R.startswith(R.index(env, "arch"), "rv")) then
        return false
    end
    env:module_add_dependencies("theora", {"ogg", "vorbis"})
    return true
end
function configure(env)
end
function get_doc_classes()
    return {"VideoStreamTheora"}
end
function get_doc_path()
    return "doc_classes"
end
