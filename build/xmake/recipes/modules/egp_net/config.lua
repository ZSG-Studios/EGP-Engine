-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    -- Preserved old-network-only migration profile (Superpos plan step 8). It is
    -- never part of the default graph: select it explicitly with
    -- module_egp_net_enabled=y module_superpos_enabled=n.
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
    -- Opt-in only until every Superpos cutover gate passes.
    return false
end
