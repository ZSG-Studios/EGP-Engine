set_project("egp-native-codegen-tools")
set_version("1.0.0")
set_xmakever("3.1.1")
set_languages("c17", "cxx17")
add_rules("mode.debug", "mode.release")
target("egp_compress")
    set_kind("binary")
    set_default(true)
    set_targetdir("$(builddir)/bin")
    add_files("compress.cpp")
    add_includedirs("../../../thirdparty/zlib")
    for _, name in ipairs({"adler32", "compress", "crc32", "deflate", "trees", "zutil"}) do
        add_files("../../../thirdparty/zlib/" .. name .. ".c")
    end
    if is_plat("windows") then add_defines("_CRT_SECURE_NO_WARNINGS") end
target("egp_zip")
    set_kind("binary")
    set_targetdir("$(builddir)/bin")
    add_files("zip.cpp")
    add_includedirs("../../../thirdparty/zlib")
    for _, name in ipairs({"adler32", "compress", "crc32", "deflate", "trees", "zutil"}) do
        add_files("../../../thirdparty/zlib/" .. name .. ".c")
    end
