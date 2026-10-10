function main(package)
    local root = os.curdir()
    local installer = import('misc.scripts.install_build_dependencies', {rootdir=root})
    local checks = 0
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local function lookup(available)
        return function (name) return available[name] and {program=available[name]} end
    end
    local x64 = 'x64-gnu'
    local arm = 'arm64-gnu'
    local llvm = 'llvm-all-architectures'
    check(installer.pix_dlltool('x64', lookup({['x86_64-w64-mingw32-dlltool']=x64})).program == x64, 'x64 GNU selection')
    check(not installer.pix_dlltool('ARM64', lookup({['x86_64-w64-mingw32-dlltool']=x64, dlltool=x64})), 'An x64-only GNU tool must never be used for ARM64')
    check(installer.pix_dlltool('ARM64', lookup({['aarch64-w64-mingw32-dlltool']=arm, ['llvm-dlltool']=llvm})).program == arm, 'ARM64 GNU selection')
    check(installer.pix_dlltool('ARM64', lookup({['llvm-dlltool']=llvm, dlltool=x64})).program == llvm, 'LLVM ARM64 fallback')
    check(installer.pix_dlltool('x64', lookup({dlltool=x64})).program == x64, 'Generic x64 GNU fallback')
    local directory = path.join(root, '.build/pix-import-contract')
    os.mkdir(directory)
    for _, arch in ipairs({'x64', 'ARM64'}) do
        local folder = path.join(directory, 'pix/bin', arch)
        os.mkdir(folder)
        io.writefile(path.join(folder, 'WinPixEventRuntime.dll'), 'preserved pinned DLL ' .. arch)
        io.writefile(path.join(folder, 'WinPixEventRuntime.lib'), 'preserved pinned native library ' .. arch)
    end
    local invalid_package = path.join(directory, 'missing-native-package')
    os.mkdir(invalid_package)
    check(not utils.trycall(function () installer.generate_pix_imports(invalid_package, lookup({})) end), 'Missing native package files must remain fatal even for MSVC')
    local commands = {}
    installer.generate_pix_imports(directory, lookup({gendef='gendef', dlltool=x64}), function (program, args)
        table.insert(commands, {program=program, args=args})
    end)
    check(#commands == 2 and commands[2].program == x64 and commands[2].args[2] == 'i386:x86-64', 'Available x64 conversion must succeed independently of absent ARM64 conversion')
    for _, arch in ipairs({'x64', 'ARM64'}) do
        check(io.readfile(path.join(directory, 'pix/bin', arch, 'WinPixEventRuntime.lib')) == 'preserved pinned native library ' .. arch, 'GNU conversion must preserve native import libraries')
    end
    check(not utils.trycall(function () installer.generate_pix_imports(directory, lookup({dlltool=x64}), nil, 'x64') end), 'Missing gendef must clearly reject required MinGW conversion')
    check(not utils.trycall(function () installer.generate_pix_imports(directory, lookup({gendef='gendef'}), nil, 'x64') end), 'Missing dlltool must clearly reject required MinGW conversion')
    local calls = 0
    installer.generate_pix_imports(directory, lookup({gendef='gendef', dlltool=x64}), function () calls = calls + 1 end, 'x64')
    check(calls == 2, 'Required MinGW x64 conversion must not fail because an optional ARM64 tool is absent')
    local available = lookup({gendef='gendef', ['x86_64-w64-mingw32-dlltool']=x64, ['llvm-dlltool']=llvm})
    check(utils.trycall(function ()
        installer.generate_pix_imports(directory, available, function (program)
            if program == 'gendef' then raise('fixture gendef failed') end
        end)
        return true
    end), 'Available but failing gendef must not invalidate native MSVC SDK installation')
    check(utils.trycall(function ()
        installer.generate_pix_imports(directory, available, function (program)
            if program ~= 'gendef' then raise('fixture dlltool failed') end
        end)
        return true
    end), 'Available but failing optional converters must not invalidate native MSVC installation')
    local required_calls = 0
    check(utils.trycall(function ()
        installer.generate_pix_imports(directory, available, function (program, args)
            if program ~= 'gendef' then
                if args[2] == 'arm64' then raise('fixture ARM64 converter failed') end
                required_calls = required_calls + 1
            end
        end, 'x64')
        return true
    end) and required_calls == 1, 'Optional ARM64 failure must not undo successful required MinGW x64 conversion')
    for _, failed_program in ipairs({'gendef', x64}) do
        local failure
        try {function ()
            installer.generate_pix_imports(directory, available, function (program)
                if program == failed_program then raise('fixture required converter failure') end
            end, 'x64')
        end, catch {function (errors) failure = tostring(errors) end}}
        check(failure and failure:find('fixture required converter failure', 1, true), 'Required MinGW conversion must propagate the original tool failure')
    end
    for _, arch in ipairs({'x64', 'ARM64'}) do
        check(io.readfile(path.join(directory, 'pix/bin', arch, 'WinPixEventRuntime.lib')) == 'preserved pinned native library ' .. arch, 'All converter failures must preserve native import libraries')
    end
    local workflow = io.readfile(path.join(root, '.github/workflows/_platform-windows.yml'))
    check(workflow:find('install_build_dependencies.lua d3d12 install "${{ matrix.compiler }}"', 1, true), 'The Windows matrix must explicitly select compiler-specific SDK qualification')
    if package then
        import('lib.detect.find_tool')
        local tool = find_tool('llvm-dlltool')
        if not tool and os.host() == 'windows' then
            local candidates = os.files('C:/Program Files/Microsoft Visual Studio/*/Community/VC/Tools/Llvm/x64/bin/llvm-dlltool.exe')
            if #candidates > 0 then tool = {program=candidates[1]} end
        end
        assert(tool, 'Actual package regression requires LLVM COFF tools')
        local readobj = path.join(path.directory(tool.program), 'llvm-readobj' .. (os.host() == 'windows' and '.exe' or ''))
        for _, arch in ipairs({'x64', 'ARM64'}) do
            local dll = path.join(package, 'bin', arch, 'WinPixEventRuntime.dll')
            local native = path.join(package, 'bin', arch, 'WinPixEventRuntime.lib')
            check(os.isfile(dll) and os.isfile(native), 'Real pinned package must contain both native architectures')
            local before = hash.sha256(native)
            local exports = os.iorunv(readobj, {'--coff-exports', dll})
            local names = {}
            for name in exports:gmatch('Name: ([^\r\n]+)') do table.insert(names, name) end
            check(#names > 0, 'Extract actual DLL exports for the import-library probe')
            local definition = path.join(directory, arch .. '.def')
            io.writefile(definition, 'LIBRARY WinPixEventRuntime.dll\nEXPORTS\n' .. table.concat(names, '\n') .. '\n')
            local output = path.join(directory, arch .. '.a')
            local chosen = installer.pix_dlltool(arch, lookup({['llvm-dlltool']=tool.program}))
            os.iorunv(chosen.program, {'-m', arch == 'x64' and 'i386:x86-64' or 'arm64', '--no-leading-underscore', '-d', definition, '-D', 'WinPixEventRuntime.dll', '-l', output})
            local members = os.iorunv(readobj, {'--file-headers', output})
            check(members:find(arch == 'x64' and 'IMAGE_FILE_MACHINE_AMD64' or 'IMAGE_FILE_MACHINE_ARM64', 1, true), 'Real generated COFF import library must match the requested architecture')
            check(hash.sha256(native) == before, 'Real native package library must remain untouched')
        end
    end
    print('XMAKE_PIX_IMPORTS_PASS ' .. checks)
end
