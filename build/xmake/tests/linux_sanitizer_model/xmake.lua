set_xmakever('3.1.1')

target('linux_sanitizer_model')
 set_kind('phony')
 on_config(function(target)
  import('lib.detect.find_tool')
  local root=path.absolute('../../../..',os.projectdir())
  local policy=import('build.xmake.platforms.init',{rootdir=root})
  local gcc=import('core.tools.gcc')
  local checks=0
  local function check(value,message) assert(value,message); checks=checks+1 end
  local function capture(options)
   local probe={values={}}
   function probe:set(key,...) self.values[key]={...} end
   function probe:add(key,...)
    self.values[key]=self.values[key] or {}
    for _,value in ipairs({...}) do if type(value)~='table' then table.insert(self.values[key],value) end end
   end
   policy.configure(probe,options)
   return probe.values
  end
  local driver={program=function() return 'g++' end,is_plat=function() return false end}
  local cases={}
  for _,sanitizer in ipairs({'asan','ubsan','lsan','tsan','msan'}) do
   table.insert(cases,{platform='linuxbsd',arch='x86_64',['use_' .. sanitizer]=true,expect=true})
   table.insert(cases,{platform='linuxbsd',arch='x86_64',['use_' .. sanitizer]=true,use_llvm=true,expect=false})
  end
  table.join2(cases,{
   {platform='linuxbsd',arch='x86_64',use_asan=true,use_ubsan=true,expect=true},
   {platform='linuxbsd',arch='amd64',use_asan='y',expect=true},
   {platform='linuxbsd',arch='x86_64',expect=false},
   {platform='linuxbsd',arch='x86_64',use_asan='n',expect=false},
   {platform='linuxbsd',arch='x86_32',use_asan=true,expect=false},
   {platform='linuxbsd',arch='arm64',use_asan=true,expect=false},
   {platform='windows',arch='x86_64',use_asan=true,expect=false},
   {platform='windows',arch='x86_64',use_mingw=true,use_asan=true,expect=false},
   {platform='macos',arch='x86_64',use_asan=true,expect=false},
   {platform='android',arch='arm64',use_asan=true,expect=false},
   {platform='ios',arch='arm64',use_asan=true,expect=false},
   {platform='visionos',arch='arm64',use_asan=true,expect=false},
   {platform='web',arch='wasm32',use_asan=true,expect=false}
  })
  local production
  for _,options in ipairs(cases) do
   options.accesskit=false; options.angle=false; options.d3d12=false; options.sdl=false
   options.vulkan=false; options.opengl3=false; options.metal=false
   local flags=capture(options)
   for _,key in ipairs({'cxflags','ldflags','shflags'}) do
    check(table.contains(flags[key] or {},'-mcmodel=medium')==options.expect,'Medium model leaked or was omitted from production ' .. key)
   end
   local _,compile=gcc.compargv(driver,'probe.cpp','probe.o',flags.cxflags or {},{rawargs=true})
   local _,link=gcc.linkargv(driver,{'probe.o'},'binary','probe',flags.ldflags or {},{rawargs=true})
   local _,shared=gcc.linkargv(driver,{'probe.o'},'shared','probe.so',flags.shflags or {},{rawargs=true})
   for _,argv in ipairs({compile,link,shared}) do
    check(table.contains(argv,'-mcmodel=medium')==options.expect,'Installed GCC driver lost the selected production model')
   end
   if options.platform=='linuxbsd' and options.use_asan==true and options.use_ubsan==true then
    production=flags
    for _,key in ipairs({'cxflags','ldflags'}) do
     local instrumentation=false
     for _,flag in ipairs(flags[key]) do
      if flag:startswith('-fsanitize=') and flag:find('address',1,true) and flag:find('undefined',1,true) then instrumentation=true end
     end
     check(instrumentation,'Address and undefined instrumentation must remain enabled')
    end
   end
  end

  local model_flags={}
  for _,flag in ipairs(production.cxflags) do if flag:startswith('-mcmodel=') then table.insert(model_flags,flag) end end
  -- A sparse NOBITS global exceeds 2GiB without allocating or writing that size.
  -- Use native GCC/binutils on Linux; Windows uses installed LLVM's real ELF backend.
  local tools={}
  if os.host()=='windows' then
   local directories={}
   for _,variable in ipairs({'ProgramFiles','ProgramFiles(x86)'}) do
    local base=os.getenv(variable)
    if base then
     table.join2(directories,os.dirs(path.join(base,'Microsoft Visual Studio','*','*','VC','Tools','Llvm','x64','bin')))
     table.insert(directories,path.join(base,'LLVM','bin'))
    end
   end
   tools.cc=assert(find_tool('clang',{paths=directories}),'ELF object proof requires installed Clang')
   tools.ld=assert(find_tool('ld.lld',{paths=directories}),'ELF link proof requires installed LLD')
  else
   tools.cc=assert(find_tool('gcc'),'Linux object proof requires GCC')
   tools.ld=assert(find_tool('ld'),'Linux link proof requires binutils')
  end
  local config=import('core.project.config')
  local output=path.absolute(config.get('builddir') or config.get('buildir') or 'build',os.projectdir())
  os.mkdir(output)
  local source=path.join(output,'large_data.c')
  io.writefile(source,[[volatile unsigned char large[0x80010000ULL];
volatile int small;
void _start(void) { int result=large[0]+small; __asm__ volatile("syscall" : : "a"(60), "D"(result) : "rcx", "r11", "memory"); __builtin_unreachable(); }
]])
  local function compile(name,flags)
   local object=path.join(output,name .. '.o')
   local argv={'-O0','-fPIE','-c',source,'-o',object}
   if os.host()=='windows' then table.insert(argv,1,'--target=x86_64-linux-gnu') end
   table.join2(argv,flags)
   os.vrunv(tools.cc.program,argv,{timeout=30000})
   return object
  end
  local small=compile('small',{})
  local medium=compile('medium',model_flags)
  check(not io.readfile(small,{encoding='binary'}):find('.lbss',1,true),'Small-model negative control unexpectedly used large-data BSS')
  check(io.readfile(medium,{encoding='binary'}):find('.lbss',1,true),'Medium model must place the huge global in large-data BSS')
  local function link(object,name)
   return os.iorunv(tools.ld.program,{'-pie','-e','_start','-o',path.join(output,name),object},{timeout=30000})
  end
  local failed,reason=false,''
  try {function() link(small,'small') end,catch {function(errors) failed=true; reason=tostring(errors) end}}
  check(failed and (reason:find('R_X86_64_PC32',1,true) or reason:find('out of range',1,true)),'Small-model negative control must reproduce a PC32 overflow')
  io.writefile(path.join(output,'small-model-negative.txt'),reason)
  link(medium,'medium')
  check(os.isfile(path.join(output,'medium')),'Medium-model object must link as actual ELF with the same large global')
  local instrumented=compile('instrumented',production.cxflags)
  local bytes=io.readfile(instrumented,{encoding='binary'})
  check(bytes:find('.lbss',1,true),'Instrumented large global must retain large-data placement')
  check(bytes:find('__asan_',1,true) and bytes:find('__ubsan_',1,true),'Actual object must retain both sanitizer families')
  local _,compile_argv=gcc.compargv(driver,'probe.cpp','probe.o',production.cxflags,{rawargs=true})
  local _,link_argv=gcc.linkargv(driver,{'probe.o'},'binary','probe',production.ldflags,{rawargs=true})
  import('core.base.json').savefile(path.join(output,'model-proof.json'),{
   passed=true,checks=checks,compiler=tools.cc.program,linker=tools.ld.program,
   actual_elf_backend=os.host()=='windows' and 'Clang/LLD cross-ELF' or 'GCC/GNU ld native ELF',
   production_gcc_compile_argv=compile_argv,production_gcc_link_argv=link_argv,
   small_model_overflow=true,medium_model_linked=true,asan_and_ubsan_retained=true,
   full_sanitized_engine_link_qualified=false
  })
  print('NATIVE_LINUX_SANITIZER_MODEL_COMPILER=' .. tools.cc.program)
  print('NATIVE_LINUX_SANITIZER_MODEL_CHECKS=' .. checks)
 end)
