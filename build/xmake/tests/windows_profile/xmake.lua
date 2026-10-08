set_xmakever("3.1.1")

target("windows_profile")
    set_kind("phony")
    on_config(function()
        local root=path.absolute("../../../..", os.projectdir())
        local policy=import("build.xmake.platforms.init", {rootdir=root})
        local hosts=import("build.xmake.platforms.host", {rootdir=root})
        local gcc=import("core.tools.gcc")
        local checks=0
        local function check(value,message) assert(value,message); checks=checks+1 end
        local function capture(profile)
            local options=table.join({platform="windows",arch="x86_64",accesskit=false,angle=false,d3d12=false},profile)
            local probe={values={}}
            function probe:set(key,...) self.values[key]={...}; if key=="toolchains" then self.settings=self.values[key][2] end end
            function probe:add(key,...)
                self.values[key]=self.values[key] or {}
                for _,value in ipairs({...}) do if type(value)~="table" then table.insert(self.values[key],value) end end
            end
            policy.configure(probe,options)
            return probe
        end
        local profiles={{},{use_llvm=true},{use_mingw=true},{use_mingw=true,use_llvm=true}}
        for _,profile in ipairs(profiles) do
            local auto=capture(table.join(profile,{lto="auto"}))
            local expected=profile.use_mingw and (profile.use_llvm and "-flto=thin" or "-flto") or nil
            for _,key in ipairs({"cxflags","ldflags","shflags"}) do
                local values=auto.values[key] or {}
                check(expected and table.contains(values,expected) or not expected and not table.contains(values,"/GL") and not table.contains(values,"-flto"),"Windows automatic LTO must preserve upstream compiler policy")
                check(table.contains(values,"-fwhole-program")==not not(profile.use_mingw and not profile.use_llvm),"GCC MinGW automatic LTO needs the upstream whole-program workaround")
                check(table.contains(values,"-fno-use-linker-plugin")==not not(profile.use_mingw and not profile.use_llvm),"GCC MinGW automatic LTO must disable the linker plugin")
            end
            local disabled=capture(table.join(profile,{lto="none",use_static_cpp=false}))
            check(not table.contains(disabled.values.ldflags or {},"-static"),"Dynamic runtime option must not force static linking")
            local static=capture(table.join(profile,{use_static_cpp=true}))
            check(table.contains(static.values.ldflags or {},"-static")==not not profile.use_mingw,"Static MinGW runtime must retain complete upstream static link policy")
            local thin_ok=utils.trycall(function() capture(table.join(profile,{lto="thin"})) end)
            check(not not thin_ok==not not profile.use_llvm,"ThinLTO requires LLVM for both Windows ABI families")
            local full=capture(table.join(profile,{lto="full",msvc_version="14.51",mssdk_version="10.0.26100.0"}))
            local compile_flag=(profile.use_mingw or profile.use_llvm) and "-flto" or "/GL"
            check(table.contains(full.values.cxflags,compile_flag),"Explicit full LTO must select the compiler-family flag")
            for _,key in ipairs({"ldflags","shflags"}) do
                check(table.contains(full.values[key],profile.use_mingw and "-flto" or "/LTCG"),"Full LTO must reach binary and shared-library links")
            end
            check(table.contains(full.values.arflags or {},"/LTCG")==not profile.use_mingw,"MSVC-family LTO must reach native archive creation")
            check((full.settings.vs_toolset=="14.51")==not profile.use_mingw,"Explicit MSVC version must route only to MSVC ABI toolchains")
            check((full.settings.vs_sdkver=="10.0.26100.0")==not profile.use_mingw,"Explicit Windows SDK version must route only to MSVC ABI toolchains")
            local incremental=capture(table.join(profile,{incremental_link=true}))
            check(not table.contains(incremental.values.ldflags or {},"/INCREMENTAL:NO"),"Explicit incremental linking must not be silently disabled")
            check(table.contains(disabled.values.ldflags or {},"/INCREMENTAL:NO")==not profile.use_mingw,"Default nonincremental flag belongs to MSVC-family links")
            check(table.contains(full.values.cxflags,"-Wa,-mbig-obj")==not not profile.use_mingw,"Big-object assembly policy must remain MinGW scoped")
            for _,library in ipairs({"mingw32","d3d9","ksuser","uuid"}) do
                check(table.contains(full.values.syslinks,library)==not not profile.use_mingw,"Windows GNU base SDK libraries must preserve upstream identity and scope")
            end
            for _,arch in ipairs({"x86_32","x86_64","arm64"}) do
                local runtime=capture(table.join(profile,{arch=arch,use_static_cpp=true}))
                for _,key in ipairs({"ldflags","shflags"}) do
                    for _,flag in ipairs({"-static-libgcc","-static-libstdc++"}) do
                        check(table.contains(runtime.values[key] or {},flag)==not not(profile.use_mingw and arch=="x86_32"),"Extra static-runtime switches must remain x86-only")
                    end
                end
            end
            if profile.use_mingw then
                local driver={program=function() return "g++" end,is_plat=function() return false end}
                local _,compile=gcc.compargv(driver,"probe.cpp","probe.o",full.values.cxflags,{rawargs=true})
                local _,binary=gcc.linkargv(driver,{"probe.o"},"binary","probe.exe",full.values.ldflags,{rawargs=true})
                local _,shared=gcc.linkargv(driver,{"probe.o"},"shared","probe.dll",full.values.shflags,{rawargs=true})
                for _,argv in ipairs({compile,binary,shared}) do
                    check(table.contains(argv,"-flto"),"Installed GNU mapper must preserve explicit full LTO")
                    check(table.contains(argv,"-fno-use-linker-plugin")==not profile.use_llvm,"Installed GNU mapper must retain the compiler-scoped linker-plugin workaround")
                    check(table.contains(argv,"-fwhole-program")==not profile.use_llvm,"Installed GNU mapper must retain the compiler-scoped whole-program workaround")
                end
                check(auto.settings.clang==not not profile.use_llvm,"Engine toolchain must select the requested MinGW compiler")
                local host=hosts.select(table.join(profile,{platform="windows"}),"windows","x64")
                check(host.clang==not not profile.use_llvm,"Host generator toolchain must agree with engine compiler")
                local old=os.getenv("EGP_MINGW_ROOT"); os.setenv("EGP_MINGW_ROOT",nil)
                local args=hosts.configure_arguments(host,{})
                os.setenv("EGP_MINGW_ROOT",old)
                check(table.contains(args,"--egp_mingw_clang=y")==not not profile.use_llvm,"Host native configure must preserve LLVM selection without an SDK override")
            end
        end
        for _,arch in ipairs({"x86_32","x86_64","arm32","arm64"}) do
            for _,llvm in ipairs({false,true}) do
                for _,san in ipairs({"asan","ubsan"}) do
                    local options={use_mingw=true,use_llvm=llvm,arch=arch,["use_"..san]=true}
                    local ok,result=utils.trycall(function() return capture(options) end)
                    local legal=llvm and (arch=="x86_32" or arch=="x86_64")
                    check(not not ok==legal,"MinGW sanitizers must reject unsupported compiler/architecture combinations")
                    if legal then
                        for _,key in ipairs({"cxflags","ldflags","shflags"}) do
                            check(table.contains(result.values[key] or {},"-fno-sanitize=vptr")== (san=="ubsan"),"COM vptr exclusion must apply only to MinGW UBSAN")
                        end
                    end
                end
            end
        end
        -- Invoke the installed xmake toolchain's real loader; discovery is not needed.
        local toolchains=import("core.tool.toolchain")
        for _,llvm in ipairs({false,true}) do
            local instance=assert(toolchains.load("mingw",{plat="mingw",arch="x64",configs={clang=llvm}}))
            instance:config_set("clang",llvm)
            instance:load()
            check(table.concat(table.wrap(instance:get("toolset.cc"))," "):find(llvm and "clang" or "gcc",1,true)~=nil,"Installed MinGW loader must select the requested real compiler family")
        end
        local driver={program=function() return "g++" end,is_plat=function() return false end}
        local directory=path.absolute(import("core.project.config").builddir(),os.projectdir())
        os.mkdir(directory)
        for _,profile in ipairs(profiles) do
            local probe=capture(table.join(profile,{angle=true,angle_libs=directory}))
            for _,name in ipairs({"ANGLE","EGL","GLES"}) do
                local identity=(profile.use_mingw and "" or "lib")..name..".windows.x86_64"
                check(table.contains(probe.values.syslinks,identity),"ANGLE must preserve the GNU versus MSVC archive identity")
                if profile.use_mingw then check(gcc.nf_syslink(driver,identity)=="-l"..identity,"Installed GNU linker mapper must avoid a doubled library prefix") end
            end
        end
        -- Link three real archives using the production GNU identities. The old
        -- MSVC-style names are a negative control against the same files.
        local find_tool=import("lib.detect.find_tool")
        local compiler=find_tool("clang++")
        if not compiler and os.host()=="windows" then
            local paths={}
            for _,variable in ipairs({"ProgramFiles","ProgramFiles(x86)"}) do
                local prefix=os.getenv(variable)
                if prefix then
                    for _,instance in ipairs(os.dirs(path.join(prefix,"Microsoft Visual Studio/*/*"))) do
                        table.insert(paths,path.join(instance,"VC/Tools/Llvm/x64/bin"))
                        table.insert(paths,path.join(instance,"VC/Tools/Llvm/bin"))
                    end
                    table.insert(paths,path.join(prefix,"LLVM/bin"))
                end
            end
            compiler=find_tool("clang++",{force=true,paths=paths})
        end
        assert(compiler,"Clang is required for the real GNU archive link control")
        local ar=assert(find_tool("llvm-ar",{paths={path.directory(compiler.program)}}))
        local proof=path.join(directory,"ANGLE libraries with spaces")
        os.mkdir(proof)
        local common=os.host()=="windows" and {"--target=x86_64-w64-windows-gnu","-fuse-ld=lld"} or {}
        local identities={}
        for _,name in ipairs({"ANGLE","EGL","GLES"}) do
            local source,object=path.join(proof,name..".c"),path.join(proof,name..".o")
            io.writefile(source,"int "..name.."_probe(void) { return 14; }\n")
            os.iorunv(compiler.program,table.join(common,{"-x","c","-c",source,"-o",object}),{timeout=30000})
            local identity=name..".windows.x86_64"
            table.insert(identities,identity)
            os.iorunv(ar.program,{"rcs",path.join(proof,"lib"..identity..".a"),object},{timeout=30000})
        end
        local source,object=path.join(proof,"main.c"),path.join(proof,"main.o")
        io.writefile(source,"extern int ANGLE_probe(void), EGL_probe(void), GLES_probe(void); int probe_entry(void) { return ANGLE_probe()+EGL_probe()+GLES_probe()!=42; }\n")
        os.iorunv(compiler.program,table.join(common,{"-x","c","-c",source,"-o",object}),{timeout=30000})
        local linker={program=function() return compiler.program end,is_plat=function() return false end}
        local flags=table.join(common,{"-nostdlib","-Wl,-e,"..(os.host()=="macosx" and "_probe_entry" or "probe_entry"),"-L"..proof})
        local negative=table.clone(flags)
        for _,identity in ipairs(identities) do table.insert(flags,gcc.nf_syslink(linker,identity)); table.insert(negative,gcc.nf_syslink(linker,"lib"..identity)) end
        local _,argv=gcc.linkargv(linker,{object},"binary",path.join(proof,"linked"),flags,{rawargs=true})
        os.iorunv(compiler.program,argv,{timeout=30000})
        check(os.isfile(path.join(proof,"linked")) or os.isfile(path.join(proof,"linked.exe")),"Production GNU ANGLE identities must link all real archives")
        local _,bad=gcc.linkargv(linker,{object},"binary",path.join(proof,"negative"),negative,{rawargs=true})
        local rejected=false
        try {function() os.iorunv(compiler.program,bad,{timeout=30000}) end,catch {function(errors) rejected=tostring(errors):find("libANGLE.windows",1,true)~=nil end}}
        check(rejected,"Original double-lib ANGLE identity must fail against the same archive files")
        import("core.base.json").savefile(path.join(proof,"receipt.json"),{passed=true,compiler=compiler.program,argv=argv,negative_double_lib_rejected=true,boundary=os.host()=="windows" and "Clang GNU-driver COFF link; MSYS GCC not executed locally" or "Native Clang GNU-driver link"})
        print("NATIVE_WINDOWS_PROFILE_CHECKS="..checks)
    end)

