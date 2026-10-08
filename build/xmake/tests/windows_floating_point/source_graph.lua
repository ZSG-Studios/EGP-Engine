-- Capture production recipe policies from the engine project, independently of the probe project.
function main(root,compiler,output)
    local graph=import("build.xmake.graph",{rootdir=root}).new(root,{
        platform="windows",arch="x86_64",target="template_debug",use_llvm=compiler=="clang-cl",
        debug_symbols=false,werror=true,accesskit=false,angle=false,d3d12=false})
    graph:configure()
    local selected={
        ["core/math/vector3.cpp"]=true,
        ["modules/egp_net/egp_net_session.cpp"]=true,
        ["modules/box2d/box2d_physics_server_2d.cpp"]=true,
        ["modules/box3d/scene_backend/servers/box3d_physics_server_3d.cpp"]=true
    }
    local policies={}
    for _,library in ipairs(graph:serialize().libraries) do
        for _,source in ipairs(library.sources) do if selected[source.path] then policies[source.path]=source.policy end end
    end
    import("core.base.json").savefile(output,{options=graph.options,policies=policies})
end
