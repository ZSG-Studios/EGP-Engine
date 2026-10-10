-- Qualify the full pinned upstream extension fixture without changing SDK inputs.
function main()
    local root = os.curdir()
    local prepare = import('misc.scripts.prepare_godot_cpp_fixture', {rootdir=root})
    local directory = path.join(root, '.build/godot-cpp-fixture-contract/source-only')
    local receipt = prepare.main(directory, root)
    local source = path.join(root, 'thirdparty/godot-cpp/test')
    local checks, files = 0, 0
    local function check(value, message) assert(value, message); checks=checks+1 end
    local patched = {['src/my_test.cpp']=true, ['src/my_test.h']=true, ['project/main.gd']=true}
    local expected = {}
    for _, relative in ipairs({'.gitignore', 'README.md', 'build_profile.json', 'doc_classes/MyTestMain.xml',
            'project/default_env.tres', 'project/icon.png', 'project/icon.png.import', 'project/main.gd',
            'project/main.gd.uid', 'project/main.tscn', 'project/my_test.gd', 'project/my_test.gd.uid',
            'project/my_test.gdextension', 'project/my_test.gdextension.uid', 'project/project.godot',
            'project/test_base.gd', 'project/test_base.gd.uid', 'project/test_typed_dictionary.gd',
            'run-tests.sh', 'src/my_test.cpp', 'src/my_test.h', 'src/register_types.cpp',
            'src/register_types.h', 'src/tests.h'}) do
        expected[relative] = true
    end
    check(receipt.revision == assert(io.readfile(path.join(root, 'thirdparty/godot-cpp/UPSTREAM.md'))):match('`(%x+)`'), 'Fixture and SDK must share the recorded upstream source revision')
    for _, filename in ipairs(prepare.source_files(source)) do
        local relative = path.relative(filename, source):gsub('\\', '/')
        local destination = path.join(receipt.project, relative)
        files=files+1
        check(expected[relative] and os.isfile(destination), 'Every pinned upstream test file must be retained: ' .. relative)
        expected[relative] = nil
        if not patched[relative] then
            check(hash.sha256(filename) == hash.sha256(destination), 'Unadapted upstream test files must remain byte-identical: ' .. relative)
        end
    end
    check(files == receipt.files and files == 24, 'Pinned fixture must copy its exact complete source, project and documentation corpus')
    local generated = path.join(directory, 'generated-source')
    os.mkdir(path.join(generated, 'src'))
    os.mkdir(path.join(generated, 'project/bin/ignored.framework/Resources'))
    io.writefile(path.join(generated, 'src/fixture.cpp'), 'upstream source')
    io.writefile(path.join(generated, 'project/bin/ignored.framework/Resources/Info.plist'), 'generated library metadata')
    local inventory = prepare.source_files(generated)
    check(#inventory == 1 and path.filename(inventory[1]) == 'fixture.cpp', 'Generated project/bin outputs must never enter the pinned source-copy inventory')
    local native = assert(io.readfile(path.join(receipt.project, 'src/my_test.cpp')))
    local script = assert(io.readfile(path.join(receipt.project, 'project/main.gd')))
    check(not native:find('classes/multiplayer_', 1, true) and not native:find('test_send_rpc', 1, true), 'Existing EGP RPC-only adaptation must apply to the matching MyTest fixture')
    check(script:find('test_variant_iterator', 1, true) and script:find('# Virtual method.', 1, true), 'Neighboring upstream test cases must be preserved')
    check(assert(io.readfile(path.join(receipt.project, 'src/register_types.cpp'))):find('my_test_library_init', 1, true), 'The complete upstream extension entry point must survive preparation')
    local include = path.join(root, 'thirdparty/godot-cpp/include')
    check(prepare.validate_templates(receipt.project, include) == receipt.template_includes and receipt.template_includes >= 17, 'Every actual upstream template include must resolve against the pinned SDK source')
    local negative = path.join(directory, 'negative/src')
    os.mkdir(negative)
    io.writefile(path.join(negative, 'tests.h'), '#include <godot_cpp/templates/search_array.hpp>\n')
    local failure
    try {function () prepare.validate_templates(path.directory(negative), include) end, catch {function (errors) failure=tostring(errors) end}}
    check(failure and failure:find('search_array.hpp', 1, true), 'The former moving-branch header mismatch must be rejected with its missing dependency')
    local action = assert(io.readfile(path.join(root, '.github/actions/test-godot-cpp/action.yml')))
    local workflow = assert(io.readfile(path.join(root, '.github/workflows/_platform-linux.yml')))
    check(action:find('prepare_godot_cpp_fixture.lua', 1, true) and not action:find('actions/checkout', 1, true), 'CI must prepare the bundled matching fixture instead of selecting an unrelated upstream branch')
    check(not workflow:find('GODOT_CPP_BRANCH', 1, true) and not action:find('godot-cpp-branch', 1, true), 'Obsolete floating fixture branch configuration must be removed')
    local configuration = assert(io.readfile(path.join(receipt.project, 'project/my_test.gdextension')))
    check(configuration:find('libmytest.linux.template_debug.x86_64.so', 1, true) and action:find('--name libmytest.linux.template_debug.x86_64', 1, true), 'The built native library must match the upstream project loader')
    print('XMAKE_GODOT_CPP_FIXTURE_PASS ' .. checks)
end
