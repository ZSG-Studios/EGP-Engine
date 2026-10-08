-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return R.index(env, "minizip")
end
function configure(env)
end
function get_doc_classes()
    return {"ZIPReader", "ZIPPacker"}
end
function get_doc_path()
    return "doc_classes"
end
