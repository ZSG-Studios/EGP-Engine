-- Install the same pinned native SDK artifacts used by EGP platform CI.
function main(component, dryrun)
    import('net.http')
    import('utils.archive')
    import('core.base.json')
    local root = path.absolute(path.join(os.scriptdir(),'../..'))
    local localdata = os.getenv('LOCALAPPDATA')
    local deps = os.getenv('EGP_BUILD_DEPS') or (localdata and not os.getenv('MSYSTEM') and path.join(localdata,'Godot/build_deps') or path.join(root,'bin/build_deps'))
    if dryrun ~= 'dry-run' then os.mkdir(deps) end
    local function install(url, destination, subdirectory)
        if dryrun == 'dry-run' then print(url .. ' -> ' .. destination); return end
        local temp = os.tmpfile() .. '.egp-sdk'
        os.mkdir(temp)
        local zip = path.join(temp,'package.zip')
        local unpacked = path.join(temp,'unpacked')
        print('Downloading ' .. url)
        http.download(url,zip)
        archive.extract(zip,unpacked)
        local source = subdirectory and path.join(unpacked,subdirectory) or unpacked
        assert(os.isdir(source),'Downloaded SDK layout does not match its pinned version')
        os.mkdir(destination)
        -- Replace package-owned files only; preserve unrelated SDK installations.
        for _, file in ipairs(os.files(path.join(source,'**'))) do
            local output = path.join(destination,path.relative(file,source))
            os.mkdir(path.directory(output))
            os.cp(file,output)
        end
        os.tryrm(temp)
    end
    if component == 'accesskit' then
        install('https://github.com/godotengine/godot-accesskit-c-static/releases/download/0.23.1/accesskit-c-0.23.1.zip',path.join(deps,'accesskit'),'accesskit-c-0.23.1')
        if dryrun ~= 'dry-run' then io.writefile(path.join(deps,'accesskit/version'),'0.23.1') end
    elseif component == 'angle' or component == 'd3d12' then
        local variants = {'arm64-llvm','x86_32-gcc','x86_32-llvm','x86_64-gcc','x86_64-llvm'}
        if os.host() == 'windows' then table.join2(variants,{'arm64-msvc','x86_32-msvc','x86_64-msvc'})
        elseif os.host() == 'macosx' and component == 'angle' then table.join2(variants,{'arm64-ios','arm64-ios-sim','arm64-macos','x86_64-macos'}) end
        local name = component == 'angle' and 'angle' or 'mesa'
        local release = component == 'angle' and 'chromium/7219' or '25.3.1-3'
        local repo = component == 'angle' and 'godot-angle-static' or 'godot-nir-static'
        for _, variant in ipairs(variants) do
            install('https://github.com/godotengine/' .. repo .. '/releases/download/' .. release .. '/' .. repo .. '-' .. variant .. '-release.zip',path.join(deps,name .. '-' .. variant))
        end
        if component == 'd3d12' then
            install('https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001',path.join(deps,'pix'))
            install('https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.618.5',path.join(deps,'agility_sdk'))
            import('lib.detect.find_tool')
            local gendef = find_tool('x86_64-w64-mingw32-gendef') or find_tool('gendef')
            local dlltool = find_tool('x86_64-w64-mingw32-dlltool') or find_tool('dlltool')
            if dryrun ~= 'dry-run' and gendef and dlltool then
                for _, arch in ipairs({'x64','ARM64'}) do
                    local directory = path.join(deps,'pix/bin',arch)
                    os.vrunv(gendef.program,{path.join(directory,'WinPixEventRuntime.dll')},{curdir=directory})
                    os.vrunv(dlltool.program,{'--machine',arch=='x64' and 'i386:x86-64' or 'arm64','--no-leading-underscore','-d',path.join(directory,'WinPixEventRuntime.def'),'-D','WinPixEventRuntime.dll','-l',path.join(directory,'libWinPixEventRuntime.a')})
                end
            end
        end
    elseif component == 'swappy' then
        install('https://github.com/godotengine/godot-swappy/releases/download/from-source-2025-01-31/godot-swappy.zip',path.join(root,'thirdparty/swappy-frame-pacing'))
    elseif component == 'perfetto' then
        local file = os.tmpfile() .. '.json'
        http.download('https://api.github.com/repos/google/perfetto/releases/latest',file)
        local tag = assert(json.loadfile(file).tag_name)
        os.tryrm(file)
        install('https://github.com/google/perfetto/releases/download/' .. tag .. '/perfetto-cpp-sdk-src.zip',path.join(root,'thirdparty/perfetto'))
    else raise('Unknown SDK component: %s',tostring(component)) end
    print('Native SDK installation complete: ' .. component)
end
