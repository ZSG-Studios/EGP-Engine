-- Private RTC C++23 products use Clang while the engine may retain MSVC.
-- Both use the engine-selected Microsoft ABI, SDK/toolset and CRT.
function configure(target, options)
    local config=import("core.project.config")
    local settings={llvm=true}
    local toolset=(options and options.msvc_version) or config.get("msvc_version")
    local sdk=(options and options.mssdk_version) or config.get("mssdk_version")
    if toolset and toolset~="" and toolset~="auto" then settings.vs_toolset=toolset end
    if sdk and sdk~="" and sdk~="auto" then settings.vs_sdkver=sdk end
    target:set("toolchains","clang-cl",settings)
    -- C++ standards are explicit /clang:-std=c++23 on private RTC sources.
    -- Retain the C provider language without inheriting the engine C++17 flag.
    target:set("languages","c17")
    target:set("toolset","ar","link@llvm-lib")
    -- Match the existing engine Clang policy for its portable r128 header.
    target:add("defines","R128_STDC_ONLY")
    local previous=target:script("config")
    target:set("config",function(current, config_options)
        local toolchain=assert(current:toolchain("clang-cl"))
        assert(toolchain:check(),"Embedded RTC Clang toolchain unavailable")
        local compiler=assert(current:tool("cxx"))
        local version=os.iorunv(compiler,{"--version"})
        assert(version:match("clang version ([^%s]+)")=="22.1.3","Embedded RTC requires pinned Clang 22.1.3")
        local librarian=assert(import("lib.detect.find_program")("llvm-lib",{envs=toolchain:runenvs()}),"Embedded RTC requires LLVM librarian")
        current:set("toolset","ar","link@"..librarian)
        if previous then previous(current,config_options) end
    end)
end
