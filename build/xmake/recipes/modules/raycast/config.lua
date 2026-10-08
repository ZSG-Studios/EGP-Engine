-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    if R.truthy((function() local v = ((R.index(env, "arch") == "arm64")); if not R.truthy(v) then return v end; local v = ((platform == "windows")); if not R.truthy(v) then return v end; return env.msvc end)()) then
        return false
    end
    if R.truthy((R.contains({"x86_64", "arm64", "wasm32"}, R.index(env, "arch")))) then
        return true
    end
    if R.truthy((function() local v = ((R.index(env, "arch") == "x86_32")); if not R.truthy(v) then return v end; return ((platform == "windows")) end)()) then
        return true
    end
    return false
end
function configure(env)
end
