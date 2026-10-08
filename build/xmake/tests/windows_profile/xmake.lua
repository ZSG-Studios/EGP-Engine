set_xmakever("3.1.1")

target("windows_profile")
    set_kind("phony")
    on_config(function(target)
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
        local accesskit_sdk=path.join(directory,"AccessKit SDK")
        os.mkdir(path.join(accesskit_sdk,"include"))
        for _,profile in ipairs(profiles) do
            local disabled=capture(profile)
            local selected=capture(table.join(profile,{accesskit=true,accesskit_sdk_path=accesskit_sdk}))
            local function position(values,name)
                for index,value in ipairs(values) do if value==name then return index end end
            end
            local consumer=position(selected.values.syslinks,"accesskit")
            local expected=profile.use_mingw and "accesskit,runtimeobject,propsys,oleaut32,user32,userenv,ntdll" or "accesskit,runtimeobject,propsys,userenv"
            check(table.concat(selected.values.syslinks,",",consumer)==expected,"Complete AccessKit dependency ordering must match the compiler-scoped upstream policy")
            for _,dependency in ipairs({"oleaut32","user32","ntdll"}) do
                local index=position(selected.values.syslinks,dependency)
                check(index and consumer and ((index>consumer)==not not profile.use_mingw),"Only GNU AccessKit must defer its import dependencies until after its archive consumer")
            end
            check(position(disabled.values.syslinks,"ntdll")<position(disabled.values.syslinks,"hid"),"AccessKit-disabled base library ordering must remain unchanged")
            for _,dependency in ipairs({"oleaut32","user32","ntdll"}) do
                local count=0
                for _,identity in ipairs(selected.values.syslinks) do if identity==dependency then count=count+1 end end
                check(count==1,"AccessKit dependency must not rely on duplicated syslinks surviving xmake deduplication")
            end
        end
        -- Real archive link controls retain AccessKit and engine dependency coverage.
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
        local ar=assert(find_tool("llvm-ar",{paths={path.directory(compiler.program)}}) or (os.host()~="windows" and find_tool("ar")),"A native archive tool is required for the GNU link controls")
        if os.host()=="windows" or os.host()=="linux" then
            local chain_compiler=os.host()=="linux" and assert(find_tool("gcc")) or compiler
            local chain_common=os.host()=="windows" and {"--target=x86_64-linux-gnu"} or {}
            local chain_driver={program=function() return chain_compiler.program end,is_plat=function() return false end}
            local accesskit=path.join(directory,"GNU AccessKit dependency")
            os.mkdir(accesskit)
            for name,body in pairs({accesskit="extern int NtWriteFile(void), Ole_probe(void), User_probe(void); int accesskit_probe(void) { return NtWriteFile()+Ole_probe()+User_probe(); }",ntdll="int NtWriteFile(void) { return 14; }",oleaut32="int Ole_probe(void) { return 14; }",user32="int User_probe(void) { return 14; }",main="extern int accesskit_probe(void); int probe_entry(void) { return accesskit_probe()!=42; }"}) do
                local source,object=path.join(accesskit,name..".c"),path.join(accesskit,name..".o")
                io.writefile(source,body.."\n")
                os.iorunv(chain_compiler.program,table.join(chain_common,{"-x","c","-c",source,"-o",object}),{timeout=30000})
                if name~="main" then os.iorunv(ar.program,{"rcs",path.join(accesskit,"lib"..name..".a"),object},{timeout=30000}) end
            end
            local selected_accesskit=capture({use_mingw=true,accesskit=true,accesskit_sdk_path=accesskit_sdk})
            local fixed_accesskit=table.join(chain_common,{"-nostdlib","-Wl,-e,probe_entry","-L"..accesskit,os.host()=="linux" and "-fuse-ld=bfd" or "-fuse-ld=lld"})
            if os.host()=="windows" then table.join2(fixed_accesskit,{"-Wl,--warn-backrefs","-Wl,--fatal-warnings"}) end
            local base_accesskit=table.clone(fixed_accesskit)
            for _,identity in ipairs(selected_accesskit.values.syslinks) do
                if identity=="accesskit" or identity=="ntdll" or identity=="oleaut32" or identity=="user32" then table.insert(fixed_accesskit,gcc.nf_syslink(chain_driver,identity)) end
            end
            local _,accesskit_argv=gcc.linkargv(chain_driver,{path.join(accesskit,"main.o")},"binary",path.join(accesskit,"fixed.elf"),fixed_accesskit,{rawargs=true})
            os.iorunv(chain_compiler.program,accesskit_argv,{timeout=30000})
            check(os.isfile(path.join(accesskit,"fixed.elf")),"Production GNU AccessKit ordering must resolve all later import archives")
            local _,accesskit_bad=gcc.linkargv(chain_driver,{path.join(accesskit,"main.o")},"binary",path.join(accesskit,"old.elf"),table.join(base_accesskit,{"-loleaut32","-luser32","-lntdll","-laccesskit"}),{rawargs=true})
            local accesskit_rejected,accesskit_reason=false,""
            try {function() os.iorunv(chain_compiler.program,accesskit_bad,{timeout=30000}) end,catch {function(errors) accesskit_rejected=true; accesskit_reason=tostring(errors) end}}
            check(accesskit_rejected and accesskit_reason:find("NtWriteFile",1,true) and accesskit_reason:find("Ole_probe",1,true) and accesskit_reason:find("User_probe",1,true),"Original dependencies-before-AccessKit order must fail all three back-reference controls")
            io.writefile(path.join(accesskit,"negative-control.log"),accesskit_reason)
            import("core.base.json").savefile(path.join(accesskit,"receipt.json"),{passed=true,requested_platform="windows",use_mingw=true,compiler=chain_compiler.program,argv=accesskit_argv,old_order_rejected=true,boundary=os.host()=="linux" and "Actual GCC GNU bfd ELF link; real MinGW CI still required" or "Clang ELF lld strict back-reference check; real MinGW CI still required"})
            local cycle=path.join(directory,"GNU cyclic engine archives")
            os.mkdir(cycle)
            local objects={}
            local cycle_bodies={entry="extern int test_main(void); int engine_entry(void) { return test_main(); }",helper="int engine_helper(void) { return 42; }",tests="extern int engine_helper(void); int test_main(void) { return engine_helper(); }",main="extern int engine_entry(void); int probe_entry(void) { return engine_entry()!=42; }"}
            for _,name in ipairs({"entry","helper","tests","main"}) do
                local source,object=path.join(cycle,name..".c"),path.join(cycle,name..".o")
                io.writefile(source,cycle_bodies[name].."\n")
                os.iorunv(chain_compiler.program,table.join(chain_common,{"-x","c","-fPIC","-c",source,"-o",object}),{timeout=30000})
                objects[name]=object
            end
            os.iorunv(ar.program,{"rcs",path.join(cycle,"libegp_archive_main.a"),objects.entry,objects.helper},{timeout=30000})
            os.iorunv(ar.program,{"rcs",path.join(cycle,"libegp_archive_tests.a"),objects.tests},{timeout=30000})
            local captured={}
            function captured:add(key,values,extras) self[key]=values; self[key.."_extras"]=extras end
            local linking=import("build.xmake.linking",{rootdir=root})
            linking.archive_group(captured,{LIBS={{kind="target",name="main"},{kind="target",name="tests"}}},{platform="windows",use_mingw=true})
            check(captured.ldflags and captured.shflags,"Production MinGW policy must group cyclic engine archives")
            check(captured.ldflags_extras.expand==false and captured.shflags_extras.expand==false,"Production GNU rescan flags must remain atomic")
            local mingw_driver={program=chain_driver.program,is_plat=function(_,...) return table.contains({...},"mingw") end}
            local native_group=gcc.nf_linkgroup(mingw_driver,{"egp_archive_main","egp_archive_tests"},{extra={group=true}})
            check(not table.contains(native_group,"-Wl,--start-group"),"Installed xmake MinGW group omission must remain the negative control")
            local flags=table.join(chain_common,{"-nostdlib","-Wl,-e,probe_entry","-L"..cycle,os.host()=="linux" and "-fuse-ld=bfd" or "-fuse-ld=lld"})
            if os.host()=="windows" then table.join2(flags,{"-Wl,--warn-backrefs","-Wl,--fatal-warnings"}) end
            for _,kind in ipairs({"binary","shared"}) do
                local selected=kind=="binary" and captured.ldflags or captured.shflags
                local output=path.join(cycle,kind..".elf")
                local kind_flags=kind=="shared" and {"-shared"} or {}
                local _,argv=gcc.linkargv(chain_driver,{objects.main},kind,output,table.join(flags,selected,kind_flags,{"-Wl,--no-undefined"}),{rawargs=true})
                os.iorunv(chain_compiler.program,argv,{timeout=30000})
                check(os.isfile(output),"Production GNU grouping must resolve the real engine/test archive cycle for "..kind)
                if kind=="shared" then
                    check(table.contains(argv,"-shared"),"Actual shared-library proof must retain the native shared-driver option")
                    local bytes=assert(io.readfile(output,{encoding="binary"}))
                    check(bytes:sub(1,4)=="\127ELF" and bytes:byte(6)==1 and bytes:byte(17)+256*bytes:byte(18)==3,"Actual shared-library output must be ELF ET_DYN")
                end
                import("core.base.json").savefile(path.join(cycle,kind.."-receipt.json"),{passed=true,argv=argv,compiler=chain_compiler.program})
            end
            local _,old=gcc.linkargv(chain_driver,{objects.main},"binary",path.join(cycle,"old.elf"),table.join(flags,native_group),{rawargs=true})
            local rejected,reason=false,""
            try {function() os.iorunv(chain_compiler.program,old,{timeout=30000}) end,catch {function(errors) rejected=true; reason=tostring(errors) end}}
            check(rejected and reason:find("engine_helper",1,true),"Original ungrouped MinGW mapper must fail the real cyclic archive control")
            io.writefile(path.join(cycle,"negative-control.log"),reason)
            if os.host()=="linux" then
                -- Exercise the target flag collector as well as the raw mapper:
                -- the production atomic group must survive real xmake argv generation.
                target:add("linkdirs",cycle)
                for _,flag in ipairs(flags) do if not flag:startswith("-L") then target:add("ldflags",flag,{force=true}) end end
                linking.archive_group(target,{LIBS={{kind="target",name="main"},{kind="target",name="tests"}}},{platform="windows",use_mingw=true})
                local linker=assert(import("core.tool.linker").load("binary",{"cc"},{target=target}))
                local output=path.join(cycle,"native-target.elf")
                local program,argv=linker:linkargv({objects.main},output,{target=target})
                check(table.contains(argv,"-Wl,--start-group") and table.contains(argv,"-Wl,--end-group"),"Actual native target must preserve the atomic production archive group")
                os.iorunv(program,argv,{envs=linker:runenvs(),timeout=30000})
                check(os.isfile(output),"Actual native target must link the real engine/test cycle")
                import("core.base.json").savefile(path.join(cycle,"native-target-receipt.json"),{passed=true,program=program,argv=argv})
            end
        end
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
