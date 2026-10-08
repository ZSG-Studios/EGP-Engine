set_xmakever("3.1.1")
toolchain("egp_source_flags_probe")
    set_kind("standalone")
    on_check(function() return true end)
    on_load(function(chain)
        import("lib.detect.find_tool")
        local directories={}
        if os.host()=="windows" then
            for _, instance in ipairs(os.dirs("C:/Program Files/Microsoft Visual Studio/*/*")) do
                for _, directory in ipairs({"VC/Tools/Llvm/bin","VC/Tools/Llvm/x64/bin"}) do table.insert(directories,path.join(instance,directory)) end
            end
            table.insert(directories,"C:/Program Files/LLVM/bin")
        end
        local clang=assert(find_tool("clang",{paths=directories}),"Source argument fixture requires Clang")
        local archive=assert(find_tool("llvm-ar",{paths=directories}) or find_tool("ar"),"Source argument fixture requires a native archiver")
        for _, kind in ipairs({"cc","cxx","mm","mxx","as"}) do chain:set("toolset",kind,"clang@" .. clang.program) end
        chain:set("toolset","ar","ar@" .. archive.program)
        -- Only render the native RC command; Unix cannot execute Microsoft's compiler.
        local resources=clang.program
        if os.host()=="windows" then
            local candidates=os.files("C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe")
            table.sort(candidates)
            resources=assert(candidates[#candidates],"Windows resource compiler is missing")
        end
        chain:set("toolset","mrc","rc@" .. resources)
    end)
toolchain_end()

target("source_flags_probe")
    set_kind("static")
    set_toolchains("egp_source_flags_probe")
    on_load(function(target)
        local root=path.absolute("../../../..",os.scriptdir())
        local helper=import("build.xmake.source_flags",{rootdir=root})
        local policy={CCFLAGS={"-fmodules","-fcxx-modules"},CXXFLAGS={"-std=gnu++20","-fexceptions"},
            ASFLAGS={"-DEGP_ASSEMBLER_POLICY=1"},ARFLAGS={"-D"},RCFLAGS={"/DEGP_RESOURCE_POLICY=1"}}
        helper.configure(target,policy)
        local directory=target:autogendir()
        os.mkdir(directory)
        for _, extension in ipairs({"mm","S","rc"}) do
            local filename=path.join(directory,"probe." .. extension)
            io.writefile(filename,"// Native argument fixture, never compiled.\n")
            target:add("files",filename,{force=helper.file(policy)})
            target:data_set("egp_probe." .. extension,filename)
        end
    end)
    on_config(function(target)
        import("core.tool.compiler")
        import("core.tool.linker")
        local function arguments(kind,extension)
            local instance=assert(compiler.load(kind,{target=target}))
            local _,argv=instance:compargv(target:data("egp_probe." .. extension),"probe.o",{target=target})
            return table.concat(argv," ")
        end
        local objc=arguments("mxx","mm")
        for _, flag in ipairs({"-fmodules","-fcxx-modules","-std=gnu++20","-fexceptions"}) do assert(objc:find(flag,1,true),"Objective-C++ command lost " .. flag) end
        assert(arguments("as","S"):find("-DEGP_ASSEMBLER_POLICY=1",1,true),"Assembly command lost ASFLAGS")
        assert(arguments("mrc","rc"):find("/DEGP_RESOURCE_POLICY=1",1,true),"Resource command lost RCFLAGS")
        local instance=assert(linker.load("static",{"mxx"},{target=target}))
        local _,argv=instance:linkargv({"probe.o"},target:targetfile())
        assert(table.contains(argv,"-D"),"Archive command lost ARFLAGS")
        print("NATIVE_SOURCE_FLAGS_CHECKS=7")
    end)
target_end()
