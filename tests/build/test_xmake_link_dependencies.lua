-- Exercises Xmake's actual native link dependency collector and cache checker.
-- No compiler or linker is launched.
function main()
    local root=os.curdir()
    local factory=import('build.xmake.graph',{rootdir=root})
    local policy=import('build.xmake.link_dependencies',{rootdir=root})
    local native=import('private.action.build.target')
    local depend=import('core.project.depend')
    local graph=factory.new(root,{platform='web',target='template_release',arch='wasm32'})
    graph:configure()
    local serialized=graph:serialize()
    local files=policy.files(serialized,serialized.programs[1],root)
    local library=path.join(root,'platform/web/js/libs/library_godot_runtime.js')
    local extern=path.join(root,'platform/web/js/libs/library_godot_webgl2.externs.js')
    local wrapper=path.join(root,'platform/web/js/engine/engine.js')
    assert(table.contains(files,library),'Recipe JS libraries must survive native graph serialization')
    assert(table.contains(files,extern),'Closure extern inputs must survive native graph serialization')
    assert(table.contains(files,wrapper),'Wrapper inputs must invalidate after-link packaging')
    assert(table.contains(files,path.join(root,'misc/dist/html/full-size.html')),'Template HTML input must invalidate package')
    local folder=path.join(root,'.build/xmake-link-dependencies-test'); os.mkdir(folder)
    local js,engine=path.join(folder,'library.js'),path.join(folder,'engine.js')
    io.writefile(js,'first library'); io.writefile(engine,'first wrapper')
    local fixture={options={platform='linuxbsd'},dependencies={{targets={{kind='file',path='bin/probe'}},inputs={{kind='file',path=path.relative(js,root)},{kind='file',path=path.relative(engine,root)}}}}}
    local state={}
    local target={data=function(_,name) return state[name] end,data_set=function(_,name,value) state[name]=value end,
        objectfiles=function() return {} end,orderdeps=function() return {} end}
    policy.configure(target,fixture,{filename='probe'},root)
    policy.configure(target,fixture,{filename='probe'},root)
    assert(#state.linkdepfiles==2,'Repeated native target configuration must not duplicate dependencies')
    local depfiles=native.get_linkdepfiles(target)
    assert(#depfiles==2 and table.contains(depfiles,js) and table.contains(depfiles,engine),'Native linker collector must receive exact source inputs')
    local cache=path.join(folder,'native-link.dep')
    local output=path.join(folder,'linked.txt')
    if os.isfile(cache) then os.rm(cache) end
    local links=0
    local function link()
        depend.on_changed(function()
            links=links+1
            io.writefile(output,io.readfile(js) .. io.readfile(engine))
        end,{dependfile=cache,files=depfiles,values={'same-linker',{'same-flags'}},timecache=false})
    end
    link(); assert(links==1,'Cold link must run')
    link(); assert(links==1,'Unchanged link must remain cached')
    os.sleep(1100); io.writefile(js,'changed same-path library')
    link(); assert(links==2 and io.readfile(output):find('changed same%-path library'),'Same-path JS change must rerun native link')
    os.sleep(1100); io.writefile(engine,'changed same-path wrapper')
    link(); assert(links==3 and io.readfile(output):find('changed same%-path wrapper'),'Wrapper change must rerun native link and enable after_link packaging')
    print('XMAKE_LINK_DEPENDENCIES_PASS 10')
end
