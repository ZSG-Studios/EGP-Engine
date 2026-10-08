-- Xmake 3.1.1's native linker consumes target:data("linkdepfiles") in
-- private/action/build/target.get_linkdepfiles before depend.on_changed.
function files(graph, program, root)
    local found={}
    local targetfile=path.join('bin',program.filename):gsub('\\','/')
    for _, dependency in ipairs(graph.dependencies or {}) do
        local matches=false
        for _, target in ipairs(dependency.targets) do
            if target.kind=='file' and target.path:gsub('\\','/')==targetfile then matches=true end
        end
        if matches then
            for _, input in ipairs(dependency.inputs) do
                if input.kind=='file' then found[path.absolute(input.path,root)]=true end
            end
        end
    end
    if graph.options.platform=='web' and program.filename:endswith('.js') then
        -- Packaging runs after linking; wrapper/template changes must invalidate
        -- the link job too, so after_link regenerates the complete template ZIP.
        for _, folder in ipairs({'platform/web/js/engine','platform/web/js/libs','misc/dist/html'}) do
            for _, filename in ipairs(os.files(path.join(root,folder,'**'))) do found[filename]=true end
        end
        for _, filename in ipairs({'misc/logo/icon.png','thirdparty/fonts/Inter_Regular.woff2','thirdparty/fonts/Inter_Bold.woff2'}) do
            found[path.join(root,filename)]=true
        end
    end
    local result=table.keys(found); table.sort(result)
    return result
end

function configure(target, graph, program, root)
    local result=table.clone(target:data('linkdepfiles') or {})
    for _, filename in ipairs(files(graph,program,root)) do
        if not table.contains(result,filename) then table.insert(result,filename) end
    end
    table.sort(result)
    target:data_set('linkdepfiles',result)
end
