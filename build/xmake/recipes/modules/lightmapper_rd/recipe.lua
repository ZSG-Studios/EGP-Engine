-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_lightmapper_rd, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_lightmapper_rd = env_modules:clone()
    env_lightmapper_rd:GLSL_HEADER("lm_raster.glsl")
    env_lightmapper_rd:GLSL_HEADER("lm_compute.glsl")
    env_lightmapper_rd:GLSL_HEADER("lm_blendseams.glsl")
    env_lightmapper_rd:depends(graph:files("*.glsl.gen.h"), {"lm_common_inc.glsl", "#build/xmake/generators/shaders.lua"})
    env_lightmapper_rd:sources(env.modules_sources, "*.cpp")
end
