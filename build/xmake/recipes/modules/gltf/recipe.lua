-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_gltf, env_modules
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_gltf = env_modules:clone()
    env_gltf:sources(env.modules_sources, "*.cpp")
    env_gltf:sources(env.modules_sources, "structures/*.cpp")
    graph:include("extensions/recipe.lua")
    if R.truthy(env.editor_build) then
        env_gltf:sources(env.modules_sources, "editor/*.cpp")
    end
end
