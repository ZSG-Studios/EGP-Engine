-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return (R.contains({"windows", "linuxbsd", "macos"}, platform))
end
function configure(env)
end
function get_doc_classes()
    return {"EGPNetSession", "EGPNetSnapshotInterpolator", "Superposition", "SuperpositionConfig", "SuperpositionProperty", "SuperpositionWorld", "SuperpositionPrediction", "SuperpositionScene", "SuperpositionSpawner", "SuperpositionRPCMethod", "SuperpositionRPC"}
end
function get_doc_path()
    return "doc_classes"
end
function is_enabled()
    return true
end
