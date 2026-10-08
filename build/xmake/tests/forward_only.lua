function main()
    local root=os.projectdir()
    local model=import('build.xmake.graph',{rootdir=root})
    local common=import('build.xmake.common',{rootdir=root})
    local checks=0
    local function check(value) assert(value);checks=checks+1 end
    for _, platform in ipairs({'windows','linuxbsd','macos','android','ios','visionos'}) do
        local graph=model.new(root,{platform=platform})
        graph:configure()
        local o=graph.options
        check(o.rendering_device and o.forward_plus_renderer and not o.forward_mobile_renderer and not o.opengl3 and not o.angle)
        local compiled=graph:serialize()
        for _, lib in ipairs(compiled.libraries) do
            for _, source in ipairs(lib.sources) do
                check(not source.path:find('gles3/',1,true) and not source.path:find('forward_mobile/',1,true))
                assert(os.isfile(path.join(root,source.path)) or source.path:find(".gen.",1,true), "Missing source: "..source.path)
            end
        end
    end
    for _, options in ipairs({{platform='web'},{platform='windows',opengl3=true},{platform='windows',angle=true},{platform='windows',forward_mobile_renderer=true},{platform='windows',forward_plus_renderer=false},{platform='windows',rendering_device=false}}) do
        local ok=utils.trycall(function()
            local graph=model.new(root,options)
            common.configure(graph.environment,graph.options,graph.explicit)
        end)
        check(not ok)
    end
    print('FORWARD_ONLY_POLICY_PASS '..checks)
end
