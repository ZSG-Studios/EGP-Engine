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
        {'license_literals','build/xmake/tests/license_literals.lua',marker='NATIVE_LICENSE_LITERAL_CHECKS=(%d+)'},
        {'shaders','tests/build/validate_builders.lua'},
        {'packages','tests/build/test_xmake_packages.lua',zipper},
        {'pix_imports','tests/build/test_pix_imports.lua'},
        {'sdk_paths','build/xmake/tests/sdk_paths.lua',marker='NATIVE_SDK_PATH_CHECKS=(%d+)'},
        {'sdk_native_structures','build/xmake/tests/sdk_native_structures.lua',marker='NATIVE_SDK_NATIVE_STRUCTURE_CHECKS=(%d+)'},
        {'godot_cpp_fixture','tests/build/test_godot_cpp_fixture.lua'},
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
    local sanitizer_project=path.join(root,'build/xmake/tests/linux_sanitizer_model')
    local sanitizer_output=path.join(directory,'linux-sanitizer-model')
    local sanitizer_stdout,sanitizer_stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',sanitizer_project,'-o',sanitizer_output},{curdir=sanitizer_project,envs={XMAKE_CONFIGDIR=path.join(sanitizer_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-linux-sanitizer-model')}})
    io.writefile(path.join(directory,'linux_sanitizer_model.log'),sanitizer_stdout .. (sanitizer_stderr or ''))
    local sanitizerchecks=assert(tonumber(sanitizer_stdout:match('NATIVE_LINUX_SANITIZER_MODEL_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='linux_sanitizer_model',checks=sanitizerchecks,status='PASS'})
    print('linux_sanitizer_model: PASS ' .. sanitizerchecks)
    if os.host()=='windows' then
        local exception_project=path.join(root,'build/xmake/tests/windows_exceptions')
        local exception_output=path.join(directory,'windows-exceptions')
        local exception_stdout,exception_stderr=os.iorunv(os.programfile(),{'f','-y','-p','windows','-a','x64','--toolchain=msvc','-P',exception_project,'-o',exception_output},{curdir=exception_project,envs={XMAKE_CONFIGDIR=path.join(exception_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-windows-exceptions')}})
        io.writefile(path.join(directory,'windows_exceptions.log'),exception_stdout .. (exception_stderr or ''))
        local exceptioncases=0
        for _ in exception_stdout:gmatch('NATIVE_WINDOWS_EXCEPTION_CASE_PASS=') do exceptioncases=exceptioncases+1 end
        assert(exceptioncases==6,'MSVC and clang-cl must each qualify default, disabled and enabled engine exception options')
        table.insert(receipt.tests,{name='windows_exceptions',checks=exceptioncases*4,status='PASS'})
        print('windows_exceptions: PASS ' .. (exceptioncases*4))
        local fp_project=path.join(root,'build/xmake/tests/windows_floating_point')
        local fp_output=path.join(directory,'windows-floating-point')
        local fp_stdout,fp_stderr=os.iorunv(os.programfile(),{'f','-y','-p','windows','-a','x64','--toolchain=msvc','-P',fp_project,'-o',fp_output},{curdir=fp_project,envs={XMAKE_CONFIGDIR=path.join(fp_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-windows-floating-point')}})
        io.writefile(path.join(directory,'windows_floating_point.log'),fp_stdout .. (fp_stderr or ''))
        local fp_cases,fp_checks=0,0
        for count in fp_stdout:gmatch('NATIVE_WINDOWS_FLOATING_POINT_CASE_PASS=[^\r\n]+:(%d+)') do fp_cases=fp_cases+1; fp_checks=fp_checks+tonumber(count) end
        assert(fp_cases==8,'MSVC and clang-cl must compile graph-derived engine, networking, Box2D and Box3D floating-point policies')
        table.insert(receipt.tests,{name='windows_floating_point',checks=fp_checks,status='PASS'})
        print('windows_floating_point: PASS ' .. fp_checks)
    end
    local visionos_project=path.join(root,'build/xmake/tests/visionos_toolchain')
    local visionos_output=path.join(directory,'visionos-toolchain')
    local visionos_stdout,visionos_stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',visionos_project,'-o',visionos_output},{curdir=visionos_project,envs={XMAKE_CONFIGDIR=path.join(visionos_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-visionos')}})
    io.writefile(path.join(directory,'visionos_toolchain.log'),visionos_stdout .. (visionos_stderr or ''))
    local visionoschecks=assert(tonumber(visionos_stdout:match('NATIVE_VISIONOS_TOOLCHAIN_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='visionos_toolchain',checks=visionoschecks,status='PASS'})
    print('visionos_toolchain: PASS ' .. visionoschecks)
    local web_project=path.join(root,'build/xmake/tests/web_export_arguments')
    local web_output=path.join(directory,'web-export-arguments')
    local web_envs={XMAKE_CONFIGDIR=path.join(web_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-web-export-arguments')}
    local web_config,web_config_error=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',web_project,'-o',web_output},{curdir=web_project,envs=web_envs})
    local web_stdout,web_stderr=os.iorunv(os.programfile(),{'-P',web_project,'-b','-j','1'},{curdir=web_project,envs=web_envs})
    io.writefile(path.join(directory,'web_export_arguments.log'),web_config .. (web_config_error or '') .. web_stdout .. (web_stderr or ''))
    local web_checks=assert(tonumber(web_stdout:match('NATIVE_WEB_EXPORT_ARGUMENT_CHECKS=(%d+)')))
    table.insert(receipt.tests,{name='web_export_arguments',checks=web_checks,status='PASS'})
    print('web_export_arguments: PASS ' .. web_checks)
    local web_link_project=path.join(root,'build/xmake/tests/web_link_flags')
    local web_link_output=path.join(directory,'web-link-flags')
    local web_link_stdout,web_link_stderr=os.iorunv(os.programfile(),{'f','-y','-p',os.host(),'-a',os.arch(),'--toolchain=' .. hostcompiler,'-P',web_link_project,'-o',web_link_output},{curdir=web_link_project,envs={XMAKE_CONFIGDIR=path.join(web_link_output,'config'),XMAKE_GLOBALDIR=path.join(root,'.build/xmake-global/contract-web-link-flags')}})
    io.writefile(path.join(directory,'web_link_flags.log'),web_link_stdout .. (web_link_stderr or ''))
    local web_link_cases,web_link_checks=0,0
    for count in web_link_stdout:gmatch('NATIVE_WEB_LINK_FLAG_CHECKS=(%d+)') do web_link_cases=web_link_cases+1; web_link_checks=web_link_checks+tonumber(count) end
    assert(web_link_cases==2 and web_link_checks>0,'Binary and shared native linker argv must preserve every repeated Emscripten option and operand')
    table.insert(receipt.tests,{name='web_link_flags',checks=web_link_checks,status='PASS'})
    print('web_link_flags: PASS ' .. web_link_checks)
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
