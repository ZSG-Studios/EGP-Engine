-- Native post-link staging and packaging. All programs are invoked as argument arrays.
local policy = import('init', {rootdir=os.scriptdir()})

local function copy(source, destination)
    assert(os.isfile(source), 'Required package input missing: ' .. source)
    os.mkdir(path.directory(destination))
    os.cp(source,destination)
end
local function zip(context, directory, output)
    assert(context.zip_executable, 'Native egp_zip host tool is required for packaging')
    os.vrunv(context.zip_executable, {'--root',directory,'--output',output})
end
local function reset(context, directory)
    local absolute, base=path.absolute(directory),path.absolute(context.bin_dir)
    local relative=path.relative(absolute,base)
    assert(relative~='.' and not relative:startswith('..') and not path.is_absolute(relative),'Package staging path escapes its explicit output directory')
    if os.isdir(absolute) then os.rm(absolute) end
    os.mkdir(absolute)
end
local function substitute(source, replacements)
    local value = assert(io.readfile(source))
    for marker,replacement in pairs(replacements) do value=value:gsub(marker,function() return replacement end) end
    return value
end
local function suffix(options, target, extra)
    local value = '.' .. options.platform .. '.' .. target
    if policy.enabled(options.dev_build) then value=value .. '.dev' end
    if options.precision=='double' then value=value .. '.double' end
    return value, extra or ''
end
local function lipo(prefix, extra)
    local inputs={}
    for _, arch in ipairs({'x86_64','arm64','arm32','x86_32'}) do
        local file=prefix .. '.' .. arch .. extra
        if os.isfile(file) then table.insert(inputs,file) end
    end
    if #inputs==0 then return nil end
    if #inputs==1 then return inputs[1] end
    local output=prefix .. '.fat' .. extra
    os.vrunv('lipo',table.join({'-create'},inputs,{'-output',output}))
    return output
end

function android(context)
    local options=context.options
    local arches={arm32={'armeabi-v7a','arm-linux-androideabi'},arm64={'arm64-v8a','aarch64-linux-android'},x86_32={'x86','i686-linux-android'},x86_64={'x86_64','x86_64-linux-android'}}
    local arch=assert(arches[options.arch],'Unsupported Android package architecture')
    local editor=options.target=='editor'
    local build=(options.target~='template_release' and not (editor and policy.enabled(options.store_release))) and 'debug' or 'release'
    local base=path.join(context.root,'platform/android/java/lib/libs',editor and 'tools' or '',build)
    local directory=path.join(base,arch[1])
    copy(context.targetfile,path.join(directory,'libgodot_android.so'))
    local ndk=options.ANDROID_NDK_ROOT or options.ndk or os.getenv('ANDROID_NDK_ROOT') or os.getenv('ANDROID_NDK_HOME') or (os.getenv('ANDROID_HOME') and path.join(os.getenv('ANDROID_HOME'),'ndk/29.0.14206865'))
    assert(ndk,'Android NDK path is required to stage libc++_shared.so')
    local host=({windows='windows-x86_64',macosx='darwin-x86_64',linux='linux-x86_64'})[os.host()]
    copy(path.join(ndk,'toolchains/llvm/prebuilt',assert(host),'sysroot/usr/lib',arch[2],'libc++_shared.so'),path.join(directory,'libc++_shared.so'))
    if policy.enabled(options.debug_symbols) and policy.enabled(options.separate_debug_symbols) then
        zip(context,base,path.join(context.bin_dir,'android-' .. (editor and 'editor-' or 'template-') .. build .. '-native-symbols.zip'))
    end
    if policy.enabled(options.generate_android_binaries) then
        local tasks=editor and {'generateGodotEditor','generateGodotHorizonOSEditor','generateGodotPicoOSEditor'} or {policy.enabled(options.module_mono_enabled) and 'generateGodotMonoTemplates' or 'generateGodotTemplates'}
        table.insert(tasks,'--quiet')
        if policy.enabled(options.debug_symbols) and not policy.enabled(options.separate_debug_symbols) then table.insert(tasks,'-PdoNotStrip=true') end
        local command=os.host()=='windows' and 'cmd' or './gradlew'
        if os.host()=='windows' then tasks=table.join({'/c','gradlew.bat'},tasks) end
        os.vrunv(command,tasks,{curdir=path.join(context.root,'platform/android/java')})
    end
end

