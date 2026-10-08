-- Golden shader generation verification. Outputs are isolated from tracked fixtures.
function main()
    local root=os.curdir()
    local generator=import('build.xmake.generators.shaders',{rootdir=root})
    local count=0
    for _, fixture in ipairs({{'gles3','vertex_fragment','build_gles3_headers'},{'glsl','compute','build_raw_headers'},{'glsl','vertex_fragment','build_raw_headers'},{'rd_glsl','compute','build_rd_headers'},{'rd_glsl','vertex_fragment','build_rd_headers'}}) do
        local base=path.join(root,'tests/build/fixtures',fixture[1],fixture[2])
        local output=path.join(root,'.build/xmake-shader-fixtures',fixture[1],fixture[2] .. '.out')
        assert(generator.generate({builder=fixture[3],targets={output},sources={{path=base .. '.glsl'}}},{root=root}))
        local actual=assert(io.readfile(output)):gsub('\r\n','\n'):gsub('^/%* THIS FILE IS GENERATED%. EDITS WILL BE LOST%. %*/\n\n','')
        local expected=assert(io.readfile(base .. '.out')):gsub('\r\n','\n')
        assert(actual==expected,'Shader golden fixture differs: ' .. base)
        count=count+1
    end
    print('XMAKE_SHADER_FIXTURES_PASS ' .. count)
end
