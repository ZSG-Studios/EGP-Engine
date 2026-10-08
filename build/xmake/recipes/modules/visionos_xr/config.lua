-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (function() local v = ((platform == "visionos")); if not R.truthy(v) then return v end; return not R.truthy(R.index(env, "disable_xr")) end)()
end
function configure(env)
end
function get_doc_classes()
    return {"VisionOSXRInterface"}
end
function get_doc_path()
    return "doc_classes"
end
