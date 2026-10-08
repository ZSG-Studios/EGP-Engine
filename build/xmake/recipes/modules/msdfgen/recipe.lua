-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_msdfgen, idx, inserted, lib, linklib, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_msdfgen = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_msdfgen")) then
        env_msdfgen:disable_warnings()
        thirdparty_dir = "#thirdparty/msdfgen/"
        thirdparty_sources = {"core/Contour.cpp", "core/DistanceMapping.cpp", "core/EdgeHolder.cpp", "core/MSDFErrorCorrection.cpp", "core/Projection.cpp", "core/Scanline.cpp", "core/Shape.cpp", "core/contour-combiners.cpp", "core/convergent-curve-ordering.cpp", "core/edge-coloring.cpp", "core/edge-segments.cpp", "core/edge-selectors.cpp", "core/equation-solver.cpp", "core/msdf-error-correction.cpp", "core/msdfgen.cpp", "core/rasterization.cpp", "core/render-sdf.cpp", "core/sdf-error-estimation.cpp", "core/shape-description.cpp"}
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_msdfgen:add({["CPPDEFINES"] = {{"MSDFGEN_PUBLIC", ""}}})
        env_msdfgen:prepend({["CPPPATH"] = {"#thirdparty/freetype/include", "#thirdparty/msdfgen", "#thirdparty/nanosvg"}})
        lib = env_msdfgen:library("msdfgen_builtin", thirdparty_sources)
        thirdparty_obj = R.iadd(thirdparty_obj, lib)
        inserted = false
        for _, __item3 in ipairs(R.iter(R.enumerate(R.index(env, "LIBS")))) do
            local __item4 = __item3; idx = R.index(__item4, 0); linklib = R.index(__item4, 1)
            if R.truthy(R.isinstance(linklib, {"str", "bytes"})) then
                R.insert(R.index(env, "LIBS"), idx, lib)
                inserted = true
                break
            end
        end
        if R.truthy(not R.truthy(inserted)) then
            env:add({["LIBS"] = {lib}})
        end
    end
    module_obj = {}
    env_msdfgen:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
