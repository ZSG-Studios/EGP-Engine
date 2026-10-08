-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_csg, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_csg = env_modules:clone()
    env_csg:add({["CPPDEFINES"] = {{"MANIFOLD_PAR", (-1)}}})
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/manifold/"
    thirdparty_sources = {"src/boolean_result.cpp", "src/boolean3.cpp", "src/constructors.cpp", "src/csg_tree.cpp", "src/edge_op.cpp", "src/execution_impl.cpp", "src/face_op.cpp", "src/impl.cpp", "src/manifold.cpp", "src/minkowski.cpp", "src/polygon.cpp", "src/properties.cpp", "src/quickhull.cpp", "src/sdf.cpp", "src/smoothing.cpp", "src/sort.cpp", "src/subdivision.cpp", "src/tree2d.cpp"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_csg:prepend({["CPPPATH"] = {R.add(thirdparty_dir, "include")}})
    env_thirdparty = env_csg:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    module_obj = {}
    env_csg:sources(module_obj, "*.cpp")
    if R.truthy(env.editor_build) then
        env_csg:sources(module_obj, "editor/*.cpp")
    end
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
