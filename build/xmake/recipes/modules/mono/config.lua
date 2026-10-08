-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return table.contains(env:get("supported", {}), "mono")
end
function configure(env)
    local supported, sys
    supported = env:get("supported", {})
    if R.truthy((not R.contains(supported, "mono"))) then
        sys = R.sys
        print("The 'mono' module does not currently support building for this platform. Aborting.")
        sys.exit(255)
    end
    env:add_module_version_string("mono")
end
function get_doc_classes()
    return {"CSharpScript", "GodotSharp"}
end
function get_doc_path()
    return "doc_classes"
end
function is_enabled()
    return false
end
