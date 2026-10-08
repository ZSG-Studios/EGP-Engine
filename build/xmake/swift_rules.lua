-- The stock per-file Swift rule cannot resolve this engine's cross-file Swift declarations.
rule("egp.apple.swift")
    set_sourcekinds("sc")
    add_orders("egp.apple.swift", "c++.build")
    add_orders("egp.apple.swift", "c.build")
    add_orders("egp.apple.swift", "objc++.build")
    add_orders("egp.apple.swift", "objc.build")
    add_orders("egp.apple.swift", "swift.build")
    add_orders("egp.apple.swift", "swift.interop")
    on_config(function(target)
        target:rule_enable("swift.build", false)
        target:rule_enable("swift.interop", false)
        local batch = assert(target:sourcebatches()["egp.apple.swift"], "Swift policy has no native source batch")
        local settings = assert(target:data("egp.apple.swift.settings"))
        local directory = target:autogendir("egp.apple.swift")
        local object = path.join(directory, settings.module .. ".o")
        local header = path.join(directory, settings.header)
        for name, other in pairs(target:sourcebatches()) do
            if name ~= "egp.apple.swift" and other.sourcekind == "sc" then other.objectfiles = {} end
        end
        batch.objectfiles = {object}
        target:data_set("egp.apple.swift.object", object)
        target:data_set("egp.apple.swift.header", header)
    end)
    on_prepare_files(function(target, jobgraph, sourcebatch, opt)
        jobgraph:add(target:fullname() .. "/swift_module", function(index, total, jobopt)
            import("build.xmake.platforms.swift", {rootdir = os.projectdir()}).prepare(target, sourcebatch, jobopt)
        end)
    end, {jobgraph = true})
    on_build_files(function(target, jobgraph, sourcebatch, opt)
        -- Preparation creates the single WMO object before dependent Objective-C++ jobs.
    end, {jobgraph = true, batch = true})
rule_end()
