-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    env:module_add_dependencies("text_server_adv", {"freetype", "msdfgen", "svg"}, true)
    return true
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("graphite", "Enable SIL Graphite smart fonts support", true)}
end
function configure(env)
end
function get_doc_classes()
    return {"TextServerAdvanced"}
end
function get_doc_path()
    return "doc_classes"
end