function web(context)
    import('core.base.json')
    local options=context.options
    local stem=context.targetfile:gsub('%.js$','')
    local sources={'features.js','preloader.js','config.js','engine.js'}
    if options.target=='editor' then table.insert(sources,'iframe.js') end
    local engine=stem .. '.engine.js'
    if policy.enabled(options.use_closure_compiler) then
        import('lib.detect.find_tool')
        local emcc=assert(find_tool('emcc'),'Emscripten compiler missing')
        local args={path.join(path.directory(emcc.program),'node_modules/.bin/google-closure-compiler'),'--compilation_level','ADVANCED_OPTIMIZATIONS','--externs',path.join(context.root,'platform/web/js/engine/engine.externs.js')}
        for _, source in ipairs(sources) do table.insert(args,'--js'); table.insert(args,path.join(context.root,'platform/web/js/engine',source)) end
        table.insert(args,'--js_output_file'); table.insert(args,engine)
        os.vrunv('node',args)
    else
        local parts={}
        for _, source in ipairs(sources) do table.insert(parts,substitute(path.join(context.root,'platform/web/js/engine',source),{___GODOT_THREADS_ENABLED=policy.enabled(options.threads,true) and 'true' or 'false'})) end
        io.writefile(engine,table.concat(parts,'\n'))
    end
    local wrapped=stem .. '.wrapped.js'
    io.writefile(wrapped,assert(io.readfile(context.targetfile)) .. '\n' .. assert(io.readfile(engine)))
    local stage=path.join(context.bin_dir,'.web_zip',path.filename(stem))
    reset(context,stage)
    local name=options.target=='editor' and 'godot.editor' or 'godot'
    copy(wrapped,path.join(stage,name .. '.js'))
    copy(stem .. '.wasm',path.join(stage,name .. '.wasm'))
    for _, worklet in ipairs({'audio.worklet.js','audio.position.worklet.js'}) do copy(path.join(context.root,'platform/web/js/libs',worklet),path.join(stage,name .. '.' .. worklet)) end
    if policy.enabled(options.dlink_enabled) then copy(assert(context.side_wasm),path.join(stage,name .. '.side.wasm')) end
    if options.target=='editor' then
        local cache={'godot.editor.html','godot.editor.iframe.html','offline.html','godot.editor.js','godot.editor.audio.worklet.js','godot.editor.audio.position.worklet.js','logo.svg','favicon.png','inter-regular.woff2','inter-bold.woff2'}
        local replacements={___GODOT_VERSION___=assert(context.build_version),___GODOT_NAME___='GodotEngine',___GODOT_CACHE___=json.encode(cache),___GODOT_OPT_CACHE___=json.encode({'godot.editor.wasm'}),___GODOT_OFFLINE_PAGE___='offline.html',___GODOT_THREADS_ENABLED___=policy.enabled(options.threads,true) and 'true' or 'false',___GODOT_ENSURE_CROSSORIGIN_ISOLATION_HEADERS___='true',___GODOT_EDITOR_FILESIZES___=json.encode({['godot.editor.wasm']=os.filesize(stem .. '.wasm')})}
        io.writefile(path.join(stage,name .. '.html'),substitute(path.join(context.root,'misc/dist/html/editor.html'),replacements))
        io.writefile(path.join(stage,name .. '.iframe.html'),'<!DOCTYPE html>\n<html lang="en"><head><title></title></head></html>')
        io.writefile(path.join(stage,'service.worker.js'),substitute(path.join(context.root,'misc/dist/html/service-worker.js'),replacements))
        for _, pair in ipairs({{'misc/dist/html/logo.svg','logo.svg'},{'misc/logo/icon.png','favicon.png'},{'misc/dist/html/manifest.json','manifest.json'},{'misc/dist/html/offline.html','offline.html'},{'thirdparty/fonts/Inter_Regular.woff2','inter-regular.woff2'},{'thirdparty/fonts/Inter_Bold.woff2','inter-bold.woff2'}}) do copy(path.join(context.root,pair[1]),path.join(stage,pair[2])) end
    else
        copy(path.join(context.root,'misc/dist/html/full-size.html'),path.join(stage,name .. '.html'))
        copy(path.join(context.root,'misc/dist/html/service-worker.js'),path.join(stage,name .. '.service.worker.js'))
        copy(path.join(context.root,'misc/dist/html/offline-export.html'),path.join(stage,'godot.offline.html'))
    end
    zip(context,stage,stem .. '.zip')
end

