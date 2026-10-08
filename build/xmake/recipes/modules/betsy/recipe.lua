-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_betsy, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_betsy = env_modules:clone()
    env_betsy:GLSL_HEADER("bc6h.glsl")
    env_betsy:GLSL_HEADER("bc1.glsl")
    env_betsy:GLSL_HEADER("bc4.glsl")
    env_betsy:GLSL_HEADER("alpha_stitch.glsl")
    env_betsy:GLSL_HEADER("rgb_to_rgba.glsl")
    env_betsy:depends(graph:files("*.glsl.gen.h"), {"#build/xmake/generators/shaders.lua"})
    env_betsy:sources(env.modules_sources, "*.cpp")
end
