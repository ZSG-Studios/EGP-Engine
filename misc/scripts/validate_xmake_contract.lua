-- Native CI receipt driver. Only the two small host tools are compiled by the workflow.
function main(compressor, zipper)
    assert(os.isfile(compressor) and os.isfile(zipper),'Native code-generation tools are required')
    local root=os.curdir()
    local directory=path.join(root,'.build/xmake-contract')
    local hostcompiler=assert(({windows='msvc',linux='gcc',macosx='xcode',bsd='clang'})[os.host()])
    os.mkdir(directory)
    local tests={
        {'common','tests/build/test_xmake_common.lua'},
        {'generators','tests/build/test_xmake_generators.lua',compressor},
        {'shaders','tests/build/validate_builders.lua'},
        {'packages','tests/build/test_xmake_packages.lua',zipper},
        {'linking','tests/build/test_xmake_linking.lua'},
        {'link_dependencies','tests/build/test_xmake_link_dependencies.lua'},
        {'api_fence','tests/build/test_xmake_api_fence.lua'},
        {'metadata','tests/build/test_xmake_metadata.lua'},
        {'workflows','tests/build/test_xmake_workflows.lua'},
        {'host_sdk','build/xmake/tests/host_sdk.lua',marker='NATIVE_HOST_SDK_CHECKS=(%d+)'},
        {'generated_headers','build/xmake/tests/generated_headers.lua',marker='NATIVE_GENERATED_HEADER_CHECKS=(%d+)'},
        {'recipe_compat','build/xmake/tests/recipe_compat.lua',marker='LUA_RECIPE_COMPAT_CHECKS=(%d+)'},
        {'recipes','build/xmake/tests/recipes.lua',marker='PLATFORM_PROFILES=(%d+)'},
        {'engine_tests','build/xmake/tests/engine_tests.lua',marker='NATIVE_ENGINE_TEST_GRAPH_CHECKS=(%d+)'},
        {'platform_defaults','build/xmake/tests/platform_defaults.lua',marker='NATIVE_PLATFORM_DEFAULT_CHECKS=(%d+)'},
        {'managed','build/xmake/tests/managed.lua',marker='NATIVE_MANAGED_CONTRACT_CHECKS=(%d+)'},
        {'generated_objects','build/xmake/tests/generated_objects.lua',marker='NATIVE_GENERATED_OBJECT_CHECKS=(%d+)'},
        {'compile_variants','build/xmake/tests/compile_variants.lua',marker='NATIVE_COMPILE_VARIANT_CHECKS=(%d+)'},
        {'custom_modules','build/xmake/tests/custom_modules.lua',marker='NATIVE_CUSTOM_MODULE_CHECKS=(%d+)'}
    }
    local version=os.iorunv(os.programfile(),{'--version'})
    local receipt={xmake=assert(version:match('v(%d+%.%d+%.%d+)')),host=os.host(),architecture=os.arch(),tests={}}
    for _, test in ipairs(tests) do
        local args={'lua',path.join(root,test[2])}
        for index=3,#test do table.insert(args,test[index]) end
        local testenvs
        if test[1]=='host_sdk' then
            testenvs={XMAKE_CONFIGDIR=path.join(directory,'host-sdk-config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-host-sdk')}
        end
        local stdout,stderr=os.iorunv(os.programfile(),args,{curdir=root,envs=testenvs})
        io.writefile(path.join(directory,test[1] .. '.log'),stdout .. (stderr or ''))
        local count=tonumber(stdout:match(test.marker or 'XMAKE_[A-Z_]+_PASS (%d+)'))
        assert(count and count>0,'Native contract fixture did not produce its success receipt: ' .. test[1])
        table.insert(receipt.tests,{name=test[1],checks=count,status='PASS'})
        print(test[1] .. ': PASS ' .. count)
    end
    local project=path.join(root,'build/xmake/tests/swift_object_graph')
    local output=path.join(directory,'swift-object-graph')
    local stdout,stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',project,'-o',output},{curdir=project,envs={XMAKE_CONFIGDIR=path.join(output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-swift')}})
    io.writefile(path.join(directory,'swift_object_graph.log'),stdout .. (stderr or ''))
    local sources,objects=stdout:match('NATIVE_SWIFT_OBJECT_GRAPH_PASS%s+(%d+)%s+(%d+)')
    assert(tonumber(sources) and tonumber(sources)>1 and tonumber(objects)==1,'Swift cross-file declarations must produce exactly one WMO object')
    local swiftchecks=assert(tonumber(stdout:match('NATIVE_SWIFT_OBJECT_GRAPH_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='swift_object_graph',checks=swiftchecks,status='PASS',sources=tonumber(sources),objects=tonumber(objects)})
    print('swift_object_graph: PASS ' .. swiftchecks)
    local toolchain_project=path.join(root,'build/xmake/tests/toolchain_selection')
    local toolchain_output=path.join(directory,'toolchain-selection')
    local toolchain_stdout,toolchain_stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',toolchain_project,'-o',toolchain_output},{curdir=toolchain_project,envs={XMAKE_CONFIGDIR=path.join(toolchain_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-toolchain')}})
    io.writefile(path.join(directory,'toolchain_selection.log'),toolchain_stdout .. (toolchain_stderr or ''))
    local toolchainchecks=assert(tonumber(toolchain_stdout:match('NATIVE_TOOLCHAIN_SELECTION_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='toolchain_selection',checks=toolchainchecks,status='PASS'})
    print('toolchain_selection: PASS ' .. toolchainchecks)
    local visionos_project=path.join(root,'build/xmake/tests/visionos_toolchain')
    local visionos_output=path.join(directory,'visionos-toolchain')
    local visionos_stdout,visionos_stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',visionos_project,'-o',visionos_output},{curdir=visionos_project,envs={XMAKE_CONFIGDIR=path.join(visionos_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-visionos')}})
    io.writefile(path.join(directory,'visionos_toolchain.log'),visionos_stdout .. (visionos_stderr or ''))
    local visionoschecks=assert(tonumber(visionos_stdout:match('NATIVE_VISIONOS_TOOLCHAIN_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='visionos_toolchain',checks=visionoschecks,status='PASS'})
    print('visionos_toolchain: PASS ' .. visionoschecks)
    local link_project=path.join(root,'build/xmake/tests/link_environment')
    local link_output=path.join(directory,'link-environment')
    local link_stdout,link_stderr=os.iorunv(os.programfile(),{'f','-y','-p','cross','--toolchain=egp_link_probe','-P',link_project,'-o',link_output},{curdir=link_project,envs={XMAKE_CONFIGDIR=path.join(link_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-link-environment')}})
    io.writefile(path.join(directory,'link_environment.log'),link_stdout .. (link_stderr or ''))
    local cases=0
    for _ in link_stdout:gmatch('NATIVE_LINK_ENVIRONMENT_CASE_PASS%s+%d+') do cases=cases+1 end
    assert(cases==2,'Two isolated linker environments must pass actual shared-link command checks')
    table.insert(receipt.tests,{name='link_environment',checks=cases*5,status='PASS'})
    print('link_environment: PASS ' .. (cases*5))
    local source_project=path.join(root,'build/xmake/tests/source_flags')
    local source_output=path.join(directory,'source-flags')
    local source_stdout,source_stderr=os.iorunv(os.programfile(),{'f','-y','-p','cross','--toolchain=egp_source_flags_probe','-P',source_project,'-o',source_output},{curdir=source_project,envs={XMAKE_CONFIGDIR=path.join(source_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-source-flags')}})
    io.writefile(path.join(directory,'source_flags.log'),source_stdout .. (source_stderr or ''))
    local sourcechecks=assert(tonumber(source_stdout:match('NATIVE_SOURCE_FLAGS_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='source_flags',checks=sourcechecks,status='PASS'})
    print('source_flags: PASS ' .. sourcechecks)
    import('core.base.json').savefile(path.join(directory,'qualification.json'),receipt)
end