function macos_bundle(context)
    local options=context.options
    local extra=(context.extra_suffix or '') .. (context.module_version_string or '')
    if options.target=='editor' then
        local prefix=suffix(options,'editor')
        local program=assert(lipo(path.join(context.bin_dir,'godot' .. prefix),extra),'No macOS editor executable to package')
        local bundle=path.join(context.bin_dir,('godot' .. prefix .. extra):gsub('%.','_') .. '.app')
        reset(context,bundle)
        os.cp(path.join(context.root,'misc/dist/macos_tools.app/Contents'),bundle)
        copy(program,path.join(bundle,'Contents/MacOS/Godot'))
        if policy.enabled(options.module_mono_enabled) then os.cp(path.join(context.bin_dir,'GodotSharp'),path.join(bundle,'Contents/Resources')) end
        local version=assert(context.version)
        local full=table.concat({version.major,version.minor,version.patch,version.status,version.build},'.')
        local short=table.concat({version.major,version.minor,version.patch},'.')
        local info=substitute(path.join(context.root,'misc/dist/macos/editor_info_plist.template'),{['%$version']=full,['%$short_version']=short})
        if version.build~='official' and version.build~='steam' then info=info:gsub('org.godotengine.godot',function() return 'org.godotengine.godot.' .. version.build end) end
        io.writefile(path.join(bundle,'Contents/Info.plist'),info)
        if options.bundle_sign_identity and options.bundle_sign_identity~='' then
            os.vrunv('codesign',{'-s',options.bundle_sign_identity,'--deep','--force','--options=runtime','--entitlements',path.join(context.root,'misc/dist/macos',policy.enabled(options.dev_build) and 'editor_debug.entitlements' or 'editor.entitlements'),bundle})
        end
    else
        local stage=path.join(context.bin_dir,'.macos_template_package')
        reset(context,stage)
        os.cp(path.join(context.root,'misc/dist/macos_template.app'),stage)
        for _, build in ipairs({'release','debug'}) do
            local prefix=suffix(options,'template_' .. build)
            local program=lipo(path.join(context.bin_dir,'godot' .. prefix),extra)
            if program then copy(program,path.join(stage,'macos_template.app/Contents/MacOS/godot_macos_' .. build .. '.universal')) end
        end
        local base='godot.macos' .. (policy.enabled(options.dev_build) and '.dev' or '') .. (options.precision=='double' and '.double' or '') .. extra
        zip(context,stage,path.join(context.bin_dir,base:gsub('%.','_') .. '.zip'))
    end
end

function apple_bundle(context)
    local options=context.options
    local platform=options.platform
    local extra=(context.extra_suffix or ''):gsub('%.simulator','')
    local stage=path.join(context.bin_dir,platform .. '_xcode')
    reset(context,stage)
    for _, file in ipairs(os.files(path.join(context.root,'misc/dist/apple_embedded_xcode/**'))) do copy(file,path.join(stage,path.relative(file,path.join(context.root,'misc/dist/apple_embedded_xcode')))) end
    local framework=platform=='ios' and 'ios-arm64' or 'xros-arm64'
    local simulator=platform=='ios' and 'ios-arm64_x86_64-simulator' or 'xros-arm64-simulator'
    local modules={''}
    for _, module in ipairs(context.external_modules or {}) do table.insert(modules,module) end
    for _, module in ipairs(modules) do
        for _, build in ipairs({'release','debug'}) do
            local prefix=suffix(options,'template_' .. build)
            for _, sim in ipairs({false,true}) do
                local program=lipo(path.join(context.bin_dir,'libgodot' .. module .. prefix),(sim and '.simulator' or '') .. extra .. '.a')
                if program then copy(program,path.join(stage,'libgodot' .. module .. '.' .. platform .. '.' .. build .. '.xcframework',sim and simulator or framework,'libgodot' .. module .. '.a')) end
            end
        end
    end
    if platform=='ios' and policy.enabled(options.vulkan) then
        assert(context.moltenvk_xcframework,'MoltenVK XCFramework path is required for iOS Vulkan bundle')
        for _, sub in ipairs({'ios-arm64','ios-arm64_x86_64-simulator'}) do os.cp(path.join(context.moltenvk_xcframework,sub),path.join(stage,'MoltenVK.xcframework')) end
        copy(path.join(context.moltenvk_xcframework,'Info.plist'),path.join(stage,'MoltenVK.xcframework/Info.plist'))
    end
    if policy.enabled(options.accesskit) then
        assert(options.accesskit_sdk_path and options.accesskit_sdk_path~='','AccessKit bundle requires its SDK path')
        os.cp(path.join(options.accesskit_sdk_path,'lib/ios/AccessKit.xcframework'),stage)
    end
    local base='godot.' .. platform .. (policy.enabled(options.dev_build) and '.dev' or '') .. (options.precision=='double' and '.double' or '') .. extra
    zip(context,stage,path.join(context.bin_dir,base:gsub('%.','_') .. '.zip'))
