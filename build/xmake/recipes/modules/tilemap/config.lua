-- Native Lua module capabilities and documentation metadata.
local R = import("recipe_compat", {rootdir = path.absolute("../../..", os.scriptdir()), anonymous = true}).new()
function can_build(env, platform)
    return not R.truthy(R.index(env, "disable_2d"))
end
function configure(env)
end
function get_doc_classes()
    return {"TileMap", "TileMapLayer", "TileData", "TileMapPattern", "TileSet", "TileSetAtlasSource", "TileSetScenesCollectionSource", "TileSetSource"}
end
function get_doc_path()
    return "doc_classes"
end
