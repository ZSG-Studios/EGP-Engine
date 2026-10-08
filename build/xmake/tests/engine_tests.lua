-- Engine unit tests and standalone qualification executables use separate graphs.
function main()
    local root=os.curdir()
    local model=import('build.xmake.graph',{rootdir=root})
    local checks=0
    local function check(value,message) assert(value,message); checks=checks+1 end
    local excluded={
        'tests/physics/box3d/determinism.cpp',
        'tests/physics/box3d/joints.cpp',
        'tests/compatibility_test/src/compat_checker.c',
        'modules/egp_net/tests/core_checks.cpp',
        'build/xmake/tests/generated_objects/ordinary.cpp',
        'thirdparty/box2d/test/main.c'
    }
    local preserved={
        'tests/test_main.cpp','tests/test_macros.cpp','tests/test_utils.cpp','tests/signal_watcher.cpp',
        'tests/core/io/test_pck_packer.cpp','tests/scene/test_physics_material.cpp','tests/servers/test_nav_heap.cpp'
    }
    for _, profile in ipairs({
        {platform='windows',arch='x86_64',target='template_release'},
        {platform='windows',arch='x86_64',target='editor'},
        {platform='linuxbsd',arch='x86_64',target='template_debug'},
        {platform='macos',arch='arm64',target='template_release'},
        {platform='android',arch='arm64',target='template_debug',disable_physics_2d=true,disable_physics_3d=true},
        {platform='ios',arch='arm64',target='template_release'},
        {platform='visionos',arch='arm64',target='template_release'},
        {platform='web',arch='wasm32',target='template_release',disable_physics_2d=true,disable_physics_3d=true}
    }) do
        profile.dev_mode=true
        local graph=model.new(root,profile)
        graph:configure()
        check(graph.options.tests==true,'dev_mode must retain actual engine unit tests on ' .. profile.platform)
        local tests
        for _, library in ipairs(graph.libraries) do if library.name=='tests' then tests=library; break end end
        check(tests,'The dev_mode engine test archive must exist on ' .. profile.platform)
        local sources={}
        for _, source in ipairs(tests.sources) do sources[(path.relative(source.path.path,root):gsub('\\','/'))]=true end
        for _, source in ipairs(excluded) do check(not sources[source],'Standalone qualification source leaked into engine tests: ' .. source) end
        for _, source in ipairs(preserved) do check(sources[source],'Engine unit-test translation unit was incorrectly excluded: ' .. source) end
        local force_link
        for _, generator in ipairs(graph.generators) do
            if generator.generator:endswith('force_link_builder') then force_link=generator; break end
        end
        check(force_link and force_link.inputs[1].literal,'Force-link generation must use the filtered unit-test list')
        local anchors={}
        for _, source in ipairs(force_link.inputs[1].value) do anchors[(path.relative(source,root):gsub('\\','/'))]=true end
        check(anchors['tests/core/io/test_pck_packer.cpp'] and anchors['tests/scene/test_physics_material.cpp'],'Engine unit-test force-link anchors must survive filtering')
        check(not anchors['tests/physics/box3d/determinism.cpp'] and not anchors['tests/physics/box3d/joints.cpp'],'Standalone main functions must not receive engine force-link anchors')
    end
    local disabled=model.new(root,{platform='windows',target='template_release',dev_mode=true,tests=false})
    disabled:configure()
    local tests=false
    for _, library in ipairs(disabled.libraries) do if library.name=='tests' then tests=true end end
    check(not tests,'Explicit tests=false must override dev_mode without creating the unit-test archive')
    print('NATIVE_ENGINE_TEST_GRAPH_CHECKS=' .. checks)
end