end

function finish(context)
    context.root=context.root or os.projectdir()
    context.bin_dir=context.bin_dir or path.join(context.root,'bin')
    local options=context.options
    local platform=options.platform or options.godot_platform
    if (platform=='ios' or platform=='visionos' or options.library_type=='static_library') and #(context.archives or {})>0 then
        local merged=context.targetfile .. '.merged' .. path.extension(context.targetfile)
        local run=context.run or os.vrunv
        if table.contains({'ios','visionos','macos'},platform) then
            run('xcrun',table.join({'libtool','-static','-o',merged,context.targetfile},context.archives))
        elseif platform=='windows' and not policy.enabled(options.use_mingw) then
            run(assert(context.archive_tool,'Native MSVC archive tool is required'),table.join({'/NOLOGO','/OUT:' .. merged,context.targetfile},context.archives))
        else
            local function quoted(filename)
                assert(not filename:find('["\r\n]'),'Archive path contains unsupported MRI control characters')
                return '"' .. filename:gsub('\\','/') .. '"'
            end
            local script={'CREATE ' .. quoted(merged),'ADDLIB ' .. quoted(context.targetfile)}
            for _, archive in ipairs(context.archives) do table.insert(script,'ADDLIB ' .. quoted(archive)) end
            table.join2(script,{'SAVE','END',''})
            local filename=merged .. '.mri'
            io.writefile(filename,table.concat(script,'\n'))
            run(assert(context.archive_tool,'Native archive tool is required'),{'-M'},{stdin=filename})
            os.tryrm(filename)
        end
        assert(os.isfile(merged),'Native archive merge did not produce its output')
        os.mv(merged,context.targetfile)
    end
    local large_mingw=platform=='windows' and policy.enabled(options.use_mingw) and os.filesize(context.targetfile)>=2040109465
    if policy.enabled(options.debug_symbols) and (policy.enabled(options.separate_debug_symbols) or large_mingw) then
        if platform=='macos' then
            local dsym=options.macports_clang and options.macports_clang~='no' and path.join(os.getenv('MACPORTS_PREFIX') or '/opt/local','libexec/llvm-' .. options.macports_clang .. '/bin/llvm-dsymutil') or 'dsymutil'
            os.vrunv(dsym,{context.targetfile,'-o',context.targetfile .. '.dSYM'})
            os.vrunv('strip',{'-u','-r',context.targetfile})
        elseif platform=='linuxbsd' or (platform=='windows' and policy.enabled(options.use_mingw)) then
            os.vrunv(options.OBJCOPY or 'objcopy',{'--only-keep-debug',context.targetfile,context.targetfile .. '.debugsymbols'})
            os.vrunv(options.STRIP or 'strip',{'--strip-debug','--strip-unneeded',context.targetfile})
            os.vrunv(options.OBJCOPY or 'objcopy',{'--add-gnu-debuglink=' .. context.targetfile .. '.debugsymbols',context.targetfile})
        end
    end
    if platform=='windows' and policy.enabled(options.d3d12) then
        import('build.xmake.sdk_paths', {rootdir=context.root}).resolve(options, context.root)
        local agility=options.agility_sdk_path
        if os.isdir(agility) then
            local arch=({x86_32='win32',x86_64='x64',arm32='arm',arm64='arm64'})[options.arch]
            local output=policy.enabled(options.agility_sdk_multiarch) and path.join(context.bin_dir,options.arch) or context.bin_dir
            for _, name in ipairs({'D3D12Core.dll','d3d12SDKLayers.dll'}) do copy(path.join(agility,'build/native/bin',assert(arch),name),path.join(output,name)) end
        end
        if policy.enabled(options.use_pix) then
            local pix=options.pix_path
            copy(path.join(pix,'bin',options.arch=='arm64' and 'arm64' or 'x64','WinPixEventRuntime.dll'),path.join(context.bin_dir,'WinPixEventRuntime.dll'))
        end
    end
    if platform=='android' then android(context)
    elseif platform=='web' then web(context)
    elseif policy.enabled(options.generate_bundle) then
        if platform=='macos' then macos_bundle(context)
        elseif platform=='ios' or platform=='visionos' then apple_bundle(context) end
    end
end
