-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy((function() local v = ((platform == "web")); if not R.truthy(v) then return v end; return R.index(env, "proxy_to_pthread") end)()) then
        return false
    end
    return (function() local v = R.index(env, "opengl3"); if not R.truthy(v) then return v end; return not R.truthy(R.index(env, "disable_xr")) end)()
end
function configure(env)
end
function get_doc_classes()
    return {"WebXRInterface"}
end
function get_doc_path()
    return "doc_classes"
end
