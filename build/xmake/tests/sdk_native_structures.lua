-- Native structure headers must compile without a lucky transitive include.
function main(api_file)
    local root = os.curdir()
    local json = import('core.base.json')
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
    assert(compiler, 'Clang is required for standalone native structure compilation')
    local folder = path.join(root, '.build/sdk-native-structures')
    os.mkdir(folder)
    local api = json.loadfile(api_file or path.join(root, 'thirdparty/godot-cpp/gdextension/extension_api-4-7.json'))
    table.insert(api.native_structures, {name='FixtureNativeMixed', format='Vector2 point;Vector3 normal;ObjectID owner;RID resource'})
    local synthetic = path.join(folder, 'synthetic-api.json')
    json.savefile(synthetic, api)
    import('build.xmake.generators.binding_generator', {rootdir=root}).main(synthetic,
        path.join(root, 'thirdparty/godot-cpp/gdextension/gdextension_interface.json'), folder, '64', 'single')
    local function compile(source, object)
        local args = {'-std=c++17', '-Werror', object and '-c' or '-fsyntax-only', '-DTYPED_METHOD_BIND',
            '-I' .. path.join(root, 'thirdparty/godot-cpp/include'), '-I' .. path.join(folder, 'gen/include'), source}
        if object then table.join2(args, {'-o', object}) end
        os.iorunv(compiler.program, args, {timeout=60000})
    end
    local checks = 0
    for _, name in ipairs({'fixture_native_mixed', 'physics_server2d_extension_motion_result'}) do
        local header = path.join(folder, 'gen/include/godot_cpp/classes', name .. '.hpp')
        local source = path.join(folder, name .. '.cpp')
        io.writefile(source, '#include <godot_cpp/classes/' .. name .. '.hpp>\n')
        compile(source); checks = checks + 1
        local text = assert(io.readfile(header))
        local previous, count = text:gsub('#include <godot_cpp/core/method_ptrcall.hpp>\n', '')
        assert(count == 1, 'Native pointer support must be an explicit unique dependency')
        io.writefile(header, previous)
        local rejected = false
        try {function () compile(source) end, catch {function (errors)
            local message = tostring(errors)
            rejected = message:find('Vector2', 1, true) ~= nil and message:find('ObjectID', 1, true) ~= nil
        end}}
        io.writefile(header, text)
        assert(rejected, 'The previous mixed-dependency header must reproduce the missing type failure')
        checks = checks + 1
    end
    local headers = {}
    for _, filename in ipairs(os.files(path.join(folder, 'gen/include/godot_cpp/classes/*.hpp'))) do
        local name = io.readfile(filename):match('\nstruct ([%w_]+) {')
        if name then headers[name] = filename end
    end
    for _, native in ipairs(api.native_structures) do
        if native.name ~= 'ObjectID' then
            local header = assert(headers[native.name], 'Missing generated native structure header: ' .. native.name)
            local source = path.join(folder, 'standalone-' .. native.name .. '.cpp')
            io.writefile(source, '#include <godot_cpp/classes/' .. path.filename(header) .. '>\n')
            compile(source); checks = checks + 1
        end
    end
    local object = path.join(folder, 'physics_server2d_extension.o')
    compile(path.join(folder, 'gen/src/classes/physics_server2d_extension.cpp'), object)
    assert(os.isfile(object), 'Cold generated SDK translation unit must publish an actual native object')
    checks = checks + 1
    print('NATIVE_SDK_NATIVE_STRUCTURE_CHECKS=' .. checks)
end
