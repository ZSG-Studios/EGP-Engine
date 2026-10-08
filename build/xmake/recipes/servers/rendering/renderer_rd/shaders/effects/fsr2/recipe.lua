-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, fsr2_dir, gl_include_files, glsl_file, glsl_files
    env = graph:use("env")
    if R.truthy((R.contains(R.index(env, "BUILDERS"), "RD_GLSL"))) then
        gl_include_files = R.add((function() local __item1 = {}; for _, __item2 in ipairs(R.iter(graph:files("*_inc.glsl"))) do; local f = __item2; table.insert(__item1, R.str(f)); end; return __item1 end)(), (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(graph:files("../*_inc.glsl"))) do; local f = __item4; table.insert(__item3, R.str(f)); end; return __item3 end)())
        fsr2_dir = "#thirdparty/amd-fsr2/shaders"
        gl_include_files = R.iadd(gl_include_files, (function() local __item5 = {}; for _, __item6 in ipairs(R.iter(graph:files(R.add(fsr2_dir, "/*.h")))) do; local f = __item6; table.insert(__item5, R.str(f)); end; return __item5 end)())
        gl_include_files = R.iadd(gl_include_files, (function() local __item7 = {}; for _, __item8 in ipairs(R.iter(graph:files(R.add(fsr2_dir, "/*.glsl")))) do; local f = __item8; table.insert(__item7, R.str(f)); end; return __item7 end)())
        glsl_files = (function() local __item9 = {}; for _, __item10 in ipairs(R.iter(graph:files("*.glsl"))) do; local f = __item10; if R.truthy((not R.contains(gl_include_files, R.str(f)))) then; table.insert(__item9, R.str(f)); end; end; return __item9 end)()
        env:depends((function() local __item11 = {}; for _, __item12 in ipairs(R.iter(glsl_files)) do; local f = __item12; table.insert(__item11, R.add(f, ".gen.h")); end; return __item11 end)(), R.add(gl_include_files, {"#build/xmake/generators/shaders.lua"}))
        for _, __item13 in ipairs(R.iter(glsl_files)) do
            glsl_file = __item13
            env:RD_GLSL(glsl_file)
        end
    end
end
