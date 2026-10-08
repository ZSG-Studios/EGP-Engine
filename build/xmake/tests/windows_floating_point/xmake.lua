set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags",false)

local graphs={}
local components={
    {name="engine",source="core/math/vector3.cpp"},
    {name="network",source="modules/egp_net/egp_net_session.cpp"},
    {name="box2d",source="modules/box2d/box2d_physics_server_2d.cpp"},
    {name="box3d",source="modules/box3d/scene_backend/servers/box3d_physics_server_3d.cpp"}
}
for _,compiler_name in ipairs({"msvc","clang-cl"}) do
    for _,component in ipairs(components) do
        target("floating_point_" .. compiler_name:gsub("%-","_") .. "_" .. component.name)
            set_kind("object")
            on_load(function(target)
                local root=path.absolute("../../../..",os.scriptdir())
                if not graphs[compiler_name] then
                    local snapshot=os.tmpfile()
                    os.vrunv(os.programfile(),{"lua",path.join(os.scriptdir(),"source_graph.lua"),root,compiler_name,snapshot},{curdir=root,timeout=120000})
                    graphs[compiler_name]=import("core.base.json").loadfile(snapshot)
                    os.tryrm(snapshot)
                end
                local graph=graphs[compiler_name]
                local selected=graph.policies[component.source]
                assert(selected,"The actual engine graph must provide " .. component.source)
                import("build.xmake.platforms.init",{rootdir=root}).configure(target,graph.options,selected.BUILD_ENV)
                target:add("files",path.join(os.scriptdir(),"probe.cpp"),{force=import("build.xmake.source_flags",{rootdir=root}).file(selected)})
                target:data_set("egp.fp.source",component.source)
            end)
            on_config(function(target)
                local compiler=assert(import("core.tool.compiler").load("cxx",{target=target}))
                local object=path.join(path.absolute(import("core.project.config").builddir(),os.projectdir()),target:name() .. ".obj")
                os.mkdir(path.directory(object))
                local source=assert(target:sourcefiles()[1])
                local program,arguments=compiler:compargv(source,object,{target=target})
                import("core.base.json").savefile(object .. ".arguments.json",{source=target:data("egp.fp.source"),compiler=program,arguments=arguments})
                local strict=component.name=="engine"
                assert(table.contains(arguments,"/fp:strict")==strict,"Only ordinary engine sources must retain the strict floating-point model")
                assert(table.contains(arguments,"/fp:precise")== (not strict and compiler_name=="msvc"),"MSVC deterministic modules must retain their precise model")
                assert(table.contains(arguments,"/clang:-ffp-contract=off")== (not strict and compiler_name=="clang-cl"),"Clang-cl deterministic modules must disable contraction without conflicting models")
                assert(table.contains(arguments,"/clang:-fno-fast-math")== (not strict and compiler_name=="clang-cl"),"Clang-cl deterministic modules must reject unsafe fast math")
                assert(not table.contains(arguments,"-Wno-overriding-option"),"Floating-point conflicts must not be hidden by diagnostic suppression")
                os.vrunv(program,arguments,{envs=compiler:runenvs(),timeout=60000})
                assert(os.isfile(object),"The full graph-derived compiler command must compile with Werror")
                local checks=6
                if compiler_name=="msvc" and strict then
                    local root=path.absolute("../../../..",os.scriptdir())
                    local declaration=assert(io.readfile(path.join(root,"core/string/ustring.cpp")):match("#ifdef _MSC_VER[^\n]*\n#define _CRT_SECURE_NO_WARNINGS[^\n]*\n#endif"),"The CRT regression must compile the actual ustring macro declaration")
                    local crt_source,crt_object=os.tmpfile() .. ".cpp",os.tmpfile() .. ".obj"
                    io.writefile(crt_source,declaration .. "\nstatic_assert(sizeof(int) >= 2);\n")
                    for _,argument in ipairs(arguments) do
                        assert(not argument:find("_CRT_SECURE_NO_WARNINGS",1,true),"Platform flags must not redefine ustring's local CRT policy")
                    end
                    local _,crt_args=compiler:compargv(crt_source,crt_object,{target=target,compflags=compiler:compflags({target=target,sourcefile=source})})
                    os.vrunv(program,crt_args,{envs=compiler:runenvs(),timeout=60000})
                    assert(os.isfile(crt_object),"The actual source-derived CRT declaration must compile under the production MSVC flags")
                    local accepted,failure=true,nil
                    try {function() os.vrunv(program,table.join(crt_args,{"/D_CRT_SECURE_NO_WARNINGS"}),{envs=compiler:runenvs(),timeout=60000}) end,
                        catch {function(errors) accepted,failure=false,errors end}}
                    assert(not accepted and tostring(failure):find("C4005",1,true),"The redundant global CRT macro must reproduce the real MSVC failure")
                    os.tryrm(crt_source); os.tryrm(crt_object)
                    checks=checks+3
                end
                if compiler_name=="clang-cl" then
                    local trace=table.join(arguments,{"/clang:-###"})
                    local stdout,stderr=os.iorunv(program,trace,{envs=compiler:runenvs(),timeout=60000})
                    assert(((stdout or "") .. (stderr or "")):find('"-ffp-contract=off"',1,true),"The actual Clang frontend must disable contraction for engine and module sources")
                    checks=checks+1
                    if component.name=="network" then
                        local previous=table.join(arguments,{"/fp:strict","/fp:precise","/clang:-ffp-contract=off"})
                        local ok,failure=true,nil
                        try {function() os.vrunv(program,previous,{envs=compiler:runenvs(),timeout=60000}) end,
                            catch {function(errors) ok,failure=false,errors end}}
                        assert(not ok and tostring(failure):find("overriding",1,true),"The previous target/source collision must fail the actual compiler negative control")
                        checks=checks+1
                    end
                end
                import("core.base.json").savefile(object .. ".json",{source=target:data("egp.fp.source"),compiler=program,arguments=arguments,checks=checks})
                print("NATIVE_WINDOWS_FLOATING_POINT_CASE_PASS=" .. target:name() .. ":" .. checks)
            end)
        target_end()
    end
end
