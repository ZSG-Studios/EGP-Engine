-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    env:sources(env.scene_sources, "fog_material.cpp")
    env:sources(env.scene_sources, "importer_mesh.cpp")
    env:sources(env.scene_sources, "joint_limitation_3d.cpp")
    env:sources(env.scene_sources, "joint_limitation_cone_3d.cpp")
    env:sources(env.scene_sources, "mesh_library.cpp")
    env:sources(env.scene_sources, "primitive_meshes.cpp")
    env:sources(env.scene_sources, "skin.cpp")
    env:sources(env.scene_sources, "sky_material.cpp")
    env:sources(env.scene_sources, "world_3d.cpp")
    env:sources(env.scene_sources, "skeleton/*.cpp")
    if R.truthy(not R.truthy(R.index(env, "disable_physics_3d"))) then
        env:sources(env.scene_sources, "box_shape_3d.cpp")
        env:sources(env.scene_sources, "capsule_shape_3d.cpp")
        env:sources(env.scene_sources, "circle_shape_3d.cpp")
        env:sources(env.scene_sources, "concave_polygon_shape_3d.cpp")
        env:sources(env.scene_sources, "convex_polygon_shape_3d.cpp")
        env:sources(env.scene_sources, "cylinder_shape_3d.cpp")
        env:sources(env.scene_sources, "height_map_shape_3d.cpp")
        env:sources(env.scene_sources, "separation_ray_shape_3d.cpp")
        env:sources(env.scene_sources, "shape_3d.cpp")
        env:sources(env.scene_sources, "sphere_shape_3d.cpp")
        env:sources(env.scene_sources, "world_boundary_shape_3d.cpp")
    end
    if R.truthy(not R.truthy(R.index(env, "disable_navigation_3d"))) then
        env:sources(env.scene_sources, "navigation_mesh_source_geometry_data_3d.cpp")
    end
end
