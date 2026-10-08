-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local d, docs, e, editor_builders, env, gen_exporters, lib, module_dirs, os, sources, target_cpp, target_h, translation_targets
    os = R.os
    editor_builders = graph:builders("editor_builders")
    env = graph:use("env")
    env.editor_sources = {}
    if R.truthy(env.editor_build) then
        env:generate("doc/doc_data_class_path.gen.h", env:value(env.doc_class_path), env:generator(editor_builders.doc_data_class_path_builder))
        gen_exporters = env:generate("export/register_exporters.gen.cpp", env:value(env.platform_exporters), env:generator(editor_builders.register_exporters_builder))
        for _, __item1 in ipairs(R.iter(env.platform_exporters)) do
            e = __item1
            env:sources(env.editor_sources, R.join({"../platform/", R.str(e), "/export/*.cpp"}, ""))
        end
        docs = {}
        docs = R.iadd(docs, graph:files("#doc/classes/*.xml"))
        module_dirs = {}
        for _, __item2 in ipairs(R.iter(R.values(env.doc_class_path))) do
            d = __item2
            if R.truthy((not R.contains(module_dirs, d))) then
                R.append(module_dirs, d)
            end
        end
        for _, __item3 in ipairs(R.iter(module_dirs)) do
            d = __item3
            if R.truthy(not R.truthy(os.path.isabs(d))) then
                docs = R.iadd(docs, graph:files(R.add(R.add("#", d), "/*.xml")))
            else
                docs = R.iadd(docs, graph:files(R.add(d, "/*.xml")))
            end
        end
        docs = R.sorted(docs)
        env:generate("#editor/doc/doc_data_compressed.gen.h", docs, env:generator(editor_builders.make_doc_header))
        translation_targets = R.dict({["#editor/translations/editor_translations.gen.cpp"] = graph:files("#editor/translations/editor/*"), ["#editor/translations/property_translations.gen.cpp"] = graph:files("#editor/translations/properties/*"), ["#editor/translations/doc_translations.gen.cpp"] = graph:files("#doc/translations/*"), ["#editor/translations/extractable_translations.gen.cpp"] = graph:files("#editor/translations/extractable/*")})
        for _, __item4 in ipairs(R.iter(R.items(translation_targets))) do
            local __item5 = __item4; target_cpp = R.index(__item5, 0); sources = R.index(__item5, 1)
            target_h = R.add(R.index(os.path.splitext(target_cpp), 0), ".h")
            env:generate({target_h, target_cpp}, sources, env:generator(editor_builders.make_translations))
        end
        env:sources(env.editor_sources, "*.cpp")
        env:sources(env.editor_sources, gen_exporters)
        env:sources(env.editor_sources, R.keys(translation_targets))
        graph:include("animation/recipe.lua")
        graph:include("asset_library/recipe.lua")
        graph:include("audio/recipe.lua")
        graph:include("debugger/recipe.lua")
        graph:include("doc/recipe.lua")
        graph:include("docks/recipe.lua")
        graph:include("export/recipe.lua")
        graph:include("file_system/recipe.lua")
        graph:include("gui/recipe.lua")
        graph:include("icons/recipe.lua")
        graph:include("inspector/recipe.lua")
        graph:include("import/recipe.lua")
        graph:include("plugins/recipe.lua")
        graph:include("project_manager/recipe.lua")
        graph:include("project_upgrade/recipe.lua")
        graph:include("run/recipe.lua")
        graph:include("settings/recipe.lua")
        graph:include("scene/recipe.lua")
        graph:include("script/recipe.lua")
        graph:include("shader/recipe.lua")
        graph:include("themes/recipe.lua")
        graph:include("translations/recipe.lua")
        graph:include("version_control/recipe.lua")
        lib = env:library("editor", env.editor_sources)
        env:prepend({["LIBS"] = {lib}})
    end
end
