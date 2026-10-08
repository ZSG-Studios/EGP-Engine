-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local driver_obj, env, env_backtrace, env_thirdparty, thirdparty_dir, thirdparty_obj, thirdparty_sources
    env = graph:use("env")
    env_backtrace = env:clone()
    thirdparty_obj = {}
    thirdparty_dir = "#thirdparty/libbacktrace/"
    thirdparty_sources = {"atomic.c", "dwarf.c", "fileline.c", "posix.c", "print.c", "sort.c", "state.c", "backtrace.c", "simple.c", "pecoff.c", "read.c", "alloc.c"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_backtrace:prepend({["CPPPATH"] = {thirdparty_dir}})
    env_thirdparty = env_backtrace:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
    env.drivers_sources = R.iadd(env.drivers_sources, thirdparty_obj)
    driver_obj = {}
    env_backtrace:sources(driver_obj, "*.cpp")
    env.drivers_sources = R.iadd(env.drivers_sources, driver_obj)
    env:depends(driver_obj, thirdparty_obj)
end
