function can_build(env, platform) return platform == "windows" or platform == "linuxbsd" end
function configure(env) env:module_add_dependencies("qualification_external", {"box3d"}) end
function get_opts(platform) return {{name = "qualification_feature", description = "Native custom module feature", default = false, type = "boolean"}} end
function get_doc_classes() return {"QualificationExternal"} end
function get_doc_path() return "doc_classes" end
