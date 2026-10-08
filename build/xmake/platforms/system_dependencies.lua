-- Native pkg-config resolution for explicitly selected system vendor libraries.
local function enabled(value, default)
    if value==nil then return default end
    return value==true or value=='yes' or value=='true' or value=='y' or value=='1'
end

function configure(target, options, query)
    if options.platform~='linuxbsd' then return end
    local packages={
        {'builtin_freetype',{'freetype2'}},{'builtin_graphite',{'graphite2'}},
        {'builtin_icu4c',{'icu-i18n','icu-uc'}},{'builtin_harfbuzz',{'harfbuzz','harfbuzz-icu','harfbuzz-raster','harfbuzz-vector'}},
        {'builtin_libpng',{'libpng16'}},{'builtin_zstd',{'libzstd'}},
        {'builtin_libtheora',options.target=='editor' and {'theora','theoradec','theoraenc'} or {'theora','theoradec'}},
        {'builtin_libvorbis',options.target=='editor' and {'vorbis','vorbisfile','vorbisenc'} or {'vorbis','vorbisfile'}},
        {'builtin_libogg',{'ogg'}},{'builtin_libwebp',{'libwebp'}},{'builtin_libjpeg_turbo',{'libturbojpeg'}},
        {'builtin_mbedtls',{'mbedtls','mbedcrypto','mbedx509'}},{'builtin_wslay',{'libwslay'}},
        {'builtin_miniupnpc',{'miniupnpc'}},{'builtin_pcre2',{'libpcre2-32'}},
        {'builtin_recastnavigation',{'recastnavigation'}},{'builtin_openxr',{'openxr'}},{'builtin_zlib',{'zlib'}}
    }
    if enabled(options.brotli,true) then table.insert(packages,{'builtin_brotli',{'libbrotlicommon','libbrotlidec'}}) end
    if enabled(options.sdl,true) then table.insert(packages,{'builtin_sdl',{'sdl3'}}) end
    for _, dependency in ipairs(packages) do
        if not enabled(options[dependency[1]],true) then
            query=query or import('lib.detect.pkgconfig').libinfo
            for _, name in ipairs(dependency[2]) do
                local info=assert(query(name),'Missing selected system dependency: ' .. name)
                for _, key in ipairs({'includedirs','linkdirs','links','cxflags'}) do
                    if info[key] then target:add(key,table.unpack(info[key])) end
                end
            end
        end
    end
    if not enabled(options.builtin_embree,true) and table.contains({'x86_64','arm64'},options.arch) then target:add('syslinks','embree4') end
    if enabled(options.rendering_device,true) and not enabled(options.builtin_glslang,true) then target:add('syslinks','glslang','SPIRV','glslang-default-resource-limits') end
end
