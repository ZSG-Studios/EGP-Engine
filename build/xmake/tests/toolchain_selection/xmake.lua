set_xmakever("3.1.1")
includes("../../platforms/visionos.lua")

local selections = {
    {platform="windows",name="msvc"},
    {platform="windows",use_llvm=true,name="clang-cl"},
    {platform="windows",use_mingw=true,name="mingw"},
    {platform="linuxbsd",name="gcc"},
    {platform="linuxbsd",use_llvm=true,name="clang"},
    {platform="macos",name="xcode"},
    {platform="ios",name="xcode"},
    {platform="ios",simulator="y",name="xcode"},
    {platform="android",name="ndk"},
    {platform="web",name="emcc"},
    {platform="visionos",simulator="n",name="egp-visionos"},
    {platform="visionos",simulator="y",name="egp-visionos"}
}

for index, options in ipairs(selections) do
    if (os.host()=="windows" and index==1) or (os.host()=="linux" and index==4) or (os.host()=="macosx" and index==6) then
    target("toolchain_" .. index)
        set_kind("phony")
        on_load(function(target)
            import("build.xmake.platforms.init", {rootdir=path.absolute("../../../..", os.scriptdir())}).configure_toolchain(target, options)
        end)
        on_config(function(target)
            assert(table.wrap(target:get("toolchains"))[1]==options.name, "Target ignored requested compiler")
            assert(target:extraconf("toolchains", options.name, "simulator")==nil, "Native host compiler must not carry unrelated simulator settings")
            local policy=import("build.xmake.platforms.init", {rootdir=path.absolute("../../../..", os.scriptdir())})
            for _, profile in ipairs(selections) do
                local probe={}
                function probe:set(key, name, extras) self.key,self.name,self.extras=key,name,extras end
                policy.configure_toolchain(probe,profile)
                assert(probe.key=="toolchains" and probe.name==profile.name)
                local expected_simulator
                if profile.platform=="ios" or profile.platform=="visionos" then expected_simulator=profile.simulator=="y" end
                assert(probe.extras.simulator==expected_simulator)
                if profile.platform=="ios" then assert(probe.extras.appledev==(profile.simulator=="y" and "simulator" or "iphone") and probe.extras.target_minver=="15.0") end
            end
            local apple_checks=0
            local toolchain_utils=import("private.utils.toolchain")
            for _, profile in ipairs({
                {arch="arm64",minimum="13.0"},
                {arch="x86_64",minimum="11.0"},
                {arch="aarch64",minimum="13.0"},
                {arch="amd64",minimum="11.0"}
            }) do
                local options={platform="macos",arch=profile.arch,accesskit=false,metal=false,vulkan=false,angle=false}
                local probe={values={}}
                function probe:set(key,...)
                    self.values[key]={...}
                    if key=="toolchains" then self.name,self.extras=... end
                end
                function probe:add(key,...)
                    self.values[key]=self.values[key] or {}
                    for _,value in ipairs({...}) do if type(value)~="table" then table.insert(self.values[key],value) end end
                end
                policy.configure(probe,options)
                assert(probe.extras.target_minver==profile.minimum,"macOS Xcode deployment target must not default to the SDK version")
                apple_checks=apple_checks+1
                local normalized=policy.normalize(options)
                local compiler={}
                function compiler:arch() return normalized.arch end
                function compiler:plat() return normalized.plat end
                function compiler:config(key) return probe.extras[key] end
                local triple=toolchain_utils.get_xcode_target_triple(compiler)
                assert(triple==normalized.arch .. "-apple-macos" .. profile.minimum,"Xcode's real compiler target triple must preserve macOS compatibility")
                apple_checks=apple_checks+1
                for _,key in ipairs({"cxflags","ldflags","shflags"}) do
                    assert(table.contains(probe.values[key],"-mmacosx-version-min=" .. profile.minimum),"macOS compiler and linker deployment flags must agree with the target triple")
                    apple_checks=apple_checks+1
                end
                print("NATIVE_MACOS_DEPLOYMENT_TARGET=" .. triple)
            end
            local clang_checks=0
            local clang_probe={values={}}
            function clang_probe:set(key,...) self.values[key]={...} end
            function clang_probe:add(key,...)
                self.values[key]=self.values[key] or {}
                for _,value in ipairs({...}) do if type(value)~="table" then table.insert(self.values[key],value) end end
            end
            policy.configure(clang_probe,{platform="windows",arch="x86_64",use_llvm=true,werror=true,accesskit=false,angle=false,d3d12=false})
            assert(table.contains(clang_probe.values.cxflags,"/clang:-ffp-contract=off"),"Clang-cl must receive floating-point contraction policy through its native passthrough")
            clang_checks=clang_checks+1
            assert(not table.contains(clang_probe.values.cxflags,"-ffp-contract=off"),"Clang-cl must not receive an unsupported GNU driver argument")
            clang_checks=clang_checks+1
            if os.host()=="windows" then
                local directories={"C:/Program Files/LLVM/bin"}
                for _, instance in ipairs(os.dirs("C:/Program Files/Microsoft Visual Studio/*/*")) do
                    table.insert(directories,path.join(instance,"VC/Tools/Llvm/x64/bin"))
                end
                local compiler=assert(import("lib.detect.find_program")("clang-cl",{paths=directories}),"The Windows native policy contract requires the installed clang-cl compiler")
                local source,object=os.tmpfile() .. ".cpp",os.tmpfile() .. ".obj"
                io.writefile(source,'extern "C" float egp_contract(float a, float b, float c) { return a * b + c; }\n')
                local arguments={"/c","/WX"}
                table.join2(arguments,clang_probe.values.cxflags)
                table.join2(arguments,clang_probe.values.cxxflags or {})
                table.join2(arguments,{source,"-o",object})
                os.vrunv(compiler,arguments,{timeout=60000})
                assert(os.isfile(object),"The actual clang-cl compiler must accept the complete strict Windows platform policy")
                os.tryrm(source); os.tryrm(object)
                clang_checks=clang_checks+1
                print("NATIVE_CLANGCL_PLATFORM_POLICY_COMPILE_PASS")
                local sdk_arguments={"/c","/WX"}
                table.join2(sdk_arguments,clang_probe.values.cxflags)
                table.join2(sdk_arguments,clang_probe.values.cxxflags or {})
                clang_checks=clang_checks+import("build.xmake.tests.windows_com",{rootdir=path.absolute("../../../..",os.scriptdir())}).main(path.absolute("../../../..",os.scriptdir()),compiler,sdk_arguments)
            end
            local hosts=import("build.xmake.platforms.host",{rootdir=path.absolute("../../../..",os.scriptdir())})
            local absent=function() return false end
            assert(hosts.select({platform="windows",use_mingw=true},"windows","x64",absent).plat=="mingw")
            assert(hosts.select({platform="windows",use_llvm=true},"windows","x64",absent).toolchain=="clang-cl")
            assert(hosts.select({platform="windows"},"windows","x64",absent).toolchain=="msvc")
            assert(hosts.select({platform="linuxbsd",use_llvm=true},"linux","x86_64",absent).toolchain=="clang")
            assert(hosts.select({platform="linuxbsd"},"linux","x86_64",absent).toolchain=="gcc")
            assert(hosts.select({platform="web",arch="wasm32"},"windows","x64",function(name) return name=="g++" end).toolchain=="mingw")
            assert(hosts.select({platform="android",arch="arm64"},"linux","x86_64",function(name) return name=="clang++" end).toolchain=="clang")
            assert(hosts.select({platform="web"},"macosx","arm64",absent).toolchain=="xcode")
            for _, pair in ipairs({{"windows","amd64","x64"},{"windows","i386","x86"},{"android","aarch64","arm64-v8a"},{"linuxbsd","x64","x86_64"},{"linuxbsd","riscv64","riscv64"}}) do
                assert(policy.normalize({platform=pair[1],arch=pair[2]}).arch==pair[3])
            end
            local version_policy=import("build.xmake.platforms.compiler_warnings",{rootdir=path.absolute("../../../..",os.scriptdir())})
            assert(version_policy.gcc_warning("gxx","15.2.0")==nil,"GCC before16 must retain its previous warning policy")
            assert(version_policy.gcc_warning("gxx","16.0.0")=="-Wno-sfinae-incomplete","GCC16 must retain the intentional upstream SFINAE exception")
            assert(version_policy.gcc_warning("gcc","17.0.1")=="-Wno-sfinae-incomplete","Newer GCC must retain that same compatibility policy")
            assert(version_policy.gcc_warning("clangxx","22.1.3")==nil,"Clang must not receive a GCC-specific diagnostic option")
            assert(version_policy.gcc_warning("clang_cl","22.1.3")==nil,"Clang-cl must retain its independent strict warning policy")
            local compiler_name,compiler_version=version_policy.apply(target)
            local expected_warning=version_policy.gcc_warning(compiler_name,compiler_version)
            assert(table.contains(table.wrap(target:get("cxxflags")),"-Wno-sfinae-incomplete")== (expected_warning~=nil),"The actual selected native compiler must determine the deferred GCC version flag")
            print("NATIVE_COMPILER_VERSION_WARNING_POLICY=" .. compiler_name .. ":" .. tostring(compiler_version))
            print("NATIVE_TOOLCHAIN_SELECTION_CHECKS=" .. (#selections*2+17+apple_checks+clang_checks+6))
        end)
    target_end()
    end
end
