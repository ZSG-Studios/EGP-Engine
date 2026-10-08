-- Exercises Xmake's actual native link dependency collector and cache checker.
-- No compiler or linker is launched.
function main()
    local root=os.curdir()
    local factory=import('build.xmake.graph',{rootdir=root})
    local policy=import('build.xmake.link_dependencies',{rootdir=root})
    local native=import('private.action.build.target')
    local depend=import('core.project.depend')
    assert(not utils.trycall(function() factory.new(root,{platform='web'}):configure() end),'Removed WebGL graph must fail')
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
    print('XMAKE_LINK_DEPENDENCIES_PASS 7')
end
