-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_graphite, env_harfbuzz, env_icu, env_modules, env_text_server_adv, freetype_enabled, icudata, idx, inserted, lib, linklib, module_obj, msdfgen_enabled, text_server_adv_builders, thirdparty_dir, thirdparty_obj, thirdparty_sources
    text_server_adv_builders = graph:builders("text_server_adv_builders")
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_text_server_adv = env_modules:clone()
    thirdparty_obj = {}
    freetype_enabled = (R.contains(env.module_list, "freetype"))
    msdfgen_enabled = (R.contains(env.module_list, "msdfgen"))
    if R.truthy((R.contains(env.module_list, "svg"))) then
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/thorvg/inc", "#thirdparty/thorvg/src/common", "#thirdparty/thorvg/src/renderer"}})
        env_text_server_adv:add({["CPPDEFINES"] = {"TVG_STATIC"}})
    end
    if R.truthy(R.index(env, "builtin_icu4c")) then
        env_text_server_adv:add({["CPPDEFINES"] = {"HAVE_ICU_BUILTIN"}})
    end
    if R.truthy(R.index(env, "builtin_harfbuzz")) then
        env_harfbuzz = env_modules:clone()
        env_harfbuzz:disable_warnings()
        thirdparty_dir = "#thirdparty/harfbuzz/"
        thirdparty_sources = {"src/hb-aat-layout.cc", "src/hb-aat-map.cc", "src/hb-blob.cc", "src/hb-buffer-serialize.cc", "src/hb-buffer-verify.cc", "src/hb-buffer.cc", "src/hb-common.cc", "src/hb-draw.cc", "src/hb-face-builder.cc", "src/hb-face.cc", "src/hb-fallback-shape.cc", "src/hb-font.cc", "src/hb-icu.cc", "src/hb-map.cc", "src/hb-number.cc", "src/hb-ot-cff1-table.cc", "src/hb-ot-cff2-table.cc", "src/hb-ot-color.cc", "src/hb-ot-face.cc", "src/hb-ot-font.cc", "src/hb-ot-layout.cc", "src/hb-ot-map.cc", "src/hb-ot-math.cc", "src/hb-ot-meta.cc", "src/hb-ot-metrics.cc", "src/hb-ot-name.cc", "src/hb-ot-shaper-arabic.cc", "src/hb-ot-shaper-default.cc", "src/hb-ot-shaper-hangul.cc", "src/hb-ot-shaper-hebrew.cc", "src/hb-ot-shaper-indic-table.cc", "src/hb-ot-shaper-indic.cc", "src/hb-ot-shaper-khmer.cc", "src/hb-ot-shaper-myanmar.cc", "src/hb-ot-shaper-syllabic.cc", "src/hb-ot-shaper-thai.cc", "src/hb-ot-shaper-use.cc", "src/hb-ot-shaper-vowel-constraints.cc", "src/hb-ot-shape-fallback.cc", "src/hb-ot-shape-normalize.cc", "src/hb-ot-shape.cc", "src/hb-ot-tag.cc", "src/hb-ot-var.cc", "src/hb-outline.cc", "src/hb-paint-bounded.cc", "src/hb-paint-extents.cc", "src/hb-paint.cc", "src/hb-set.cc", "src/hb-shape-plan.cc", "src/hb-shape.cc", "src/hb-shaper.cc", "src/hb-static.cc", "src/hb-style.cc", "src/hb-raster.cc", "src/hb-raster-draw.cc", "src/hb-raster-image.cc", "src/hb-raster-paint.cc", "src/hb-vector-draw.cc", "src/hb-vector-paint-svg.cc", "src/hb-vector-paint.cc", "src/hb-vector-path.cc", "src/hb-vector.cc", "src/hb-subset.cc", "src/hb-ucd.cc", "src/hb-unicode.cc", "src/hb-zlib.cc", "src/OT/Var/VARC/VARC.cc"}
        if R.truthy(freetype_enabled) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"src/hb-ft.cc"})
            if R.truthy(R.index(env, "graphite")) then
                thirdparty_sources = R.iadd(thirdparty_sources, {"src/hb-graphite2.cc"})
            end
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_harfbuzz:prepend({["CPPPATH"] = {"#thirdparty/harfbuzz/src"}})
        env_harfbuzz:add({["CPPDEFINES"] = {"HAVE_ICU", "HAVE_ZLIB", "HAVE_PNG"}})
        if R.truthy(R.index(env, "builtin_icu4c")) then
            env_harfbuzz:prepend({["CPPPATH"] = {"#thirdparty/icu4c/common/", "#thirdparty/icu4c/i18n/"}})
            env_harfbuzz:add({["CPPDEFINES"] = {"U_STATIC_IMPLEMENTATION", {"U_HAVE_LIB_SUFFIX", 1}, {"U_LIB_SUFFIX_C_NAME", "_godot"}, "HAVE_ICU_BUILTIN"}})
        end
        if R.truthy(freetype_enabled) then
            env_harfbuzz:add({["CPPDEFINES"] = {"HAVE_FREETYPE"}})
            if R.truthy(R.index(env, "graphite")) then
                env_harfbuzz:add({["CPPDEFINES"] = {"HAVE_GRAPHITE2"}})
            end
            if R.truthy(R.index(env, "builtin_freetype")) then
                env_harfbuzz:prepend({["CPPPATH"] = {"#thirdparty/freetype/include"}})
            end
            if R.truthy((function() local v = R.index(env, "builtin_graphite"); if not R.truthy(v) then return v end; return R.index(env, "graphite") end)()) then
                env_harfbuzz:prepend({["CPPPATH"] = {"#thirdparty/graphite/include"}})
                env_harfbuzz:add({["CPPDEFINES"] = {"GRAPHITE2_STATIC"}})
            end
        end
        if R.truthy((R.contains({"android", "linuxbsd", "web"}, R.index(env, "platform")))) then
            env_harfbuzz:add({["CPPDEFINES"] = {"HAVE_PTHREAD"}})
        end
        if R.truthy((function() local v = ((R.index(env, "platform") == "linuxbsd")); if not R.truthy(v) then return v end; local v = R.index(env, "use_ubsan"); if not R.truthy(v) then return v end; return not R.truthy(R.index(env, "use_llvm")) end)()) then
            env_harfbuzz:add({["CCFLAGS"] = {"-fno-sanitize=bounds-strict"}})
        end
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/harfbuzz/src"}})
        lib = env_harfbuzz:library("harfbuzz_builtin", thirdparty_sources)
        thirdparty_obj = R.iadd(thirdparty_obj, lib)
        inserted = false
        for _, __item3 in ipairs(R.iter(R.enumerate(R.index(env, "LIBS")))) do
            local __item4 = __item3; idx = R.index(__item4, 0); linklib = R.index(__item4, 1)
            if R.truthy(R.isinstance(linklib, {"str", "bytes"})) then
                R.insert(R.index(env, "LIBS"), idx, lib)
                inserted = true
                break
            end
        end
        if R.truthy(not R.truthy(inserted)) then
            env:add({["LIBS"] = {lib}})
        end
    end
    if R.truthy((function() local v = R.index(env, "builtin_graphite"); if not R.truthy(v) then return v end; local v = freetype_enabled; if not R.truthy(v) then return v end; return R.index(env, "graphite") end)()) then
        env_graphite = env_modules:clone()
        env_graphite:disable_warnings()
        thirdparty_dir = "#thirdparty/graphite/"
        thirdparty_sources = {"src/gr_char_info.cpp", "src/gr_face.cpp", "src/gr_features.cpp", "src/gr_font.cpp", "src/gr_logging.cpp", "src/gr_segment.cpp", "src/gr_slot.cpp", "src/CmapCache.cpp", "src/Code.cpp", "src/Collider.cpp", "src/Decompressor.cpp", "src/Face.cpp", "src/FeatureMap.cpp", "src/Font.cpp", "src/GlyphCache.cpp", "src/GlyphFace.cpp", "src/Intervals.cpp", "src/Justifier.cpp", "src/NameTable.cpp", "src/Pass.cpp", "src/Position.cpp", "src/Segment.cpp", "src/Silf.cpp", "src/Slot.cpp", "src/Sparse.cpp", "src/TtfUtil.cpp", "src/UtfCodec.cpp", "src/FileFace.cpp", "src/json.cpp"}
        if R.truthy(not R.truthy(env_graphite.msvc)) then
            thirdparty_sources = R.iadd(thirdparty_sources, {"src/direct_machine.cpp"})
        else
            thirdparty_sources = R.iadd(thirdparty_sources, {"src/call_machine.cpp"})
        end
        thirdparty_sources = (function() local __item5 = {}; for _, __item6 in ipairs(R.iter(thirdparty_sources)) do; local file = __item6; table.insert(__item5, R.add(thirdparty_dir, file)); end; return __item5 end)()
        env_graphite:prepend({["CPPPATH"] = {"#thirdparty/graphite/src", "#thirdparty/graphite/include"}})
        env_graphite:add({["CPPDEFINES"] = {"GRAPHITE2_STATIC", "GRAPHITE2_NTRACING", "GRAPHITE2_NFILEFACE"}})
        lib = env_graphite:library("graphite_builtin", thirdparty_sources)
        thirdparty_obj = R.iadd(thirdparty_obj, lib)
        inserted = false
        for _, __item7 in ipairs(R.iter(R.enumerate(R.index(env, "LIBS")))) do
            local __item8 = __item7; idx = R.index(__item8, 0); linklib = R.index(__item8, 1)
            if R.truthy(R.isinstance(linklib, {"str", "bytes"})) then
                R.insert(R.index(env, "LIBS"), idx, lib)
                inserted = true
                break
            end
        end
        if R.truthy(not R.truthy(inserted)) then
            env:add({["LIBS"] = {lib}})
        end
    end
    if R.truthy(R.index(env, "builtin_icu4c")) then
        env_icu = env_modules:clone()
        env_icu:disable_warnings()
        thirdparty_dir = "#thirdparty/icu4c/"
        thirdparty_sources = {"common/appendable.cpp", "common/bmpset.cpp", "common/brkeng.cpp", "common/brkiter.cpp", "common/bytesinkutil.cpp", "common/bytestream.cpp", "common/bytestrie.cpp", "common/bytestriebuilder.cpp", "common/bytestrieiterator.cpp", "common/caniter.cpp", "common/characterproperties.cpp", "common/chariter.cpp", "common/charstr.cpp", "common/cmemory.cpp", "common/cstr.cpp", "common/cstring.cpp", "common/cwchar.cpp", "common/dictbe.cpp", "common/dictionarydata.cpp", "common/dtintrv.cpp", "common/edits.cpp", "common/emojiprops.cpp", "common/errorcode.cpp", "common/filteredbrk.cpp", "common/filterednormalizer2.cpp", "common/fixedstring.cpp", "common/icudataver.cpp", "common/icuplug.cpp", "common/loadednormalizer2impl.cpp", "common/localebuilder.cpp", "common/localematcher.cpp", "common/localeprioritylist.cpp", "common/locavailable.cpp", "common/locbased.cpp", "common/locdispnames.cpp", "common/locdistance.cpp", "common/locdspnm.cpp", "common/locid.cpp", "common/loclikely.cpp", "common/loclikelysubtags.cpp", "common/locmap.cpp", "common/locresdata.cpp", "common/locutil.cpp", "common/lsr.cpp", "common/lstmbe.cpp", "common/messagepattern.cpp", "common/mlbe.cpp", "common/normalizer2.cpp", "common/normalizer2impl.cpp", "common/normlzr.cpp", "common/parsepos.cpp", "common/patternprops.cpp", "common/pluralmap.cpp", "common/propname.cpp", "common/propsvec.cpp", "common/punycode.cpp", "common/putil.cpp", "common/rbbi.cpp", "common/rbbi_cache.cpp", "common/rbbidata.cpp", "common/rbbinode.cpp", "common/rbbirb.cpp", "common/rbbiscan.cpp", "common/rbbisetb.cpp", "common/rbbistbl.cpp", "common/rbbitblb.cpp", "common/resbund.cpp", "common/resbund_cnv.cpp", "common/resource.cpp", "common/restrace.cpp", "common/ruleiter.cpp", "common/schriter.cpp", "common/serv.cpp", "common/servlk.cpp", "common/servlkf.cpp", "common/servls.cpp", "common/servnotf.cpp", "common/servrbf.cpp", "common/servslkf.cpp", "common/sharedobject.cpp", "common/simpleformatter.cpp", "common/static_unicode_sets.cpp", "common/stringpiece.cpp", "common/stringtriebuilder.cpp", "common/uarrsort.cpp", "common/ubidi.cpp", "common/ubidi_props.cpp", "common/ubidiln.cpp", "common/ubiditransform.cpp", "common/ubidiwrt.cpp", "common/ubrk.cpp", "common/ucase.cpp", "common/ucasemap.cpp", "common/ucasemap_titlecase_brkiter.cpp", "common/ucat.cpp", "common/uchar.cpp", "common/ucharstrie.cpp", "common/ucharstriebuilder.cpp", "common/ucharstrieiterator.cpp", "common/uchriter.cpp", "common/ucln_cmn.cpp", "common/ucmndata.cpp", "common/ucnv.cpp", "common/ucnv2022.cpp", "common/ucnv_bld.cpp", "common/ucnv_cb.cpp", "common/ucnv_cnv.cpp", "common/ucnv_ct.cpp", "common/ucnv_err.cpp", "common/ucnv_ext.cpp", "common/ucnv_io.cpp", "common/ucnv_lmb.cpp", "common/ucnv_set.cpp", "common/ucnv_u16.cpp", "common/ucnv_u32.cpp", "common/ucnv_u7.cpp", "common/ucnv_u8.cpp", "common/ucnvbocu.cpp", "common/ucnvdisp.cpp", "common/ucnvhz.cpp", "common/ucnvisci.cpp", "common/ucnvlat1.cpp", "common/ucnvmbcs.cpp", "common/ucnvscsu.cpp", "common/ucnvsel.cpp", "common/ucol_swp.cpp", "common/ucptrie.cpp", "common/ucurr.cpp", "common/udata.cpp", "common/udatamem.cpp", "common/udataswp.cpp", "common/uenum.cpp", "common/uhash.cpp", "common/uhash_us.cpp", "common/uidna.cpp", "common/uinit.cpp", "common/uinvchar.cpp", "common/uiter.cpp", "common/ulist.cpp", "common/uloc.cpp", "common/uloc_keytype.cpp", "common/uloc_tag.cpp", "common/ulocale.cpp", "common/ulocbuilder.cpp", "common/umapfile.cpp", "common/umath.cpp", "common/umutablecptrie.cpp", "common/umutex.cpp", "common/unames.cpp", "common/unifiedcache.cpp", "common/unifilt.cpp", "common/unifunct.cpp", "common/uniset.cpp", "common/uniset_closure.cpp", "common/uniset_props.cpp", "common/unisetspan.cpp", "common/unistr.cpp", "common/unistr_case.cpp", "common/unistr_case_locale.cpp", "common/unistr_cnv.cpp", "common/unistr_props.cpp", "common/unistr_titlecase_brkiter.cpp", "common/unorm.cpp", "common/unormcmp.cpp", "common/uobject.cpp", "common/uprops.cpp", "common/ures_cnv.cpp", "common/uresbund.cpp", "common/uresdata.cpp", "common/usc_impl.cpp", "common/uscript.cpp", "common/uscript_props.cpp", "common/uset.cpp", "common/uset_props.cpp", "common/usetiter.cpp", "common/usprep.cpp", "common/ustack.cpp", "common/ustr_cnv.cpp", "common/ustr_titlecase_brkiter.cpp", "common/ustr_wcs.cpp", "common/ustrcase.cpp", "common/ustrcase_locale.cpp", "common/ustrenum.cpp", "common/ustrfmt.cpp", "common/ustring.cpp", "common/ustrtrns.cpp", "common/utext.cpp", "common/utf_impl.cpp", "common/util.cpp", "common/util_props.cpp", "common/utrace.cpp", "common/utrie.cpp", "common/utrie2.cpp", "common/utrie2_builder.cpp", "common/utrie_swap.cpp", "common/uts46.cpp", "common/utypes.cpp", "common/uvector.cpp", "common/uvectr32.cpp", "common/uvectr64.cpp", "common/wintz.cpp", "i18n/scriptset.cpp", "i18n/ucln_in.cpp", "i18n/uspoof.cpp", "i18n/uspoof_impl.cpp"}
        thirdparty_sources = (function() local __item9 = {}; for _, __item10 in ipairs(R.iter(thirdparty_sources)) do; local file = __item10; table.insert(__item9, R.add(thirdparty_dir, file)); end; return __item9 end)()
        if R.truthy(env.editor_build) then
            icudata = env_icu:generate("#thirdparty/icu4c/icudata.gen.h", "#thirdparty/icu4c/icudt_godot.dat", env:generator(text_server_adv_builders.make_icu_data))
            env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/icu4c/"}})
        else
            thirdparty_sources = R.iadd(thirdparty_sources, {"icu_data/icudata_stub.cpp"})
        end
        env_icu:prepend({["CPPPATH"] = {"#thirdparty/icu4c/common/", "#thirdparty/icu4c/i18n/"}})
        env_icu:add({["CPPDEFINES"] = {"U_STATIC_IMPLEMENTATION", "U_COMMON_IMPLEMENTATION", "UCONFIG_NO_COLLATION", "UCONFIG_NO_CONVERSION", "UCONFIG_NO_FORMATTING", "UCONFIG_NO_SERVICE", "UCONFIG_NO_IDNA", "UCONFIG_NO_FILE_IO", "UCONFIG_NO_TRANSLITERATION", "UCONFIG_NO_REGULAR_EXPRESSIONS", {"PKGDATA_MODE", "static"}, {"U_ENABLE_DYLOAD", 0}, {"U_HAVE_LIB_SUFFIX", 1}, {"U_LIB_SUFFIX_C_NAME", "_godot"}}})
        env_text_server_adv:add({["CPPDEFINES"] = {"U_STATIC_IMPLEMENTATION", {"U_HAVE_LIB_SUFFIX", 1}, {"U_LIB_SUFFIX_C_NAME", "_godot"}}})
        if R.truthy(env.editor_build) then
            env_text_server_adv:add({["CPPDEFINES"] = {"ICU_STATIC_DATA"}})
        end
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/icu4c/common/", "#thirdparty/icu4c/i18n/"}})
        lib = env_icu:library("icu_builtin", thirdparty_sources)
        thirdparty_obj = R.iadd(thirdparty_obj, lib)
        inserted = false
        for _, __item11 in ipairs(R.iter(R.enumerate(R.index(env, "LIBS")))) do
            local __item12 = __item11; idx = R.index(__item12, 0); linklib = R.index(__item12, 1)
            if R.truthy(R.isinstance(linklib, {"str", "bytes"})) then
                R.insert(R.index(env, "LIBS"), idx, lib)
                inserted = true
                break
            end
        end
        if R.truthy(not R.truthy(inserted)) then
            env:add({["LIBS"] = {lib}})
        end
    end
    module_obj = {}
    if R.truthy((function() local v = R.index(env, "builtin_msdfgen"); if not R.truthy(v) then return v end; return msdfgen_enabled end)()) then
        env_text_server_adv:add({["CPPDEFINES"] = {{"MSDFGEN_PUBLIC", ""}}})
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/msdfgen"}})
    end
    if R.truthy((function() local v = R.index(env, "builtin_freetype"); if not R.truthy(v) then return v end; return freetype_enabled end)()) then
        env_text_server_adv:add({["CPPDEFINES"] = {"FT_CONFIG_OPTION_USE_BROTLI"}})
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/freetype/include"}})
    end
    if R.truthy((function() local v = R.index(env, "builtin_graphite"); if not R.truthy(v) then return v end; local v = freetype_enabled; if not R.truthy(v) then return v end; return R.index(env, "graphite") end)()) then
        env_text_server_adv:prepend({["CPPPATH"] = {"#thirdparty/graphite/include"}})
    end
    env_text_server_adv:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
