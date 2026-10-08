-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local annotations, default_perfetto_install_dir, default_perfetto_path, env, env_perfetto, env_tracy, find_perfetto_path, find_tracy_path, pathlib, profiler_path, profiling_builders
    pathlib = R.pathlib
    profiling_builders = graph:builders("profiling_builders")
    env = graph:use("env")
    env:sources(env.core_sources, "*.cpp")
    default_perfetto_install_dir = "../../thirdparty/perfetto"
    find_perfetto_path = function(path)
        if R.truthy(not R.truthy(path:is_dir())) then
            print(R.join({"Perfetto profiler path '", R.str(path:absolute()), "' is invalid."}, ""))
            graph:abort(255)
        end
        if R.truthy(R.div(R.div(path, "sdk"), "perfetto.cc"):is_file()) then
            return R.div(path, "sdk")
        end
        if R.truthy(R.div(path, "perfetto.cc"):is_file()) then
            return path
        end
        print("Invalid perfetto profiler path. Unable to find perfetto.cc.")
        graph:abort(255)
    end
    find_tracy_path = function(path)
        if R.truthy(not R.truthy(path:is_dir())) then
            print("profiler_path must point to a directory.")
            graph:abort(255)
        end
        if R.truthy(R.div(R.div(path, "public"), "TracyClient.cpp"):is_file()) then
            return R.div(path, "public")
        end
        if R.truthy(R.div(path, "TracyClient.cpp"):is_file()) then
            return path
        end
        print("Invalid profiler_path. Unable to find TracyClient.cpp.")
        graph:abort(255)
    end
    if R.truthy(R.index(env, "profiler")) then
        if R.truthy(((R.index(env, "profiler") == "instruments"))) then
            if R.truthy(R.index(env, "profiler_sample_callstack")) then
                print("profiler_sample_callstack ignored. Please configure callstack sampling in Instruments instead.")
            end
            if R.truthy(R.index(env, "profiler_track_memory")) then
                print("profiler_track_memory ignored. Please configure memory tracking in Instruments instead.")
            end
            if R.truthy(R.index(env, "profiler_record_on_demand")) then
                print("profiler_record_on_demand ignored. Instruments is always recording.")
            end
            if R.truthy(R.index(env, "profiler_broadcast_address")) then
                print("profiler_broadcast_address ignored. Instruments does not support broadcast address configuration.")
            end
        else
            if R.truthy(((R.index(env, "profiler") == "tracy"))) then
                if R.truthy(not R.truthy(R.index(env, "profiler_path"))) then
                    print("profiler_path must be set when using the tracy profiler. Aborting.")
                    graph:abort(255)
                end
                profiler_path = find_tracy_path(pathlib.Path(R.index(env, "profiler_path")))
                env:prepend({["CPPPATH"] = {R.str(profiler_path:absolute())}})
                env_tracy = env:clone()
                env_tracy:add({["CPPDEFINES"] = {"TRACY_ENABLE"}})
                if R.truthy(R.index(env, "profiler_sample_callstack")) then
                    if R.truthy((not R.contains({"windows", "linuxbsd", "android"}, R.index(env, "platform")))) then
                        print("Tracy does not support call stack sampling on this platform. Aborting.")
                        graph:abort(255)
                    end
                    env_tracy:add({["CPPDEFINES"] = {{"TRACY_CALLSTACK", 62}}})
                end
                if R.truthy(R.index(env, "profiler_track_memory")) then
                    env_tracy:add({["CPPDEFINES"] = {"GODOT_PROFILER_TRACK_MEMORY"}})
                end
                if R.truthy(R.index(env, "profiler_record_on_demand")) then
                    env_tracy:add({["CPPDEFINES"] = {"TRACY_ON_DEMAND"}})
                end
                if R.truthy(R.index(env, "profiler_broadcast_address")) then
                    env_tracy:add({["CPPDEFINES"] = {{"TRACY_CLIENT_ADDRESS", R.add(R.add("\"", R.index(env, "profiler_broadcast_address")), "\"")}}})
                end
                env_tracy:disable_warnings()
                env_tracy:sources(env.core_sources, R.str(R.div(profiler_path, "TracyClient.cpp"):absolute()))
            else
                if R.truthy(((R.index(env, "profiler") == "perfetto"))) then
                    if R.truthy(R.index(env, "profiler_path")) then
                        profiler_path = find_perfetto_path(pathlib.Path(R.index(env, "profiler_path")))
                    else
                        if R.truthy((function() default_perfetto_path = pathlib.Path(default_perfetto_install_dir); return default_perfetto_path end)():is_dir()) then
                            profiler_path = find_perfetto_path(default_perfetto_path)
                        else
                            print("Perfetto must be installed or profiler_path must be set when using the perfetto profiler. Aborting.")
                            graph:abort(255)
                        end
                    end
                    env:prepend({["CPPPATH"] = {R.str(profiler_path:absolute())}})
                    env_perfetto = env:clone()
                    if R.truthy(R.index(env, "profiler_sample_callstack")) then
                        print("Perfetto does not support call stack sampling. Aborting.")
                        graph:abort(255)
                    end
                    if R.truthy(R.index(env, "profiler_track_memory")) then
                        print("Perfetto does not support memory tracking. Aborting.")
                        graph:abort(255)
                    end
                    if R.truthy(R.index(env, "profiler_record_on_demand")) then
                        print("profiler_record_on_demand ignored. Perfetto is always recording.")
                    end
                    if R.truthy(R.index(env, "profiler_broadcast_address")) then
                        print("profiler_broadcast_address ignored. Perfetto does not support broadcast address configuration.")
                    end
                    env_perfetto:disable_warnings()
                    env_perfetto:prepend({["CPPPATH"] = {R.str(profiler_path:absolute())}})
                    env_perfetto:sources(env.core_sources, R.str(R.div(profiler_path, "perfetto.cc"):absolute()))
                end
            end
        end
    else
        if R.truthy(R.index(env, "profiler_path")) then
            print("profiler is required if profiler_path is set. Aborting.")
            graph:abort(255)
        end
    end
    env:generate("profiling.gen.h", {env:value(R.index(env, "profiler")), env:value(R.index(env, "profiler_sample_callstack")), env:value(R.index(env, "profiler_track_memory")), env:value(R.index(env, "profiler_record_on_demand"))}, env:generator(profiling_builders.profiler_gen_builder))
end
