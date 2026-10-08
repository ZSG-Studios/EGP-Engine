-- Real Clang parsing also checks every decoded license byte using constexpr evaluation.
function main()
    local root=os.curdir()
    local folder=path.join(root,'.build/xmake-license-literals')
    os.mkdir(folder)
    local generator=import('build.xmake.generators.license',{rootdir=root})
    local support=import('build.xmake.generators.support',{rootdir=root})
    local find_tool=import('lib.detect.find_tool')
    local compiler=find_tool('clang++') or find_tool('clang-cl')
    if not compiler and os.host()=='windows' then
        compiler=find_tool('clang++',{force=true,paths={path.join(os.getenv('ProgramFiles'),'Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin')}})
    end
    assert(compiler,'Clang is required to validate the engine license literal warning contract')
    local clangcl=path.basename(compiler.program)=='clang-cl'
    local function parse(source)
        local args=clangcl and {'/std:c++17','/W4','/WX','/Zs','/clang:-Wstring-concatenation',source}
            or {'-std=c++17','-Wall','-Wextra','-Werror','-Wstring-concatenation','-fsyntax-only',source}
        os.iorunv(compiler.program,args,{curdir=root,timeout=30000})
    end
    local body='License bytes: ' .. string.rep('x',3000) .. ' "quoted" \195\169 \\ end'
    local expected=body .. '\n'
    local header=path.join(folder,'synthetic.gen.h')
    assert(generator.generate('make_license_header',{targets={header},sources={{value='License: Fixture\n ' .. body .. '\n\nLicense: Second\n Second license body\n'},{value='Fixture main license\n'}}},{root=root}))
    local text=assert(io.readfile(header))
    assert(text:find('(' .. support.rawstring(expected) .. '),',1,true),'Chunked array strings require an explicit single-expression boundary')
    local source=path.join(folder,'synthetic.cpp')
    io.writefile(source,'#include "synthetic.gen.h"\nconstexpr unsigned char expected[]={ ' .. support.buffer(expected) .. ' };\nconstexpr bool exact_bytes(){for(unsigned long long i=0;i<sizeof(expected);++i){if(static_cast<unsigned char>(LICENSE_BODIES[0][i])!=expected[i])return false;}return LICENSE_BODIES[0][sizeof(expected)]==0;}\nstatic_assert(LICENSE_COUNT==2 && exact_bytes(),"License byte content changed");\n')
    parse(source)
    local actual=path.join(folder,'actual.gen.h')
    assert(generator.generate('make_license_header',{targets={actual},sources={{path='COPYRIGHT.txt'},{path='LICENSE.txt'}}},{root=root}))
    source=path.join(folder,'actual.cpp')
    io.writefile(source,'#include "actual.gen.h"\nstatic_assert(LICENSE_COUNT>0 && COPYRIGHT_INFO_COUNT>0,"Actual corpus is empty");\n')
    parse(source)
    local actual_text=assert(io.readfile(actual))
    local marker='inline constexpr const char *LICENSE_BODIES[] = {\n'
    local offset=assert(actual_text:find(marker,1,true))+#marker
    local old_body=actual_text:sub(offset):gsub('^%(',''):gsub('\n%(','\n'):gsub('%),\n',',\n')
    local previous=path.join(folder,'previous.gen.h')
    io.writefile(previous,actual_text:sub(1,offset-1) .. old_body)
    local previous_source=path.join(folder,'previous.cpp')
    io.writefile(previous_source,'#include "previous.gen.h"\n')
    local rejected=false
    try {function() parse(previous_source) end,
        catch {function(errors) rejected=tostring(errors):find('string-concatenation',1,true)~=nil end}}
    assert(rejected,'The previous actual-corpus array emission must reproduce Clang warning-as-error')
    print('NATIVE_LICENSE_LITERAL_CHECKS=5')
end
