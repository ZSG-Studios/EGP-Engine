-- Native generator qualification, run with xmake lua tests/build/test_xmake_generators.lua <host-compressor>.
function main(compressor)
    local generators = import('build.xmake.generators.init', {rootdir = os.curdir()})
    local support = import('build.xmake.generators.support', {rootdir = os.curdir()})
    local folder = path.join(os.curdir(), '.build/xmake-generator-tests')
    os.mkdir(folder)
    local context = {root=os.curdir(), compress_executable=assert(compressor)}
    local checks = 0
    local function check(value, message) assert(value, message); checks = checks + 1 end
    local function generate(name, sources, options)
        local output = path.join(folder, name .. '.gen.h')
        generators.generate({builder=name, targets={output}, sources=sources, options=options}, context)
        return assert(io.readfile(output))
    end
    local version = generate('version_info_builder', {{value={short_name='egp',name='EGP',major=4,minor=6,patch=0,status='dev',build='test',module_config='.mono',website='https://example.com',docs_branch='latest'}}})
    check(version:find('#define GODOT_VERSION_MAJOR 4',1,true), 'Numeric version field')
    check(version:find('#define GODOT_VERSION_MODULE_CONFIG ".mono"',1,true), 'Module version field')
    local disabled = generate('disabled_class_builder', {{value={'CSGBox3D'}}})
    check(disabled:find('is_class_enabled<CSGBox3D>',1,true), 'Disabled classes')
    local modules = generate('modules_enabled_builder', {{value={'egp_net','box3d'}}})
    check(modules:find('MODULE_BOX3D_ENABLED',1,true) < modules:find('MODULE_EGP_NET_ENABLED',1,true), 'Stable module define order')
    local module_tests = generate('modules_tests_builder', {{path='modules/zip/tests/test_zip.h'}, {path='modules/jsonrpc/tests/test_jsonrpc.h'}})
    local jsonrpc_header = module_tests:find('#include "modules/jsonrpc/tests/test_jsonrpc.h"',1,true)
    local zip_header = module_tests:find('#include "modules/zip/tests/test_zip.h"',1,true)
    check(jsonrpc_header and zip_header and jsonrpc_header < zip_header, 'Nonempty module test headers must generate in stable native path order')
    check(not module_tests:find('\\',1,true), 'Module test includes must use portable forward slashes')
    check(module_tests:find('// IWYU pragma: begin_keep.',1,true) and module_tests:find('// IWYU pragma: end_keep.',1,true), 'Generated module test includes must retain IWYU guards')
    local wayland = import('build.xmake.generators.wayland', {rootdir=os.curdir()})
    local scanner_output = '#include "wayland-client-core.h"\n#include "wayland-util.h"\n'
    local original_run = os.vrunv
    os.vrunv = function(program, arguments, options)
        check(program == 'fixture-wayland-scanner' and arguments[1] == '-c' and
            (arguments[2] == 'client-header' or arguments[2] == 'private-code') and
            os.isfile(arguments[3]) and options.timeout == 60000, 'Wayland generation must invoke the scanner with a real protocol XML')
        io.writefile(arguments[4], scanner_output)
    end
    local scanner_success, scanner_error = utils.trycall(function()
        for _, mode in ipairs({'client_header', 'private_code'}) do
            for _, wrapped in ipairs({true, false}) do
                local output = path.join(folder, 'relocated/generated/platform/linuxbsd/wayland/protocol', mode .. tostring(wrapped) .. '.gen.h')
                check(wayland.generate({builder='wayland.scanner.' .. mode,targets={output},sources={{path='thirdparty/wayland/protocol/wayland.xml'},{value=wrapped}}},
                    {root=os.curdir(),wayland_scanner='fixture-wayland-scanner'}), 'Both Wayland scanner modes must publish relocated output')
                local actual = assert(io.readfile(output))
                if wrapped then
                    check(not actual:find('../dynwrappers/',1,true), 'Relocated Wayland outputs must not resolve wrappers inside the build directory')
                    for include in actual:gmatch('#include "([^"]+)"') do
                        check(os.isfile(path.join(context.root, include)), 'Generated Wayland includes must resolve through the engine source-root include directory')
                    end
                else
                    check(actual == scanner_output, 'System-linked Wayland must preserve the native scanner includes')
                end
            end
        end
    end)
    os.vrunv = original_run
    assert(scanner_success, scanner_error)
    local key = generate('encryption_key_builder', {{value=string.rep('ab',32)}})
    check(key:find('171, 171',1,true), 'AES exact bytes')
    local compressed = support.compress('hello\0UTF8: café\n' .. string.rep('compressible\n',1000), context)
    io.writefile(path.join(folder,'compression.zlib'),compressed,{encoding='binary'})
    check(compressed:byte(1) == 120 and #compressed < 200, 'Actual level9 zlib compression')
    local licenses = generate('make_license_header', {{path='COPYRIGHT.txt'}, {path='LICENSE.txt'}})
    check(licenses:find('COPYRIGHT_INFO_COUNT',1,true), 'Copyright project metadata')
    check(licenses:find('LICENSE_BODIES',1,true), 'License bodies')
    local icons = generate('make_editor_icons_action', {{path='editor/icons/GodotFile.svg'}, {path='editor/icons/Node.svg'}})
    check(icons:find('editor_icons_count = 2',1,true), 'Icon count')
    check(icons:find('editor_bg_thumbs_indices[] = { 0 }',1,true), 'Zero based thumb indices')
    local profile = generate('profiler_gen_builder', {}, {profiler='tracy',profiler_sample_callstack=true})
    check(profile:find('#define TRACY_CALLSTACK 62',1,true), 'Profiler options')
    local header = generate('core.extension.make_interface_header.run', {{path='core/extension/gdextension_interface.json'}})
    check(header:find('GDExtensionInterfaceVariantNewCopy',1,true), 'Real current engine interface')
    local wrapper = generate('core.extension.make_wrappers.run', {})
    check(wrapper:find('#define EXBIND12RC',1,true) and wrapper:find('#define MODBIND0R',1,true), 'All native binding wrappers')
    local dump = generate('core.extension.make_interface_dumper.run', {{path='core/extension/gdextension_interface.json'}})
    check(dump:find('GDExtensionInterfaceDump',1,true), 'Actual compressed interface dump')
    local docs = generate('make_doc_header', {{path='modules/egp_net/doc_classes/EGPNetSession.xml'}})
    check(docs:match('_doc_data_hash = "%x+"'), 'Stable doc digest')
    local controller = generate('make_default_controller_mappings', {{path='core/input/gamecontrollerdb.txt'}})
    check(controller:find('DefaultControllerMappings::mappings',1,true), 'Controller database')
    local virtuals = generate('core.object.make_virtuals.run', {})
    check(virtuals:find('#define GDVIRTUAL12RC_REQUIRED',1,true) and virtuals:find('#define GDVIRTUAL0_COMPAT',1,true),'All engine virtual arities and qualifiers')
    print('XMAKE_NATIVE_GENERATORS_PASS ' .. checks)
end
