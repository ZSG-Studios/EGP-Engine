-- Install the same pinned native SDK artifacts used by EGP platform CI.
function pix_dlltool(arch, lookup)
    assert(arch == 'x64' or arch == 'ARM64', 'Unsupported WinPix architecture: ' .. tostring(arch))
    if arch == 'ARM64' then
        -- An x86 GNU dlltool cannot emit ARM64 COFF; generic GNU is intentionally excluded.
        return lookup('aarch64-w64-mingw32-dlltool') or lookup('llvm-dlltool')
    end
    return lookup('x86_64-w64-mingw32-dlltool') or lookup('llvm-dlltool') or lookup('dlltool')
end

function generate_pix_imports(deps, lookup, run, required_arch)
    run = run or os.vrunv
    local gendef = lookup('x86_64-w64-mingw32-gendef') or lookup('gendef')
    for _, arch in ipairs({'x64', 'ARM64'}) do
        local directory = path.join(deps, 'pix/bin', arch)
        assert(os.isfile(path.join(directory, 'WinPixEventRuntime.dll')) and os.isfile(path.join(directory, 'WinPixEventRuntime.lib')), 'Pinned WinPix package is missing its native ' .. arch .. ' DLL/import library')
        local dlltool = pix_dlltool(arch, lookup)
        assert(arch ~= required_arch or (gendef and dlltool), 'MinGW D3D12 requires WinPix ' .. arch .. ' GNU import-library conversion: install gendef and an architecture-compatible dlltool')
        if gendef and dlltool then
            local succeeded, failure = true, nil
            try {function ()
                run(gendef.program, {path.join(directory, 'WinPixEventRuntime.dll')}, {curdir=directory})
                run(dlltool.program, {'-m', arch == 'x64' and 'i386:x86-64' or 'arm64', '--no-leading-underscore', '-d', path.join(directory, 'WinPixEventRuntime.def'), '-D', 'WinPixEventRuntime.dll', '-l', path.join(directory, 'libWinPixEventRuntime.a')})
            end, catch {function (errors) succeeded, failure = false, errors end}}
            if not succeeded then
                local message = 'WinPix ' .. arch .. ' GNU import-library conversion failed: ' .. tostring(failure)
                if arch == required_arch then raise(message) end
                print(message .. '; preserving the installed native MSVC import library.')
            end
        else
            print('WinPix ' .. arch .. ': native MSVC import library installed; GNU conversion unavailable (requires gendef and an architecture-compatible dlltool).')
        end
    end
end
function main(component, dryrun, compiler)
    import('net.http')
    import('utils.archive')
    import('core.base.json')
    local root = path.absolute(path.join(os.scriptdir(),'../..'))
    local deps = import('build.xmake.sdk_paths', {rootdir=root}).dependencies(root)
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
            compiler = compiler or 'msvc'
            assert(compiler == 'msvc' or compiler == 'clang' or compiler == 'gcc' or compiler == 'mingw', 'Unknown Windows SDK compiler: ' .. tostring(compiler))
            if dryrun ~= 'dry-run' then generate_pix_imports(deps, find_tool, nil, (compiler == 'gcc' or compiler == 'mingw') and 'x64' or nil) end
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
