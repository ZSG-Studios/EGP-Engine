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
            print("NATIVE_TOOLCHAIN_SELECTION_CHECKS=" .. (#selections*2+17+apple_checks))
        end)
    target_end()
    end
end
