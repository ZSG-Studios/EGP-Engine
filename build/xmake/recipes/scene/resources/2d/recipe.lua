-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.scene_sources, "tile_set.cpp")
    if R.truthy(not R.truthy(R.index(env, "disable_physics_2d"))) then
        env:sources(env.scene_sources, "capsule_shape_2d.cpp")
        env:sources(env.scene_sources, "circle_shape_2d.cpp")
        env:sources(env.scene_sources, "concave_polygon_shape_2d.cpp")
        env:sources(env.scene_sources, "convex_polygon_shape_2d.cpp")
        env:sources(env.scene_sources, "rectangle_shape_2d.cpp")
        env:sources(env.scene_sources, "segment_shape_2d.cpp")
        env:sources(env.scene_sources, "separation_ray_shape_2d.cpp")
        env:sources(env.scene_sources, "shape_2d.cpp")
        env:sources(env.scene_sources, "world_boundary_shape_2d.cpp")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_navigation_2d"))) then
        env:sources(env.scene_sources, "navigation_mesh_source_geometry_data_2d.cpp")
        env:sources(env.scene_sources, "navigation_polygon.cpp")
        env:sources(env.scene_sources, "polygon_path_finder.cpp")
    end
    graph:include("skeleton/recipe.lua")
end
