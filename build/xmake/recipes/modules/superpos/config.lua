-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (R.contains({"windows", "linuxbsd", "macos"}, platform))
end
function get_opts(platform)
    local BoolVariable
    BoolVariable = R.BoolVariable
    return {BoolVariable("superpos_dtls", "Compile Superpos DTLS backend against EGP's crypto profile", true)}
end
function configure(env)
    env:module_add_dependencies("superpos", {"mbedtls"})
end
function get_doc_classes()
    return {"SuperposUInt64", "SuperposField", "SuperposSchema", "SuperposSession", "SuperposWorld"}
end
function get_doc_path()
    return "doc_classes"
end
function is_enabled()
    return true
end
