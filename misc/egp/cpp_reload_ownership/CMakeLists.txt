cmake_minimum_required(VERSION 3.20)
project(egp_cpp_reload_ownership LANGUAGES CXX)
add_library(egp_godot_cpp STATIC IMPORTED)
set_target_properties(egp_godot_cpp PROPERTIES IMPORTED_LOCATION "${EGP_CPP_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${EGP_CPP_SDK}/include;${EGP_CPP_SDK}/gen/include"
  INTERFACE_COMPILE_DEFINITIONS "GDEXTENSION;THREADS_ENABLED;TYPED_METHOD_BIND;NOMINMAX;$<$<CONFIG:Debug>:DEBUG_ENABLED;HOT_RELOAD_ENABLED>"
  INTERFACE_COMPILE_FEATURES cxx_std_17)
foreach(version RANGE 1 3)
  add_library(ownership${version} SHARED probe.cpp)
  target_compile_definitions(ownership${version} PRIVATE FIXTURE_VERSION=${version})
  target_link_libraries(ownership${version} PRIVATE egp_godot_cpp)
  target_include_directories(ownership${version} PRIVATE "${EGP_NET_INCLUDE}")
  target_compile_options(ownership${version} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8;/Zc:__cplusplus>)
  set_target_properties(ownership${version} PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${EGP_FIXTURE_BIN}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${EGP_FIXTURE_BIN}")
endforeach()
