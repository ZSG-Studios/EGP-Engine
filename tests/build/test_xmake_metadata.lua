-- Source and artifact graphs only: this test never configures an engine compiler.
function main()
    local platform_policy=import('build.xmake.platforms.init',{rootdir=os.curdir()})
    platform_policy.validate_editor_host({platform='macos',arch='x86_64',target='editor'},'macosx','arm64')
    platform_policy.validate_editor_host({platform='windows',arch='x86_32',target='editor'},'windows','x64')
    for _, profile in ipairs({{platform='web',arch='wasm32',target='editor'},{platform='linuxbsd',arch='arm64',target='editor'}}) do
        local rejected=false
        try {function() platform_policy.validate_editor_host(profile,'linux','x86_64') end,
            catch {function(errors) rejected=tostring(errors):find('Editor SDK bootstrap must run',1,true)~=nil end}}
        assert(rejected,'Foreign editor bootstrap must fail clearly')
    end
    local root=os.curdir()
    local factory=import('build.xmake.graph',{rootdir=root})
    local checks=0
    for _, platform in ipairs({'windows','linuxbsd','ios','visionos'}) do
        local arch=(platform=='ios' or platform=='visionos') and 'arm64' or 'x86_64'
        local graph=factory.new(root,{platform=platform,target='template_debug',arch=arch,vulkan=false,d3d12=false,accesskit=false})
        graph:configure()
        local data=graph:serialize()
        local declared,linked={},{}
        for _, lib in ipairs(data.libraries) do if not lib.external then declared[lib.name]=true end end
        for _, ref in ipairs(data.programs[1].policy.LIBS) do if ref.kind=='target' then assert(declared[ref.name],'Undeclared primary archive'); linked[ref.name]=true end end
        for name in pairs(declared) do assert(linked[name],'Declared engine archive omitted from primary: ' .. name) end
        checks=checks+2
        if platform=='windows' then
            local sdl
            for _, lib in ipairs(data.libraries) do for _, source in ipairs(lib.sources) do if source.path=='thirdparty/sdl/SDL.c' then sdl=source end end end
            assert(sdl and table.contains(sdl.policy.CPPPATH,path.join(root,'drivers/sdl')),'SDL vendor sources must inherit the recipe-local private configuration directory')
            assert(os.isfile(path.join(root,'drivers/sdl/SDL_build_config_private.h')),'SDL private configuration header must exist at the resolved include directory')
            checks=checks+2
        end
        if platform=='ios' or platform=='visionos' then
            local camera
            for _, lib in ipairs(data.libraries) do if lib.name=='external_camera' then camera=lib end end
            assert(camera and camera.filename=='libgodot_camera.' .. platform .. '.template_debug.arm64.a','External camera artifact filename')
            assert(not linked.external_camera and data.programs[1].policy.LIBS_EXTERNAL[1].name=='external_camera','External camera must stay separate')
            checks=checks+2
        end
    end
    assert(not utils.trycall(function() factory.new(root,{platform='web'}):configure() end),'Removed WebGL graph must fail')
    checks=checks+1
    for _, platform in ipairs({'windows','linuxbsd','macos'}) do
        for _, kind in ipairs({'static_library','shared_library'}) do
            local graph=factory.new(root,{platform=platform,target='template_debug',arch='x86_64',library_type=kind,vulkan=false,d3d12=false,accesskit=false,metal=false})
            graph:configure()
            local data=graph:serialize()
            local program=data.programs[1]
            local extension=kind=='static_library' and (platform=='windows' and '.lib' or '.a') or (platform=='windows' and '.dll' or platform=='macos' and '.dylib' or '.so')
            local prefix=platform=='windows' and 'godot' or 'libgodot'
            assert(#data.programs==1 and program.kind==(kind=='static_library' and 'static' or 'shared'),'Template library must be the sole primary artifact')
            assert(program.filename==prefix .. '.' .. platform .. '.template_debug.x86_64' .. extension,'Template library filename must match its actual artifact kind')
            checks=checks+2
        end
    end
    for _, kind in ipairs({'static_library','shared_library'}) do
        local ok,diagnostic=utils.trycall(function() factory.new(root,{platform='windows',target='editor',library_type=kind}):configure() end)
        assert(not ok and tostring(diagnostic):find('Editor SDK generation requires an executable editor',1,true),'Unqualified editor library variant must fail with an actionable SDK diagnostic')
        checks=checks+1
    end
    print('XMAKE_METADATA_GRAPHS_PASS ' .. (checks+4))
end
