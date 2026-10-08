-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    env:module_add_dependencies("gltf", {"csg", "gridmap"}, true)
    return not R.truthy(R.index(env, "disable_3d"))
end
function configure(env)
end
function get_doc_classes()
    return {"EditorSceneFormatImporterBlend", "EditorSceneFormatImporterGLTF", "GLTFAccessor", "GLTFAnimation", "GLTFBufferView", "GLTFCamera", "GLTFDocument", "GLTFDocumentExtension", "GLTFDocumentExtensionConvertImporterMesh", "GLTFLight", "GLTFMesh", "GLTFNode", "GLTFObjectModelProperty", "GLTFPhysicsBody", "GLTFPhysicsShape", "GLTFSkeleton", "GLTFSkin", "GLTFSpecGloss", "GLTFState", "GLTFTexture", "GLTFTextureSampler"}
end
function get_doc_path()
    return "doc_classes"
end
