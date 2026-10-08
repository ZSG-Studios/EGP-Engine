-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    local sys
    sys = R.sys
    if R.truthy((function() local v = R.startswith(sys.platform, "freebsd"); if R.truthy(v) then return v end; return R.startswith(sys.platform, "openbsd") end)()) then
        return false
    end
    return (function() local v = ((platform == "macos")); if R.truthy(v) then return v end; local v = ((platform == "windows")); if R.truthy(v) then return v end; local v = ((platform == "linuxbsd")); if R.truthy(v) then return v end; local v = ((platform == "android")); if R.truthy(v) then return v end; local v = ((platform == "ios")); if R.truthy(v) then return v end; return ((platform == "visionos")) end)()
end
function configure(env)
end
