function gcc_warning(name,version)
    if name~="gcc" and name~="gxx" then return nil end
    local major=assert(tonumber(tostring(version):match("^(%d+)")),"Could not identify the selected GCC compiler version")
    -- Preserve upstream GH-119269: GCC 16 diagnoses intentional incomplete-type SFINAE.
    return major>=16 and "-Wno-sfinae-incomplete" or nil
end

function apply(target)
    local compiler=assert(import("core.tool.compiler").load("cxx",{target=target}))
    local name=compiler:name()
    if name~="gcc" and name~="gxx" then return name end
    local tool=assert(import("lib.detect.find_tool")(name,{program=compiler:program(),version=true,envs=compiler:runenvs()}),"The selected GCC compiler is unavailable")
    local warning=gcc_warning(name,tool.version)
    if warning and not table.contains(table.wrap(target:get("cxxflags")),warning) then
        target:add("cxxflags",warning,{force=true})
    end
    return name,tool.version
end

function configure(target)
    local previous=target:script("config")
    target:set("config",function(current,options)
        if previous then previous(current,options) end
        apply(current)
    end)
end
