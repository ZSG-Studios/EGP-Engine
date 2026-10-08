set_xmakever("3.1.1")
set_policy("check.auto_ignore_flags", false)

for _, compiler_name in ipairs({"msvc", "clang-cl"}) do
    target("stack_" .. compiler_name:gsub("%-", "_"))
        set_kind("binary")
        on_load(function(target)
            import("build.xmake.platforms.init", {rootdir=path.absolute("../../../..", os.scriptdir())}).configure(target, {
                platform="windows", arch="x86_64", use_llvm=compiler_name=="clang-cl", optimize="none", warnings="no", werror=true,
                windows_subsystem="console", debug_symbols=false, accesskit=false, angle=false, d3d12=false})
        end)
        on_config(function(target)
            local root=path.absolute("../../../..", os.scriptdir())
            local policy=import("build.xmake.platforms.init", {rootdir=root})
            local directory=path.absolute(import("core.project.config").builddir(), os.projectdir())
            os.mkdir(directory)
            local checks=0
            local function check(value, message) assert(value, message); checks=checks+1 end
            if compiler_name=="msvc" then
                for _, profile in ipairs({{}, {use_llvm=true}, {use_mingw=true}, {use_mingw=true, use_llvm=true}}) do
                    for _, asan in ipairs({false, true}) do
                        local probe={values={}}
                        function probe:set(key, ...) self.values[key]={...} end
                        function probe:add(key, ...)
                            self.values[key]=self.values[key] or {}
                            for _, value in ipairs({...}) do if type(value)~="table" then table.insert(self.values[key], value) end end
                        end
                        local options=table.join(profile, {platform="windows", arch="x86_64", use_asan=asan, accesskit=false, angle=false, d3d12=false})
                        if profile.use_mingw and not profile.use_llvm and asan then
                            local ok=utils.trycall(function() policy.configure(probe, options); return true end)
                            check(not ok, "GCC Windows ASAN must be rejected before emitting an unsupported stack profile")
                        else
                            policy.configure(probe, options)
                            local expected=(profile.use_mingw and "-Wl,--stack," or "/STACK:") .. (asan and 30 * 1024 * 1024 or 8 * 1024 * 1024)
                            for _, key in ipairs({"ldflags", "shflags"}) do
                                check(table.contains(probe.values[key], expected), "Supported MSVC, clang-cl and MinGW profiles must preserve upstream stack reserves")
                            end
                        end
                    end
                end
            end
            local source=path.join(directory, "deep-stack.cpp")
            io.writefile(source, [[#include <windows.h>
#include <stdio.h>
__declspec(noinline) int deep_stack(int depth) {
    volatile unsigned char pages[4096] = {};
    pages[0] = (unsigned char)depth;
    int result = depth ? deep_stack(depth - 1) : 17;
    return result + pages[0] * 0;
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (deep_stack(512) != 17) return 1;
    puts("NATIVE_DEEP_STACK_PASS");
    return 0;
}
]])
            local object=path.join(directory, compiler_name .. ".obj")
            local compiler=assert(import("core.tool.compiler").load("cxx", {target=target}))
            local compile_program, compile_arguments=compiler:compargv(source, object, {target=target})
            os.vrunv(compile_program, compile_arguments, {envs=compiler:runenvs(), timeout=60000})
            check(os.isfile(object), "The installed native compiler must produce the stack probe object")
            local linker=assert(import("core.tool.linker").load("binary", {"cxx"}, {target=target}))
            local executable=path.join(directory, compiler_name .. ".exe")
            local program, arguments=linker:linkargv({object}, executable, {target=target})
            check(table.contains(arguments, "/STACK:8388608"), "The actual installed Windows linker must receive the graph-derived engine stack reserve")
            os.vrunv(program, arguments, {envs=linker:runenvs(), timeout=60000})
            local function reserve(filename)
                local bytes=assert(io.readfile(filename, {encoding="binary"}))
                local function u32(offset)
                    local value=0
                    for index=0,3 do value=value+assert(bytes:byte(offset+index+1))*256^index end
                    return value
                end
                local pe=u32(0x3c)
                assert(bytes:sub(pe+1, pe+4)=="PE\0\0")
                return u32(pe+24+72)
            end
            check(reserve(executable)==8388608, "The linked PE header must reserve the intended eight MiB")
            local stdout=os.iorunv(executable, {}, {timeout=30000})
            check(stdout:find("NATIVE_DEEP_STACK_PASS", 1, true), "The restored stack must execute a bounded native call chain larger than the old one MiB default")
            local previous=path.join(directory, compiler_name .. "-old.exe")
            local _, previous_arguments=linker:linkargv({object}, previous, {target=target})
            local old={}
            for _, argument in ipairs(previous_arguments) do if not argument:match("^/STACK:") then table.insert(old, argument) end end
            os.vrunv(program, old, {envs=linker:runenvs(), timeout=60000})
            check(reserve(previous)==1048576, "Removing only the restored linker policy must reproduce the original one MiB PE reserve")
            import("core.base.json").savefile(path.join(directory, compiler_name .. "-stack.json"), {
                compiler=compile_program, linker=program, arguments=arguments, fixed_stack_reserve=reserve(executable),
                previous_stack_reserve=reserve(previous), deep_stack_runtime="PASS", checks=checks})
            print("NATIVE_WINDOWS_STACK_CHECKS=" .. checks)
        end)
    target_end()
end
