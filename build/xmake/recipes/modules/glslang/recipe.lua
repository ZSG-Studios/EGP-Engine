-- Native Lua source selection and generator metadata.
function main(graph)
    local R = graph.compat
    local env, env_glslang, env_modules, env_thirdparty, module_obj, thirdparty_dir, thirdparty_obj, thirdparty_sources, thirdparty_spirv_headers_dir
    env = graph:use("env")
    env_modules = graph:use("env_modules")
    env_glslang = env_modules:clone()
    thirdparty_obj = {}
    if R.truthy(R.index(env, "builtin_glslang")) then
        thirdparty_dir = "#thirdparty/glslang/"
        thirdparty_spirv_headers_dir = "#thirdparty/spirv-headers/"
        thirdparty_sources = {"glslang/GenericCodeGen/CodeGen.cpp", "glslang/GenericCodeGen/Link.cpp", "glslang/MachineIndependent/attribute.cpp", "glslang/MachineIndependent/Constant.cpp", "glslang/MachineIndependent/glslang_tab.cpp", "glslang/MachineIndependent/InfoSink.cpp", "glslang/MachineIndependent/Initialize.cpp", "glslang/MachineIndependent/Intermediate.cpp", "glslang/MachineIndependent/intermOut.cpp", "glslang/MachineIndependent/IntermTraverse.cpp", "glslang/MachineIndependent/iomapper.cpp", "glslang/MachineIndependent/limits.cpp", "glslang/MachineIndependent/linkValidate.cpp", "glslang/MachineIndependent/parseConst.cpp", "glslang/MachineIndependent/ParseContextBase.cpp", "glslang/MachineIndependent/ParseHelper.cpp", "glslang/MachineIndependent/PoolAlloc.cpp", "glslang/MachineIndependent/preprocessor/PpAtom.cpp", "glslang/MachineIndependent/preprocessor/PpContext.cpp", "glslang/MachineIndependent/preprocessor/Pp.cpp", "glslang/MachineIndependent/preprocessor/PpScanner.cpp", "glslang/MachineIndependent/preprocessor/PpTokens.cpp", "glslang/MachineIndependent/propagateNoContraction.cpp", "glslang/MachineIndependent/reflection.cpp", "glslang/MachineIndependent/RemoveTree.cpp", "glslang/MachineIndependent/Scan.cpp", "glslang/MachineIndependent/ShaderLang.cpp", "glslang/MachineIndependent/SpirvIntrinsics.cpp", "glslang/MachineIndependent/SymbolTable.cpp", "glslang/MachineIndependent/Versions.cpp", "glslang/ResourceLimits/ResourceLimits.cpp", "SPIRV/disassemble.cpp", "SPIRV/doc.cpp", "SPIRV/GlslangToSpv.cpp", "SPIRV/InReadableOrder.cpp", "SPIRV/Logger.cpp", "SPIRV/SpvBuilder.cpp", "SPIRV/SpvPostProcess.cpp", "SPIRV/SpvTools.cpp"}
        if R.truthy(((R.index(env, "platform") == "windows"))) then
            R.append(thirdparty_sources, "glslang/OSDependent/Windows/ossource.cpp")
        else
            R.append(thirdparty_sources, "glslang/OSDependent/Unix/ossource.cpp")
        end
        thirdparty_sources = (function() local __item1 = {}; for _, __item2 in ipairs(R.iter(thirdparty_sources)) do; local file = __item2; table.insert(__item1, R.add(thirdparty_dir, file)); end; return __item1 end)()
        env_glslang:prepend({["CPPPATH"] = {thirdparty_dir, "#thirdparty"}})
        env_glslang:prepend({["CPPPATH"] = {R.add(thirdparty_spirv_headers_dir, "include/spirv/unified1")}})
        env_glslang:add({["CPPDEFINES"] = {{"ENABLE_OPT", 0}}})
        env_thirdparty = env_glslang:clone()
        env_thirdparty:disable_warnings()
        env_thirdparty:sources(thirdparty_obj, thirdparty_sources)
        env.modules_sources = R.iadd(env.modules_sources, thirdparty_obj)
    end
    module_obj = {}
    env_glslang:sources(module_obj, "*.cpp")
    env.modules_sources = R.iadd(env.modules_sources, module_obj)
    env:depends(module_obj, thirdparty_obj)
end
