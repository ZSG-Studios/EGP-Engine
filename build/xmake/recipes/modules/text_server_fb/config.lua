-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    env:module_add_dependencies("text_server_fb", {"freetype", "msdfgen", "svg"}, true)
    return true
end
function configure(env)
end
function is_enabled()
    return false
end
function get_doc_classes()
    return {"TextServerFallback"}
end
function get_doc_path()
    return "doc_classes"
end
