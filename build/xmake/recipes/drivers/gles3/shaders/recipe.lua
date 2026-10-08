-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, gl_include_files, glsl_files
    env = graph:use("env")
    if R.truthy((R.contains(R.index(env, "BUILDERS"), "GLES3_GLSL"))) then
        gl_include_files = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(graph:files("*_inc.glsl"))) do; local f = __item2; table.insert(__item1, R.str(f)); end; return __item1 end)()
        glsl_files = (function() local __item3 = {}; for _, __item4 in ipairs(R.iter(graph:files("*.glsl"))) do; local f = __item4; if R.truthy((not R.contains(gl_include_files, R.str(f)))) then; table.insert(__item3, R.str(f)); end; end; return __item3 end)()
        env:depends((function() local __item5 = {}; for _, __item6 in ipairs(R.iter(glsl_files)) do; local f = __item6; table.insert(__item5, R.add(f, ".gen.h")); end; return __item5 end)(), R.add(gl_include_files, {"#build/xmake/generators/shaders.lua"}))
        env:GLES3_GLSL("canvas.glsl")
        env:GLES3_GLSL("feed.glsl")
        env:GLES3_GLSL("scene.glsl")
        env:GLES3_GLSL("sky.glsl")
        env:GLES3_GLSL("canvas_occlusion.glsl")
        env:GLES3_GLSL("canvas_sdf.glsl")
        env:GLES3_GLSL("particles.glsl")
        env:GLES3_GLSL("particles_copy.glsl")
        env:GLES3_GLSL("skeleton.glsl")
        env:GLES3_GLSL("tex_blit.glsl")
    end
    graph:include("effects/recipe.lua")
end
