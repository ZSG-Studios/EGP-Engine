-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, lib, print_error, supported
    print_error = graph:methods().print_error
    env = graph:use("env")
    env.drivers_sources = {}
    supported = env:get("supported", {})
    graph:include("unix/recipe.lua")
    graph:include("windows/recipe.lua")
    graph:include("alsa/recipe.lua")
    graph:include("pulseaudio/recipe.lua")
    if R.truthy(((R.index(env, "platform") == "windows"))) then
        graph:include("wasapi/recipe.lua")
        if R.truthy(not R.truthy(env.msvc)) then
            graph:include("backtrace/recipe.lua")
        end
    end
    if R.truthy(R.index(env, "xaudio2")) then
        if R.truthy((not R.contains(supported, "xaudio2"))) then
            graph:message("print_error", R.format("Target platform '{}' does not support the XAudio2 audio driver", R.index(env, "platform")))
            graph:abort(255)
        end
        graph:include("xaudio2/recipe.lua")
    end
    if R.truthy((R.contains({"macos", "ios", "visionos"}, R.index(env, "platform")))) then
        graph:include("apple/recipe.lua")
        graph:include("coreaudio/recipe.lua")
    end
    if R.truthy((R.contains({"ios", "visionos"}, R.index(env, "platform")))) then
        graph:include("apple_embedded/recipe.lua")
    end
    if R.truthy((function() local v = R.index(env, "accesskit"); if not R.truthy(v) then return v end; return (R.contains({"macos", "windows", "linuxbsd", "android", "ios"}, R.index(env, "platform"))) end)()) then
        graph:include("accesskit/recipe.lua")
    end
    graph:include("alsamidi/recipe.lua")
    if R.truthy((R.contains({"macos"}, R.index(env, "platform")))) then
        graph:include("coremidi/recipe.lua")
    end
    graph:include("winmidi/recipe.lua")
    if R.truthy(R.index(env, "rendering_device")) then
        if R.truthy(R.index(env, "vulkan")) then
            graph:include("vulkan/recipe.lua")
        end
        if R.truthy(R.index(env, "d3d12")) then
            if R.truthy((not R.contains(supported, "d3d12"))) then
                graph:message("print_error", R.format("Target platform '{}' does not support the D3D12 rendering driver", R.index(env, "platform")))
                graph:abort(255)
            end
            graph:include("d3d12/recipe.lua")
        end
        if R.truthy(R.index(env, "metal")) then
            if R.truthy((not R.contains(supported, "metal"))) then
                graph:message("print_error", R.format("Target platform '{}' does not support the Metal rendering driver", R.index(env, "platform")))
                graph:abort(255)
            end
            graph:include("metal/recipe.lua")
        end
    end
    if R.truthy(R.index(env, "opengl3")) then
        graph:include("gl_context/recipe.lua")
        graph:include("gles3/recipe.lua")
        graph:include("egl/recipe.lua")
    end
    if R.truthy((function() local v = R.index(env, "sdl"); if not R.truthy(v) then return v end; return (R.contains({"linuxbsd", "macos", "windows", "ios", "visionos"}, R.index(env, "platform"))) end)()) then
        graph:include("sdl/recipe.lua")
    end
    graph:include("png/recipe.lua")
    env:sources(env.drivers_sources, "*.cpp")
    lib = env:library("drivers", env.drivers_sources)
    env:prepend({["LIBS"] = {lib}})
end
