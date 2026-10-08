function main()
    local linking=import('build.xmake.linking',{rootdir=os.curdir()})
    local root=os.curdir()
    local refs=linking.references({{{library='main'}},{{library='core'}},{'pthread','m'},'-lz',{path=path.join(root,'vendor/libcodec.a')}},root)
    assert(#refs==6 and refs[1].kind=='target' and refs[2].name=='core','Nested native archives must retain recipe order')
    assert(refs[3].kind=='system' and refs[5].name=='z','External named libraries normalize without shell flags')
    assert(refs[6].kind=='path' and path.is_absolute(refs[6].path),'Library file nodes must retain absolute paths')
    local target={calls={}}
    function target:add(name,...) table.insert(self.calls,{name,...}) end
    linking.apply(target,{LIBS=refs},{platform='windows'})
    assert(#target.calls==4 and target.calls[1][1]=='syslinks' and target.calls[4][2]==refs[6].path,'External library inputs must not disappear or create archive self-dependencies')
    linking.archive_group(target,{LIBS=refs},{platform='windows'})
    assert(#target.calls==4,'MSVC must not receive GNU archive groups')
    for _, platform in ipairs({'linuxbsd','android','web'}) do
        linking.archive_group(target,{LIBS=refs},{platform=platform})
        local group=target.calls[#target.calls]
        assert(group[1]=='linkgroups' and group[2]=='egp_archive_main' and group[3]=='egp_archive_core' and group[4].group,'GNU archive dependencies must be grouped in native recipe order')
    end
    linking.archive_group(target,{LIBS={}},{platform='web'})
    assert(#target.calls==7,'Dynamic Web loader with empty LIBS must not pull engine archives')
    local mingw={calls={},add=target.add}
    linking.archive_group(mingw,{LIBS=refs},{platform='windows',use_mingw='y'})
    assert(#mingw.calls==2 and mingw.calls[1][1]=='ldflags' and mingw.calls[2][1]=='shflags','Only GNU Windows binary/shared links need explicit archive rescans')
    for _,call in ipairs(mingw.calls) do
        assert(table.concat(call[2],',')=='-Wl,--start-group,-legp_archive_main,-legp_archive_core,-Wl,--end-group','Native archive group must preserve recipe identity and scope')
        assert(call[3].force and call[3].expand==false,'Native GNU group must remain an atomic argument sequence')
    end
    local empty={calls={},add=target.add}
    linking.archive_group(empty,{LIBS={}},{platform='windows',use_mingw=true})
    assert(#empty.calls==0,'An empty GNU Windows graph must not pull engine archives')
    local ordered={calls={}}
    ordered.add=target.add
    function ordered:get(key) return key=='syslinks' and {'pthread','z'} or {} end
    linking.archive_group(ordered,{LIBS=refs},{platform='linuxbsd'})
    assert(#ordered.calls==3 and ordered.calls[2][1]=='linkorders' and ordered.calls[2][2]=='linkgroup::egp_native_archives' and ordered.calls[3][3]=='z','System dependencies must follow archive consumers under --as-needed')
    local graph=import('build.xmake.graph',{rootdir=root}).new(root,{platform='windows',target='template_debug'})
    graph.environment.ENV.EMCC_CLOSURE_ARGS="--externs 'fixture with spaces.js'"
    graph.environment.ENV.EGP_PRIVATE_TEST_SENTINEL='must-not-be-serialized'
    graph.environment:add({ASFLAGS={'-DEGP_ASSEMBLER_POLICY=1'},ARFLAGS={'-D'},RCFLAGS={'/DEGP_RESOURCE_POLICY=1'}})
    graph.environment:add({LIBS={{{library='main'}},'z',{path=path.join(root,'vendor/libcodec.a')}},LIBS_EXTERNAL={{{library='camera'}}}})
    local serialized=graph:serialize()
    local json=import('core.base.json')
    local decoded=json.decode(json.encode(serialized))
    assert(decoded.global_policy.LIBS[1].kind=='target' and decoded.global_policy.LIBS[3].kind=='path','Graph JSON must retain typed references without node methods')
    assert(decoded.global_policy.LIBS_EXTERNAL[1].name=='camera','External native archive references must survive graph serialization')
    assert(decoded.global_policy.BUILD_ENV.EMCC_CLOSURE_ARGS=="--externs 'fixture with spaces.js'" and decoded.global_policy.BUILD_ENV.EGP_PRIVATE_TEST_SENTINEL==nil and decoded.global_policy.ENV==nil,'Only explicit Closure environment input may cross the build graph')
    assert(decoded.global_policy.ASFLAGS[1]=='-DEGP_ASSEMBLER_POLICY=1','Assembly option must survive graph serialization')
    assert(decoded.global_policy.ARFLAGS[1]=='-D','Archive option must survive graph serialization')
    assert(decoded.global_policy.RCFLAGS[1]=='/DEGP_RESOURCE_POLICY=1','Resource option must survive graph serialization')
    graph.environment.PROGSUFFIX='.ios.template_debug.arm64.a'
    local camera=graph.environment:library('#bin/libgodot_camera',{})
    graph.environment:add({LIBS_EXTERNAL={camera}})
    local external=graph:serialize().libraries[1]
    assert(external.name=='external_camera' and external.external and external.filename=='libgodot_camera.ios.template_debug.arm64.a','Apple external archive must use canonical target name and retain package filename')
    assert(graph:serialize().global_policy.LIBS_EXTERNAL[2].name=='external_camera','External archive dependency must reference canonical target')
    local deps=import('build.xmake.platforms.system_dependencies',{rootdir=root})
    local queried={}
    deps.configure(target,{platform='linuxbsd',target='template_debug',builtin_freetype=false,builtin_zlib='n'},function(name)
        table.insert(queried,name); return {includedirs={'/native/include/' .. name},links={name}}
    end)
    assert(#queried==2 and queried[1]=='freetype2' and queried[2]=='zlib','System vendor choices must resolve native package headers and links')
    deps.configure(target,{platform='windows',builtin_zlib=false},function() error('Cross-platform package lookup') end)
    assert(#queried==2,'Linux package resolution must not run for MSVC')
    graph.current=path.join(root,'drivers/sdl')
    graph.environment:add({CPPPATH={'.','../headers','#thirdparty/sdl',{path=path.join(root,'native/include')}},LIBPATH={'.','../libraries'}})
    local paths=graph.environment.CPPPATH
    assert(paths[1]==path.join(root,'drivers/sdl') and paths[2]==path.join(root,'drivers/headers'),'Relative include paths must bind to their declaring recipe directory')
    assert(paths[3]=='#thirdparty/sdl' and paths[4]==path.join(root,'native/include'),'Root markers and native directory nodes must retain meaning')
    assert(graph.environment.LIBPATH[1]==path.join(root,'drivers/sdl') and graph.environment.LIBPATH[2]==path.join(root,'drivers/libraries'),'Relative library paths must bind to their declaring recipe directory')
    local platform_policy=import('build.xmake.platforms.init',{rootdir=root})
    local common=import('build.xmake.common',{rootdir=root})
    local fixture=os.tmpfile() .. '.moltenvk'
    local checks=0
    local function check(value,message) assert(value,message); checks=checks+1 end
    local function configure(options)
        options.target,options.accesskit,options.angle,options.d3d12='template_release',false,false,false
        local model=import('build.xmake.graph',{rootdir=root}).new(root,options)
        common.configure(model.environment,model.options,model.explicit)
        local native={values={}}
        function native:set(key,...) self.values[key]={...} end
        function native:add(key,...)
            self.values[key]=self.values[key] or {}
            for _, value in ipairs({...}) do if type(value)~='table' then table.insert(self.values[key],value) end end
        end
        platform_policy.configure(native,model.options)
        return native.values
    end
    for index,layout in ipairs({'MoltenVK/MoltenVK.xcframework','macOS/lib/MoltenVK.xcframework',''}) do
        local sdk=path.join(fixture,tostring(index))
        local universal=path.join(sdk,layout,'macos-arm64_x86_64')
        os.mkdir(universal); io.writefile(path.join(universal,'libMoltenVK.a'),'fixture')
        for _, arch in ipairs({'x86_64','arm64'}) do
            local effective=configure({platform='macos',arch=arch,vulkan=true,use_volk=false,metal=false,vulkan_sdk_path=sdk})
            check(table.contains(effective.syslinks or {},'MoltenVK'),'macOS Vulkan must link its native MoltenVK archive')
            check(table.contains(effective.linkdirs or {},universal),'The explicit SDK layout must select its universal MoltenVK archive')
            check(table.contains(effective.frameworks or {},'Metal') and not table.contains(effective.frameworks or {},'MetalKit'),'Vulkan must link Metal even when the Metal rendering driver is disabled')
        end
    end
    local sdk=path.join(fixture,'architecture-specific')
    local specific=path.join(sdk,'macos-arm64')
    os.mkdir(specific); io.writefile(path.join(specific,'libMoltenVK.a'),'fixture')
    local effective=configure({platform='macos',arch='arm64',vulkan=true,use_volk=false,metal=false,vulkan_sdk_path=sdk})
    check(table.contains(effective.linkdirs or {},specific),'Architecture-specific SDK slices must work without a universal slice')
    effective=configure({platform='macos',arch='x86_64',vulkan=true,use_volk=true,metal=false,vulkan_sdk_path=path.join(fixture,'missing')})
    check(table.contains(effective.frameworks or {},'Metal') and not table.contains(effective.syslinks or {},'MoltenVK'),'Volk must retain Metal without requiring a static MoltenVK installation')
    effective=configure({platform='macos',arch='arm64',vulkan=false,metal=false,vulkan_sdk_path=path.join(fixture,'missing')})
    check(not table.contains(effective.frameworks or {},'Metal') and not table.contains(effective.syslinks or {},'MoltenVK'),'Disabling Vulkan and Metal must avoid their SDK dependencies')
    effective=configure({platform='windows',arch='x86_64',vulkan=true,use_volk=false})
    check(table.contains(effective.syslinks or {},'vulkan-1') and not table.contains(effective.syslinks or {},'MoltenVK'),'Windows Vulkan loader selection must remain unchanged')
    for _, sdk in ipairs({{'1.3.230.0','macOS/lib/MoltenVK.xcframework'},{'1.3.250.0','MoltenVK/MoltenVK.xcframework'},{'1.3.290.0','macOS/lib/MoltenVK.xcframework'}}) do
        local slice=path.join(fixture,'VulkanSDK',sdk[1],sdk[2],'macos-arm64_x86_64')
        os.mkdir(slice); io.writefile(path.join(slice,'libMoltenVK.a'),'fixture')
    end
    os.mkdir(path.join(fixture,'VulkanSDK/1.4.0.0/macOS/lib/MoltenVK.xcframework'))
    local latest=path.join(fixture,'VulkanSDK/1.3.290.0/macOS/lib/MoltenVK.xcframework/macos-arm64_x86_64')
    for _, arch in ipairs({'x86_64','arm64'}) do
        local autodetected={calls={},add=target.add}
        platform_policy.configure_macos_vulkan(autodetected,{arch=arch,use_volk=false},fixture)
        check(autodetected.calls[2][1]=='linkdirs' and autodetected.calls[2][2]==latest,'Automatic discovery must choose the highest supported SDK containing its matching native archive')
        check(autodetected.calls[1][2]=='Metal' and autodetected.calls[3][2]=='MoltenVK','Automatic SDK discovery must produce effective Metal and MoltenVK link dependencies')
    end
    os.tryrm(fixture)
    print('XMAKE_LINK_POLICY_PASS ' .. (29+checks))
end
