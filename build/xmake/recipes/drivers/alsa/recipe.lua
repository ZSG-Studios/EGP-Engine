-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env
    env = graph:use("env")
    if R.truthy((function() local v = (R.contains(env, "alsa")); if not R.truthy(v) then return v end; return R.index(env, "alsa") end)()) then
        if R.truthy(R.index(env, "use_sowrap")) then
            env:sources(env.drivers_sources, "asound-so_wrap.c")
        end
    end
    env:sources(env.drivers_sources, "*.cpp")
end
