set_xmakever("3.1.1")
toolchain("egp_link_probe")
    set_kind("standalone")
    on_check(function() return true end)
    on_load(function(chain)
        import("lib.detect.find_tool")
        local candidates={}
        if os.host()=="windows" then
            for _, instance in ipairs(os.dirs("C:/Program Files/Microsoft Visual Studio/*/*")) do
                for _, directory in ipairs({"VC/Tools/Llvm/bin","VC/Tools/Llvm/x64/bin"}) do table.insert(candidates,path.join(instance,directory)) end
            end
            table.insert(candidates,"C:/Program Files/LLVM/bin")
        end
        local compiler=assert(find_tool("clang",{paths=candidates}),"Link command fixture requires the installed Clang compiler")
        chain:set("toolset","sh","clang@" .. compiler.program)
        chain:set("toolset","cc","clang@" .. compiler.program)
    end)
toolchain_end()

for index, arguments in ipairs({"--externs 'fixture one.js'", "--externs 'fixture two.js'"}) do
    target("link_environment_" .. index)
        set_kind("shared")
        set_toolchains("egp_link_probe", {egp_closure_args=arguments})
        set_languages("c11")
        on_load(function(target)
            local root=path.absolute("../../../..",os.scriptdir())
            import("build.xmake.platforms.build_environment",{rootdir=root}).configure(target,"egp_link_probe",arguments)
            import("build.xmake.linking",{rootdir=root}).flags(target,{LINKFLAGS={"-Wl,--egp-shared-recipe-marker"}})
        end)
        on_config(function(target)
            import("core.tool.linker")
            local instance=assert(linker.load("shared",{"cc"},{target=target}))
            local program,argv=instance:linkargv({"probe.o"},target:targetfile())
            assert(path.filename(program):lower():find("clang",1,true),"Linker command must use the actual configured Clang tool")
            assert(table.concat(argv," "):find("--egp-shared-recipe-marker",1,true),"Shared linker command lost recipe LINKFLAGS")
            local chain=target:toolchain("egp_link_probe")
            assert(instance:runenvs().EMCC_CLOSURE_ARGS==arguments,"Actual linker tool must receive scoped Closure externs")
            import("build.xmake.platforms.build_environment",{rootdir=path.absolute("../../../..",os.scriptdir())}).configure(target,"egp_link_probe",arguments)
            assert(chain:runenvs().EMCC_CLOSURE_ARGS==arguments,"Repeated configuration must not duplicate environment arguments")
            assert(os.getenv("EMCC_CLOSURE_ARGS")~=arguments,"Target must not change process environment")
            print("NATIVE_LINK_ENVIRONMENT_CASE_PASS",index)
        end)
    target_end()
end
