function main(graph)
    local env = graph:use("env")
    local local_env = graph:use("env_modules"):clone()
    if graph.options.qualification_feature then local_env:add({CPPDEFINES = {"QUALIFICATION_FEATURE"}}) end
    local_env:sources(env.modules_sources, "sample.cpp")
    local_env:library("external_vendor_archive", {"vendor.cpp"})
end
