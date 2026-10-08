-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, gl_include_files, glsl_file, glsl_files
    env = graph:use("env")
    if R.truthy((R.contains(R.index(env, "BUILDERS"), "RD_GLSL"))) then
        gl_include_files = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(graph:files("*_inc.glsl"))) do; local f = __item2; table.insert(__item1, R.str(f)); end; return __item1 end)()
        glsl_files = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(graph:files("*.glsl"))) do; local f = __item4; if R.truthy((not R.contains(gl_include_files, R.str(f)))) then; table.insert(__item3, R.str(f)); end; end; return __item3 end)()
        env:depends((function() local __item5 = {}; for _, __item6 in ipairs(R.iter(glsl_files)) do; local f = __item6; table.insert(__item5, R.add(f, ".gen.h")); end; return __item5 end)(), R.add(gl_include_files, {"#build/xmake/generators/shaders.lua"}))
        for _, __item7 in ipairs(R.iter(gl_include_files)) do
            glsl_file = __item7
            env:GLSL_HEADER(glsl_file)
        end
        for _, __item8 in ipairs(R.iter(glsl_files)) do
            glsl_file = __item8
            env:RD_GLSL(glsl_file)
        end
    end
    graph:include("effects/recipe.lua")
    graph:include("environment/recipe.lua")
    graph:include("forward_clustered/recipe.lua")
    graph:include("forward_mobile/recipe.lua")
end
