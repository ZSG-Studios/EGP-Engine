-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_modules, env_visual_shader
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_modules:add({["CPPDEFINES"] = {"GODOT_MODULE"}})
    env_visual_shader = env_modules:clone()
    env_visual_shader:sources(env.modules_sources, "*.cpp")
    env_visual_shader:sources(env.modules_sources, "vs_nodes/*.cpp")
    if R.truthy(env.editor_build) then
        env_visual_shader:sources(env.modules_sources, "editor/*.cpp")
    end
end
