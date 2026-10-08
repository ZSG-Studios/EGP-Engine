-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return true
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("mp3_extra_formats", "Build mp3 module with MP1/MP2 decoding support", false)}
end
function configure(env)
end
function get_doc_classes()
    return {"AudioStreamMP3", "ResourceImporterMP3"}
end
function get_doc_path()
    return "doc_classes"
end
