set_xmakever('3.1.1')
set_languages('cxx17')
target('web_export_arguments')
 set_kind('binary')
 add_files('probe.cpp')
 on_config(function(target)
  local root=path.absolute('../../../..',os.projectdir())
  local json=import('core.base.json')
  local projectdir=os.projectdir
  os.projectdir=function() return root end -- Root the version metadata import to the engine checkout.
  local factory=import('build.xmake.graph',{rootdir=root})
  os.projectdir=projectdir
  local emcc=import('core.tools.emcc')
  local compiler=import('core.tool.compiler').load('cxx',{target=target})
  local checks=0
  local records={}
  local function check(value,message) assert(value,message); checks=checks+1 end
  for _,dynamic in ipairs({false,true}) do
   local graph=factory.new(root,{platform='web',arch='wasm32',target='template_release',dlink_enabled=dynamic})
   graph:configure()
   local flags=graph:serialize().programs[1].policy.LINKFLAGS
   for _,field in ipairs({'EXPORTED_FUNCTIONS','EXPORTED_RUNTIME_METHODS'}) do
    local flag
    for _,value in ipairs(flags) do if value:startswith('-s' .. field .. '=') then flag=value end end
    check(flag,'Native recipe export setting missing')
    local processed=compiler:_preprocess_flags({flag})
    check(#processed==1 and processed[1]==flag,'Native xmake flag preprocessing must preserve the entire JSON setting')
    local _,argv=emcc.linkargv({program=function() return 'em++' end},{'probe.o'},'binary','game.js',processed,{rawargs=true})
    check(#argv==4 and argv[4]==flag,'Installed emcc linker must receive exactly one complete setting argument')
    local decoded=json.decode(argv[4]:sub(#('-s' .. field .. '=')+1))
    local expected=graph:use('env')[field]
    table.insert(records,{flag=flag,field=field,expected=expected})
    check(#decoded==#expected,'Every export must remain in the compiler setting')
    table.sort(decoded); local sorted=table.clone(expected); table.sort(sorted)
    check(table.concat(decoded,'\0')==table.concat(sorted,'\0'),'Export names and JSON string quotes must survive intact')
   end
  end
  local old="-sEXPORTED_FUNCTIONS=['_free', '_main', '_malloc']"
  local split=compiler:_preprocess_flags({old})
  check(#split>1,'Negative control must reproduce the original split-setting regression')
  target:data_set('export_records',records)
  target:data_set('export_checks',checks)
  print('NATIVE_WEB_EXPORT_CONFIG_CHECKS=' .. checks)
 end)

 after_build(function(target)
  local emcc=import('core.tools.emcc')
  local json=import('core.base.json')
  local checks=target:data('export_checks')
  for _,record in ipairs(target:data('export_records')) do
   local _,argv=emcc.linkargv({program=function() return 'em++' end},{'probe.o'},'binary','game.js',{record.flag},{})
   local output=os.iorunv(target:targetfile(),argv)
   local lines=output:trim():split('\n',{plain=true})
   assert(#lines==4 and lines[4]:trim()==record.flag,'Actual native process must receive one setting with JSON quotes intact')
   checks=checks+1
   local decoded=json.decode(lines[4]:trim():sub(#('-s' .. record.field .. '=')+1))
   table.sort(decoded); local expected=table.clone(record.expected); table.sort(expected)
   assert(table.concat(decoded,'\0')==table.concat(expected,'\0'),'Native process must retain every export name')
   checks=checks+1
  end
  print('NATIVE_WEB_EXPORT_ARGUMENT_CHECKS=' .. checks)
 end)
