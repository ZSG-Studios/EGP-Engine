-- Validate engine flags in every Actions entry point against the actual native options.
function main()
    local root=os.curdir()
    local declared={arch=true,platform=true,target=true}
    for name in assert(io.readfile(path.join(root,'build/xmake/options.lua'))):gmatch('option%("([%w_]+)"%)') do declared[name]=true end
    for _, folder in ipairs(os.dirs(path.join(root,'modules/*'))) do declared['module_' .. path.filename(folder) .. '_enabled']=true end
    local count=0
    local sdk_fields={
        D3D12_ENABLED={option='d3d12',fallback='no'},
        ACCESSKIT_ENABLED={option='accesskit',fallback='no'}, SWAPPY_ENABLED={option='swappy',fallback='no'},
        VULKAN_ENABLED={option='vulkan',fallback='no'}, PROFILER_ENABLED={option='profiler',fallback='none'}
    }
    local sdk_seen,sdk_flags={},{}
    local common=import('build.xmake.common',{rootdir=root})
    local defaults=import('core.base.json').loadfile(path.join(root,'build/xmake/defaults.json'))
    assert(defaults.profiler=='none','Disabled profiler fallback must match the native default')
    local function sdk_fallback(option,expression)
        local field,value=expression:match("steps%.[%w_%-]+%.outputs%.([%w_]+)%s*||%s*'([^']+)'%s*$")
        local specification=sdk_fields[field]
        assert(specification and specification.option==option and specification.fallback==value,
            'Optional SDK output needs an explicit disabled fallback: ' .. option .. '=' .. expression)
        return value,field
    end
    local function validate(value, source)
        for key in value:gmatch('([%w_]+)%s*=') do
            assert(declared[key],'Unknown or removed native engine option ' .. key .. ' in ' .. source)
            count=count+1
        end
    end
    for _, filename in ipairs(os.files(path.join(root,'.github/**.yml'))) do
        local text=assert(io.readfile(filename))
        for option,expression in text:gmatch('([%w_]+)=%$%{%{(.-)%}%}') do
            local field=expression:match('steps%.[%w_%-]+%.outputs%.([%w_]+)')
            if field and field:endswith('_ENABLED') then
                local value=sdk_fallback(option,expression)
                sdk_seen[field]=true
                local platform=path.filename(filename):match('^_platform%-(%w+)%.yml$')
                assert(platform,'Optional SDK flags must belong to a platform build workflow')
                sdk_flags[platform]=sdk_flags[platform] or {}
                sdk_flags[platform][option]=value
                local options=table.copy(defaults)
                options.platform,options.target,options[option]=platform=='linux' and 'linuxbsd' or platform,'template_debug',value
                local environment={graph={root=root}}
                function environment:add(fields) for key,values in pairs(fields) do self[key]=values end end
                common.configure(environment,options,{platform=true,target=true,[option]=true})
                assert(options[option]==(option=='profiler' and 'none' or false),'Missing SDK output must disable its native feature')
                count=count+2
            end
        end
        local indent
        for line in (text .. '\n'):gmatch('(.-)\n') do
            if line:find('xmake lua misc/scripts/build_egp.lua',1,true) then validate(line,filename) end
            local nativeflags=line:match('^%s*FLAGS="(.*)')
            if nativeflags then validate(nativeflags,filename) end
            local spaces,key,value=line:match('^(%s*)([%w_%-]+):%s*(.*)$')
            if key=='XMAKE_FLAGS' or key=='xmake-flags' or (key=='flags' and filename:endswith('cpp-sdk.yml')) then
                if value:match('^[>|]') then indent=#spaces else indent=nil; validate(value,filename) end
            elseif indent then
                local padding=line:match('^(%s*)')
                if #padding>indent then validate(line,filename) else indent=nil end
            end
        end
        local scripts=text:gsub('%$GITHUB_WORKSPACE/',''):gsub('%$%{%{%s*github%.workspace%s*%}%}/','')
        for _, prefix in ipairs({'misc/','build/','tests/','modules/','doc/'}) do
            for position,script in scripts:gmatch('()(' .. prefix .. '[%w_./%-]+%.%w+)') do
                local extension=script:match('%.(%w+)$')
                local preceding=scripts:sub(position-1,position-1)
                if not preceding:match('[%w_./%-]') and table.contains({'py','lua','sh','ps1','groovy'},extension) then
                    assert(os.isfile(path.join(root,script)),'Missing workflow script: ' .. script)
                end
            end
        end
    end
    for field in pairs(sdk_fields) do assert(sdk_seen[field],'Missing optional SDK failure coverage: ' .. field); count=count+1 end
    local valid=utils.trycall(function() sdk_fallback('accesskit','steps.accesskit-sdk.outputs.ACCESSKIT_ENABLED') end)
    assert(not valid,'An optional SDK expression without a fallback must fail the contract')
    count=count+1
    local launcher=import('misc.scripts.build_egp',{rootdir=root})
    for platform,flags in pairs(sdk_flags) do
        local arguments={}
        for option,value in pairs(flags) do table.insert(arguments,option .. '=' .. value) end
        launcher.main(platform=='linux' and 'linuxbsd' or platform,'template_debug',1,'.build/xmake-workflow-failure-fixture',table.concat(arguments,' '),'dry-run')
        count=count+1
    end
    valid=utils.trycall(function() launcher.main('windows','template_debug',1,'.build/xmake-workflow-failure-fixture','accesskit=','dry-run') end)
    assert(not valid,'The native launcher must keep rejecting empty build-option values')
    count=count+1
    local static=assert(io.readfile(path.join(root,'.github/workflows/_static-checks.yml')))
    local setup=assert(static:find('uses: ./.github/actions/setup-toolchain',1,true))
    assert(setup<assert(static:find('uses: j178/prek-action',1,true)),'Native generator hooks require pinned xmake before prek')
    local ok=utils.trycall(function() validate('compiledb=yes scu_build=yes','deliberate obsolete flags') end)
    assert(not ok,'Removed backend options must be rejected by workflow validation')
    local cpp=assert(io.readfile(path.join(root,'.github/workflows/cpp-sdk.yml')))
    local vendor_filters=0
    for _ in cpp:gmatch("'thirdparty/godot%-cpp/%*%*'") do vendor_filters=vendor_filters+1 end
    assert(vendor_filters==1,'SDK validation must match nested vendored godot-cpp files')
    assert(not cpp:find("'thirdparty/godot-cpp'",1,true),'The former gitlink filter must not survive vendor flattening')
    count=count+2
    local android=assert(io.readfile(path.join(root,'.github/workflows/_platform-android.yml')))
    local ndk_setup=assert(android:find('name: Install the Gradle-pinned Android NDK',1,true))
    assert(ndk_setup<assert(android:find('name: Build\n',1,true)),'Android native compilation must select its pinned NDK first')
    assert(android:find('platform/android/java/app/config.gradle',1,true) and android:find('sdkmanager --sdk_root="$SDK_ROOT" "ndk;$NDK_VERSION"',1,true),'Android CI must read and install the same NDK revision as Gradle')
    assert(android:find('ANDROID_NDK_ROOT=%s\\nANDROID_NDK_HOME=%s\\n',1,true),'Native compilation and libc++ staging must share the selected NDK')
    count=count+3
    local ci=assert(io.readfile(path.join(root,'.github/workflows/ci.yml')))
    local nightly=assert(io.readfile(path.join(root,'.github/workflows/nightly.yml')))
    for _, entry in ipairs({ci,nightly}) do
        assert(not entry:find('_platform-web',1,true),'CI must not build the removed WebGL backend')
    end
    count=count+1
    for _, filename in ipairs(os.files(path.join(root,'.github/workflows/*.yml'))) do
        local text=assert(io.readfile(filename))
        assert(not text:find('pull_request',1,true),'Self-hosted workflows must not run pull requests: ' .. path.filename(filename))
        for runner in text:gmatch('runs%-on:%s*([^\n]+)') do
            assert(runner:find('self-hosted',1,true),'GitHub-hosted runners are not used: ' .. path.filename(filename))
            count=count+1
        end
        count=count+1
    end
    for _, platform in ipairs({'windows','linux','macos','android','ios','visionos'}) do
        local workflow='.github/workflows/_platform-' .. platform .. '.yml'
        assert(nightly:find('uses: ./' .. workflow,1,true),'Shipping platform missing from the nightly matrix: ' .. platform)
        if platform~='ios' and platform~='visionos' then
            assert(ci:find('uses: ./' .. workflow,1,true),'Core platform missing from per-commit CI: ' .. platform)
        end
        local text=assert(io.readfile(path.join(root,workflow)))
        assert(text:find('workflow_call:',1,true) and text:find('uses: ./.github/actions/build-engine',1,true),
            'Platform must reuse the native build entry point: ' .. platform)
        assert(text:match('timeout%-minutes:%s*%d+'),'Platform build must have a bounded job: ' .. platform)
        if platform=='windows' or platform=='linux' or platform=='macos' then
            assert(text:find('validate_egp_unit_summary.lua',1,true),'Desktop runtime tests need a validated test summary: ' .. platform)
            count=count+1
        end
        count=count+3
    end
    local immersive=assert(io.readfile(path.join(root,'.github/workflows/visionos-immersive.yml')))
    assert(immersive:find('branches: [master]',1,true),'Immersive CI must follow the maintained master branch')
    for _, filter in ipairs({'build/xmake/**','drivers/metal/**','drivers/apple_embedded/**','servers/xr/**','.github/actions/build-engine/**'}) do
        assert(immersive:find("'" .. filter .. "'",1,true),'Immersive PR validation omits a native dependency: ' .. filter)
        count=count+1
    end
    local contracts=assert(io.readfile(path.join(root,'.github/workflows/_xmake-contracts.yml')))
    assert(contracts:match('timeout%-minutes:%s*%d+'),'Native compiler contract jobs must be bounded')
    assert(not android:lower():find('firebase',1,true) and not android:find('SERVICE_ACCOUNT_KEY',1,true),'EGP Android builds must not use upstream-only Firebase credentials')
    count=count+2
    local linux=assert(io.readfile(path.join(root,'.github/workflows/_platform-linux.yml')))
    assert(linux:find('use_llvm=yes',1,true) and not linux:find('linker=mold',1,true),'Linux CI must build with Clang/LLVM only')
    local windows=assert(io.readfile(path.join(root,'.github/workflows/_platform-windows.yml')))
    assert(windows:find('use_llvm=${{ steps.toolchain.outputs.USE_LLVM }}',1,true) and not windows:find('use_mingw',1,true),'Windows CI must keep the Clang toolchain selectable and must not build MinGW')
    count=count+2
    local native=assert(io.readfile(path.join(root,'platform/android/java/scripts/native-build.gradle')))
    for key in native:gmatch('"([%w_]+)=') do
        assert(declared[key],'Unknown Android Gradle native engine option ' .. key)
        count=count+1
    end
    for _, file in ipairs({'build.gradle','lib/build.gradle','nativeSrcsConfigs/build.gradle'}) do
        local script=assert(io.readfile(path.join(root,'platform/android/java',file)))
        assert(not script:lower():find('scons',1,true),'Android Gradle must not require the removed build tool: ' .. file)
        assert(not script:find('externalNativeBuild',1,true),'Android Gradle must not configure the removed native editing backend: ' .. file)
        count=count+2
    end
    print('XMAKE_WORKFLOW_OPTIONS_PASS ' .. count)
end
