-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.scene_sources, "skeleton_modification_2d.cpp")
    env:sources(env.scene_sources, "skeleton_modification_2d_ccdik.cpp")
    env:sources(env.scene_sources, "skeleton_modification_2d_fabrik.cpp")
    env:sources(env.scene_sources, "skeleton_modification_2d_lookat.cpp")
    env:sources(env.scene_sources, "skeleton_modification_2d_stackholder.cpp")
    env:sources(env.scene_sources, "skeleton_modification_2d_twoboneik.cpp")
    env:sources(env.scene_sources, "skeleton_modification_stack_2d.cpp")
    if R.truthy(not R.truthy(R.index(env, "disable_physics_2d"))) then
        env:sources(env.scene_sources, "skeleton_modification_2d_jiggle.cpp")
        env:sources(env.scene_sources, "skeleton_modification_2d_physicalbones.cpp")
    end
end
