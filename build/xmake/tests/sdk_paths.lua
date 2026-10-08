function main(installed)
    local root = os.projectdir()
    local paths = import('build.xmake.sdk_paths', {rootdir=root})
    local model = import('build.xmake.graph', {rootdir=root})
    local policy = import('build.xmake.platforms.init', {rootdir=root})
    local package = import('build.xmake.platforms.package', {rootdir=root})
    local checks = 0
    local proof_compiler
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local directory = path.join(root, '.build/sdk-path-contract')
    os.mkdir(directory)
    local deps = installed and path.absolute(installed) or path.join(directory, 'deps')
    if not installed then
        for _, compiler in ipairs({'msvc', 'llvm', 'gcc'}) do
            local mesa = path.join(deps, 'mesa-x86_64-' .. compiler, 'godot-mesa')
            os.mkdir(path.join(mesa, 'generated/src/compiler/nir'))
            io.writefile(path.join(mesa, 'VERSION.info'), '25.3.1\n')
            io.writefile(path.join(mesa, 'generated/src/compiler/nir/nir_opcodes.h'), 'fixture generated header')
            os.mkdir(path.join(deps, 'mesa-x86_64-' .. compiler, 'bin'))
            io.writefile(path.join(deps, 'mesa-x86_64-' .. compiler, 'bin/libNIR.windows.x86_64.' .. (compiler == 'msvc' and 'lib' or 'a')), 'fixture NIR archive')
            os.mkdir(path.join(deps, 'angle-x86_64-' .. compiler))
        end
        os.mkdir(path.join(deps, 'pix/bin/x64'))
        io.writefile(path.join(deps, 'pix/bin/x64/WinPixEventRuntime.dll'), 'fixture PIX DLL')
        for _, name in ipairs({'D3D12Core.dll', 'd3d12SDKLayers.dll'}) do
            local file = path.join(deps, 'agility_sdk/build/native/bin/x64', name)
            os.mkdir(path.directory(file)); io.writefile(file, 'fixture Agility ' .. name)
        end
    end
    local old_deps, old_local, old_msystem = os.getenv('EGP_BUILD_DEPS'), os.getenv('LOCALAPPDATA'), os.getenv('MSYSTEM')
    os.setenv('EGP_BUILD_DEPS', deps)
    check(paths.dependencies(root) == deps, 'Explicit dependency root must win')
    os.setenv('MSYSTEM', 'MINGW64')
    check(paths.dependencies(root) == deps, 'Explicit dependency root must also win under MSYS')
    os.setenv('EGP_BUILD_DEPS', nil)
    check(paths.dependencies(root) == path.join(root, 'bin/build_deps'), 'MSYS defaults must use the absolute engine dependency root')
    os.setenv('MSYSTEM', nil)
    os.setenv('LOCALAPPDATA', path.join(directory, 'local'))
    check(paths.dependencies(root) == path.join(directory, 'local/Godot/build_deps'), 'Native Windows default must follow LOCALAPPDATA')
    os.setenv('LOCALAPPDATA', old_local); os.setenv('MSYSTEM', old_msystem); os.setenv('EGP_BUILD_DEPS', deps)
    for index, profile in ipairs({{compiler='msvc'}, {compiler='msvc',use_llvm=true}, {compiler='gcc',use_mingw=true}, {compiler='llvm',use_mingw=true,use_llvm=true}}) do
        local graph = model.new(root, {platform='windows',arch='x86_64',target='template_release',d3d12=true,angle=true,use_pix=true,accesskit=false,use_mingw=profile.use_mingw,use_llvm=profile.use_llvm})
        -- This source/path contract does not require a foreign compiler installation.
        -- Only the old-GCC warning lookup is simulated; all production recipes still execute.
        if profile.use_mingw and not profile.use_llvm then
            local original_methods = graph.methods
            function graph:methods()
                local methods = original_methods(self)
                methods.get_compiler_version = function () return {major=14, minor=0, patch=0} end
                return methods
            end
        end
        graph:configure()
        local options = graph.options
        local mesa = path.join(deps, 'mesa-x86_64-' .. profile.compiler)
        check(options.mesa_libs == mesa, 'Recipes must select the installed ABI-compatible Mesa variant')
        check(options.angle_libs == path.join(deps, 'angle-x86_64-' .. profile.compiler), 'ANGLE must share the compiler/architecture route')
        check(options.pix_path == path.join(deps, 'pix') and options.agility_sdk_path == path.join(deps, 'agility_sdk'), 'PIX and Agility paths must be resolved before recipes')
        local driver
        for _, library in ipairs(graph.libraries) do
            for _, source in ipairs(library.sources) do
                if source.path.path:endswith('rendering_device_driver_d3d12.cpp') then driver = source end
            end
        end
        check(driver and table.contains(driver.environment.CPPDEFINES, 'PIX_ENABLED') and table.contains(driver.environment.CPPDEFINES, 'AGILITY_SDK_ENABLED'), 'Real D3D12 source graph must include the installed PIX and Agility interfaces')
        local string_macros = {}
        for _, definition in ipairs(driver.environment.CPPDEFINES) do
            if type(definition) == 'table' then string_macros[definition[1]] = definition[2] end
        end
        check(string_macros.PACKAGE_VERSION == '"25.3.1"', 'Production Mesa version must be a typed quoted string without shell escapes')
        check(string_macros.PACKAGE_BUGREPORT == '"https://gitlab.freedesktop.org/mesa/mesa/-/issues"', 'Production Mesa URL must be a typed quoted string without shell escapes')
        if index == 1 then
            local find_tool = import('lib.detect.find_tool')
            local compiler = find_tool('clang++')
            if not compiler and os.host() == 'windows' then
                local candidates = {}
                for _, variable in ipairs({'ProgramFiles', 'ProgramFiles(x86)'}) do
                    local prefix = os.getenv(variable)
                    if prefix then
                        for _, instance in ipairs(os.dirs(path.join(prefix, 'Microsoft Visual Studio/*/*'))) do
                            table.insert(candidates, path.join(instance, 'VC/Tools/Llvm/x64/bin'))
                            table.insert(candidates, path.join(instance, 'VC/Tools/Llvm/bin'))
                        end
                        table.insert(candidates, path.join(prefix, 'LLVM/bin'))
                    end
                end
                compiler = find_tool('clang++', {force=true, paths=candidates})
            end
            assert(compiler, 'Clang is required to validate the production Mesa macro literals')
            proof_compiler = compiler
            local source = path.join(directory, 'mesa-macros.cpp')
            io.writefile(source, 'constexpr char version[]=PACKAGE_VERSION;\nconstexpr char bugreport[]=PACKAGE_BUGREPORT;\nstatic_assert(version[0]==\'2\' && bugreport[0]==\'h\', "Mesa string macros changed");\n')
            local function parse(version, bugreport)
                os.iorunv(compiler.program, {'-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-DPACKAGE_VERSION=' .. version, '-DPACKAGE_BUGREPORT=' .. bugreport, source}, {timeout=30000})
            end
            parse(string_macros.PACKAGE_VERSION, string_macros.PACKAGE_BUGREPORT)
            check(true, 'Actual Clang must parse the production graph string macros with warning-as-error')
            local rejected = false
            try {function () parse('\\\\"25.3.1\\\\"', '\\\\"https://gitlab.freedesktop.org/mesa/mesa/-/issues\\\\"') end,
                catch {function (errors) rejected = tostring(errors):find('PACKAGE_', 1, true) ~= nil end}}
            check(rejected, 'Legacy shell-escaped typed values must reproduce the actual compiler failure')
        end
        local has_generated_headers = false
        for _, include in ipairs(driver.environment.CPPPATH) do
            if path.translate(include) == path.translate(path.join(mesa, 'godot-mesa/generated/src/compiler/nir')) then has_generated_headers = true end
        end
        check(has_generated_headers, 'D3D12 source graph must use matching generated NIR headers')
        check(os.isfile(path.join(mesa, 'godot-mesa/generated/src/compiler/nir/nir_opcodes.h')), 'Pinned generated NIR header must exist')
        local probe = {values={}}
        function probe:set(key, ...) self.values[key] = {...} end
        function probe:add(key, ...)
            self.values[key] = self.values[key] or {}
            for _, value in ipairs({...}) do if type(value) ~= 'table' then table.insert(self.values[key], value) end end
        end
        policy.configure(probe, options)
        check(table.contains(probe.values.linkdirs, path.join(mesa, 'bin')), 'Native linker must use the same Mesa selected by recipes')
        local nir = profile.compiler == 'msvc' and 'libNIR.windows.x86_64' or path.absolute(path.join(mesa, 'bin/libNIR.windows.x86_64.a'))
        check(table.contains(probe.values.syslinks, nir), 'Native NIR archive linkage must retain the selected compiler filename')
        if index == 3 then
            local gcc = import('core.tools.gcc')
            local driver = {}
            function driver:is_plat(...) return table.contains({...}, 'mingw') end
            function driver:program() return proof_compiler.program end
            function driver:name() return 'clang' end
            function driver:kind() return 'ld' end
            function driver:get() return {} end
            local linkflag = gcc.nf_syslink(driver, nir)
            check(linkflag == nir, 'GNU native mapper must pass the complete archive path as one argument')
            check(gcc.nf_syslink(driver, 'libNIR.windows.x86_64') == '-llibNIR.windows.x86_64', 'Negative control must retain the original double-lib lookup')
            local proof = path.join(directory, 'GNU archive with spaces')
            os.mkdir(proof)
            local source, main = path.join(proof, 'nir.c'), path.join(proof, 'main.c')
            io.writefile(source, 'int nir_probe(void) { return 42; }\n')
            io.writefile(main, 'extern int nir_probe(void); int probe_entry(void) { return nir_probe() != 42; }\n')
            local object, archive = path.join(proof, 'nir.o'), path.join(proof, 'libNIR.windows.x86_64.a')
            local common = os.host() == 'windows' and {'--target=x86_64-w64-windows-gnu'} or {}
            local find_tool = import('lib.detect.find_tool')
            local ar = find_tool('llvm-ar', {paths={path.directory(proof_compiler.program)}}) or (os.host() ~= 'windows' and find_tool('ar'))
            assert(ar, 'Native archive tool is required for actual GNU link proof')
            os.vrunv(proof_compiler.program, table.join(common, {'-x', 'c', '-c', source, '-o', object}), {timeout=30000})
            local main_object = path.join(proof, 'main.o')
            os.vrunv(proof_compiler.program, table.join(common, {'-x', 'c', '-c', main, '-o', main_object}), {timeout=30000})
            os.vrunv(ar.program, {'rcs', archive, object}, {timeout=30000})
            local flags = {'-nostdlib', '-Wl,-e,' .. (os.host() == 'macosx' and '_probe_entry' or 'probe_entry'), '-L' .. proof}
            if os.host() == 'windows' then table.insert(flags, 1, '-fuse-ld=lld') end
            local _, argv = gcc.linkargv(driver, {main_object}, 'binary', path.join(proof, 'linked'), table.join(common, flags, {gcc.nf_syslink(driver, archive)}), {rawargs=true})
            check(table.contains(argv, archive), 'Actual xmake GNU linker argv must preserve the archive path including spaces')
            local failed, reason = false, ''
            try {function () os.iorunv(proof_compiler.program, table.join(common, flags, {main_object, '-llibNIR.windows.x86_64', '-o', path.join(proof, 'negative')}), {timeout=30000}) end,
                catch {function(errors) failed=true; reason=tostring(errors) end}}
            check(failed and reason:find('libNIR.windows.x86_64',1,true), 'Original double-lib naming must fail with the real archive present')
            io.writefile(path.join(proof, 'negative-control.txt'), reason)
            os.iorunv(proof_compiler.program, argv, {timeout=30000})
            check(os.isfile(path.join(proof, 'linked')) or os.isfile(path.join(proof, 'linked.exe')), 'Corrected GNU linker argv must link the actual archive')
            import('core.base.json').savefile(path.join(proof, 'receipt.json'), {passed=true, compiler=proof_compiler.program, argv=argv,
                boundary=os.host()=='windows' and 'Clang GNU-driver Windows COFF link; MSYS GCC execution not qualified locally' or 'Clang GNU-driver native ELF link', negative_double_lib_failed=true})
        end
        check(os.isfile(path.join(mesa, 'bin/libNIR.windows.x86_64.' .. (profile.compiler == 'msvc' and 'lib' or 'a'))), 'Installed NIR archive must match the selected compiler ABI')
        local destination = path.join(directory, 'package-' .. index)
        os.mkdir(destination)
        local program = path.join(destination, 'fixture.exe'); io.writefile(program, 'fixture program')
        package.finish({root=root,bin_dir=destination,targetfile=program,options=options})
        for _, name in ipairs({'D3D12Core.dll', 'd3d12SDKLayers.dll'}) do
            check(hash.sha256(path.join(destination, name)) == hash.sha256(path.join(deps, 'agility_sdk/build/native/bin/x64', name)), 'Packaging must copy the same installed Agility DLL')
        end
        check(hash.sha256(path.join(destination, 'WinPixEventRuntime.dll')) == hash.sha256(path.join(deps, 'pix/bin/x64/WinPixEventRuntime.dll')), 'Packaging must copy the same installed PIX DLL')
    end
    local override = {platform='windows',arch='x86_64',d3d12=true,angle=true,mesa_libs=path.join(deps,'mesa-x86_64-msvc'),angle_libs=path.join(deps,'angle-x86_64-msvc'),pix_path=path.join(directory,'custom-pix'),agility_sdk_path=path.join(directory,'custom-agility')}
    paths.resolve(override, root)
    check(override.pix_path == path.join(directory,'custom-pix') and override.agility_sdk_path == path.join(directory,'custom-agility'), 'Explicit PIX/Agility overrides must remain intact')
    check(override.mesa_libs == path.join(deps,'mesa-x86_64-msvc') and override.angle_libs == path.join(deps,'angle-x86_64-msvc'), 'Explicit fully selected Mesa/ANGLE overrides must not be replaced')
    local base = path.join(directory, 'custom-base')
    local source = path.join(base .. '-x86_64-msvc', 'godot-mesa')
    os.mkdir(source); io.writefile(path.join(source,'VERSION.info'), '25.3.1')
    local selected = paths.resolve({platform='windows',arch='x86_64',d3d12=true,mesa_libs=base},root)
    check(selected.mesa_libs == base .. '-x86_64-msvc', 'Explicit unsuffixed Mesa base must retain variant discovery')
    for _, value in ipairs({'missing', 'malformed', '25.2.9', '24.3.1', '26.0.0'}) do
        local folder = path.join(directory, 'negative-' .. value, 'godot-mesa'); os.mkdir(folder)
        if value ~= 'missing' then io.writefile(path.join(folder,'VERSION'),value) end
        local failure
        try {function () paths.resolve({platform='windows',arch='x86_64',d3d12=true,mesa_libs=path.directory(folder)},root) end, catch {function (errors) failure=tostring(errors) end}}
        check(failure and failure:find('Direct3D 12',1,true), 'Missing, malformed and incompatible Mesa versions must fail with useful feedback')
    end
    local inactive = {platform='windows',d3d12=false,angle=false,mesa_libs='',angle_libs='',pix_path='',agility_sdk_path=''}
    paths.resolve(inactive,root)
    check(inactive.mesa_libs == '' and inactive.angle_libs == '' and inactive.pix_path == '' and inactive.agility_sdk_path == '', 'Disabled SDK features must preserve existing source options')
    os.setenv('EGP_BUILD_DEPS',old_deps)
    print('NATIVE_SDK_PATH_CHECKS=' .. checks)
end
