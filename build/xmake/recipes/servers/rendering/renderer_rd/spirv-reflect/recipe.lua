-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_thirdparty, thirdparty_dir, thirdparty_sources
    env = graph:use("env")
    thirdparty_dir = "#thirdparty/spirv-reflect/"
    thirdparty_sources = {"spirv_reflect.c"}
    thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
    env_thirdparty = env:clone()
    env_thirdparty:disable_warnings()
    env_thirdparty:sources(env.servers_sources, thirdparty_sources)
end