if is_host("windows") then
    for _,profile in ipairs({{name="msvc_full",lto="full"},{name="clang_full",lto="full",llvm=true},{name="clang_thin",lto="thin",llvm=true}}) do
        target("lto_"..profile.name)
            set_kind("phony")
            on_load(function(target)
                import("build.xmake.platforms.init",{rootdir=path.absolute("../../../..",os.scriptdir())}).configure(target,{
                    platform="windows",arch="x86_64",use_llvm=profile.llvm,lto=profile.lto,accesskit=false,angle=false,d3d12=false,
                    optimize="none",debug_symbols=false,warnings="no",windows_subsystem="console"})
            end)
            on_config(function(target)
                local directory=path.absolute(import("core.project.config").builddir(),os.projectdir())
                os.mkdir(directory)
                local library=path.join(directory,profile.name.."-library.cpp")
                local source=path.join(directory,profile.name.."-main.cpp")
                io.writefile(library,"int lto_probe() { return 42; }\n")
                io.writefile(source,"int lto_probe(); int main() { return lto_probe()!=42; }\n")
                local compiler=assert(import("core.tool.compiler").load("cxx",{target=target}))
                local objects={}
                for _,file in ipairs({library,source}) do
                    local object=file..".obj"
                    local program,argv=compiler:compargv(file,object,{target=target})
                    assert(table.contains(argv,profile.llvm and (profile.lto=="thin" and "-flto=thin" or "-flto") or "/GL"),"Actual compiler argv lost LTO")
                    os.vrunv(program,argv,{envs=compiler:runenvs(),timeout=60000})
                    table.insert(objects,object)
                end
                local archive=path.join(directory,profile.name..".lib")
                local archiver=assert(import("core.tool.linker").load("static",{"cxx"},{target=target}))
                local ar,arargv=archiver:linkargv({objects[1]},archive,{target=target})
                import("core.base.json").savefile(path.join(directory,profile.name.."-archive-command.json"),{program=ar,argv=arargv,configured_tool=target:get("toolset.ar")})
                assert((path.basename(ar):lower()=="llvm-lib")==not not profile.llvm,"LLVM bitcode must use the exact LLVM librarian rather than the MSVC alias")
                assert(table.contains(arargv,"/LTCG"),"Actual archive argv lost LTO")
                os.vrunv(ar,arargv,{envs=archiver:runenvs(),timeout=60000})
                local linker=assert(import("core.tool.linker").load("binary",{"cxx"},{target=target}))
                local executable=path.join(directory,profile.name..".exe")
                local program,argv=linker:linkargv({objects[2],archive},executable,{target=target})
                assert((path.basename(program):lower()=="lld-link")==not not profile.llvm,"LLVM LTO must use the LLVM linker")
                assert(table.contains(argv,"/LTCG"),"Actual native linker argv lost LTO")
                os.vrunv(program,argv,{envs=linker:runenvs(),timeout=60000})
                os.vrunv(executable,{},{timeout=30000})
                import("core.base.json").savefile(path.join(directory,profile.name.."-receipt.json"),{passed=true,compiler=compiler:program(),archiver=ar,archive_argv=arargv,linker=program,link_argv=argv})
                print("NATIVE_WINDOWS_LTO_CHECKS=7")
            end)
    end
end
