cmake_minimum_required(VERSION 3.20)
project(egp_cached_bindings LANGUAGES CXX)
add_library(egp_godot_cpp STATIC IMPORTED)
set_target_properties(egp_godot_cpp PROPERTIES IMPORTED_LOCATION "${EGP_CPP_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${EGP_CPP_SDK}/include;${EGP_CPP_SDK}/gen/include"
  INTERFACE_COMPILE_DEFINITIONS "GDEXTENSION;THREADS_ENABLED;TYPED_METHOD_BIND;NOMINMAX;DEBUG_ENABLED;HOT_RELOAD_ENABLED"
  INTERFACE_COMPILE_FEATURES cxx_std_17)
add_library(observer SHARED observer.cpp)
foreach(version RANGE 1 5)
  add_library(victim${version} SHARED victim.cpp)
  target_compile_definitions(victim${version} PRIVATE BINDING_VERSION=${version})
endforeach()
foreach(target observer victim1 victim2 victim3 victim4 victim5)
  target_link_libraries(${target} PRIVATE egp_godot_cpp)
  target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8;/Zc:__cplusplus>)
  set_target_properties(${target} PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${EGP_FIXTURE_BIN}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${EGP_FIXTURE_BIN}")
endforeach()
