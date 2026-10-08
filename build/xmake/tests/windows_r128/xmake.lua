set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags", false)

for _, compiler_name in ipairs({"msvc", "clang-cl"}) do
    target("r128_" .. compiler_name:gsub("%-", "_"))
        set_kind("object")
        add_files("../../../../thirdparty/misc/r128.c")
        on_load(function(target)
            import("build.xmake.platforms.init", {rootdir=path.absolute("../../../..", os.scriptdir())}).configure(target, {
                platform="windows", arch="x86_64", use_llvm=compiler_name=="clang-cl", warnings="no", werror=true,
                debug_symbols=false, accesskit=false, angle=false, d3d12=false})
        end)
        on_config(function(target)
            local root=path.absolute("../../../..", os.scriptdir())
            local policy=import("build.xmake.platforms.init", {rootdir=root})
            local checks=0
            local function check(value, message)
                assert(value, message)
                checks=checks+1
            end
            if compiler_name=="msvc" then
                for _, profile in ipairs({
                    {platform="windows", expected=false},
                    {platform="windows", use_llvm=true, expected=true},
                    {platform="windows", use_mingw=true, use_llvm=true, expected=false},
                    {platform="linuxbsd", use_llvm=true, expected=false}
                }) do
                    local probe={values={}}
                    function probe:set(key, ...) self.values[key]={...} end
                    function probe:add(key, ...)
                        self.values[key]=self.values[key] or {}
                        for _, value in ipairs({...}) do
                            if type(value)~="table" then table.insert(self.values[key], value) end
                        end
                    end
                    profile.arch="x86_64"
                    profile.accesskit, profile.angle, profile.d3d12=false, false, false
                    policy.configure(probe, profile)
                    check(table.contains(probe.values.defines, "R128_STDC_ONLY")==profile.expected,
                        "Only the Windows clang-cl platform policy may select portable r128 arithmetic")
                end
            end
            check(table.wrap(target:get("toolchains"))[1]==compiler_name, "The real target must select the requested Windows compiler")
            check(table.contains(table.wrap(target:get("defines")), "R128_STDC_ONLY")== (compiler_name=="clang-cl"),
                "The production platform policy must preserve MSVC intrinsics and clang-cl portable arithmetic")
            local compiler=assert(import("core.tool.compiler").load("cc", {target=target}))
            local source=path.join(root, "thirdparty/misc/r128.c")
            local directory=path.absolute(import("core.project.config").builddir(), os.projectdir())
            local object=path.join(directory, compiler_name .. ".obj")
            os.mkdir(directory)
            local program, arguments=compiler:compargv(source, object, {target=target})
            local portable, original_arguments=0, {}
            for _, argument in ipairs(arguments) do
                if argument:match("^[/-]DR128_STDC_ONLY$") then portable=portable+1
                else table.insert(original_arguments, argument) end
            end
            check(portable== (compiler_name=="clang-cl" and 1 or 0), "The installed C driver must receive the exact graph-derived r128 definition")
            os.vrunv(program, arguments, {envs=compiler:runenvs(), timeout=60000})
            check(os.isfile(object), "The actual bundled r128 C implementation must compile with the restored policy")
            if compiler_name=="clang-cl" then
                os.tryrm(object)
                local ok, failure=utils.trycall(function()
                    os.iorunv(program, original_arguments, {envs=compiler:runenvs(), timeout=60000})
                end)
                check(not ok and tostring(failure):find("_udiv128", 1, true),
                    "Removing only the restored definition must reproduce the original unavailable-intrinsic compile failure")
                io.writefile(path.join(directory, "clang-cl-original-negative.txt"), tostring(failure))
            end
            import("core.base.json").savefile(path.join(directory, compiler_name .. "-argv.json"), {
                compiler=program, arguments=arguments, checks=checks, status="PASS"})
            print("NATIVE_WINDOWS_R128_CHECKS=" .. checks)
        end)
    target_end()
end
