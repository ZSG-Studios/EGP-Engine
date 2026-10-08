-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_fbx, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_fbx = env_modules:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/ufbx/"
    thirdparty_sources = {R.add(thirdparty_dir, "ufbx.c")}
    env_fbx:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_thirdparty = env_fbx:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:add({["CPPDEFINES"] = {"UFBX_NO_SUBDIVISION", "UFBX_NO_TESSELLATION", "UFBX_NO_GEOMETRY_CACHE", "UFBX_NO_SCENE_EVALUATION", "UFBX_NO_INDEX_GENERATION", "UFBX_NO_SKINNING_EVALUATION", "UFBX_NO_FORMAT_OBJ"}})
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_fbx:sources(module_obj, "*.cpp")
    env_fbx:sources(module_obj, "structures/*.cpp")
    graph:include("extensions/recipe.lua")
    if R.truthy(env.editor_build) then
        env_fbx:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
