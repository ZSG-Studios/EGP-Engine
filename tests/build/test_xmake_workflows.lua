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
                local platform=path.filename(filename):match('^(%w+)_builds%.yml$')
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
            if key=='XMAKE_FLAGS' or key=='xmake-flags' or (key=='flags' and filename:endswith('egp-cpp.yml')) then
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
    local static=assert(io.readfile(path.join(root,'.github/workflows/static_checks.yml')))
    local setup=assert(static:find('uses: ./.github/actions/godot-deps',1,true))
    assert(setup<assert(static:find('uses: j178/prek-action',1,true)),'Native generator hooks require pinned xmake before prek')
    local ok=utils.trycall(function() validate('compiledb=yes scu_build=yes','deliberate obsolete flags') end)
    assert(not ok,'Removed backend options must be rejected by workflow validation')
    local cpp=assert(io.readfile(path.join(root,'.github/workflows/egp-cpp.yml')))
    local vendor_filters=0
    for _ in cpp:gmatch('"thirdparty/godot%-cpp/%*%*"') do vendor_filters=vendor_filters+1 end
    assert(vendor_filters==2,'Both push and pull-request SDK validation must match nested vendored godot-cpp files')
    assert(not cpp:find('"thirdparty/godot-cpp"',1,true),'The former gitlink filter must not survive vendor flattening')
    count=count+2
    local android=assert(io.readfile(path.join(root,'.github/workflows/android_builds.yml')))
    local ndk_setup=assert(android:find('name: Install the Gradle-pinned Android NDK',1,true))
    assert(ndk_setup<assert(android:find('name: Compilation',1,true)),'Android native compilation must select its pinned NDK first')
    assert(android:find('platform/android/java/app/config.gradle',1,true) and android:find('sdkmanager --sdk_root="$SDK_ROOT" "ndk;$NDK_VERSION"',1,true),'Android CI must read and install the same NDK revision as Gradle')
    assert(android:find('ANDROID_NDK_ROOT=%s\\nANDROID_NDK_HOME=%s\\n',1,true),'Native compilation and libc++ staging must share the selected NDK')
    count=count+3
    local runner=assert(io.readfile(path.join(root,'.github/workflows/runner.yml')))
    assert(not runner:find('web-build:',1,true),'CI must not build the removed WebGL backend')
    local windows=assert(io.readfile(path.join(root,'.github/workflows/windows_builds.yml')))
    assert(windows:find('EGP_MINGW_ROOT=$mingwRoot',1,true) and windows:find('$env:GITHUB_ENV',1,true), 'The installed MSYS compiler SDK root must reach isolated engine and host-tool configurations')
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
