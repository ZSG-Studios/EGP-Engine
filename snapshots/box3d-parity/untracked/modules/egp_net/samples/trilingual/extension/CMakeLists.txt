cmake_minimum_required(VERSION 3.20)
project(netinterop LANGUAGES CXX)
if(EGP_CPP_LIBRARY)
  add_library(egp_godot_cpp STATIC IMPORTED)
  set_target_properties(egp_godot_cpp PROPERTIES IMPORTED_LOCATION "${EGP_CPP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${EGP_CPP_SDK}/include;${EGP_CPP_SDK}/gen/include"
    INTERFACE_COMPILE_DEFINITIONS "GDEXTENSION;THREADS_ENABLED;TYPED_METHOD_BIND;NOMINMAX;$<$<CONFIG:Debug>:DEBUG_ENABLED;HOT_RELOAD_ENABLED>"
    INTERFACE_COMPILE_FEATURES cxx_std_17)
else()
  add_subdirectory("${EGP_CPP_SDK}" sdk)
endif()
add_library(netinterop SHARED probe.cpp)
target_link_libraries(netinterop PRIVATE egp_godot_cpp)
target_include_directories(netinterop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../addons/egp_net/cpp")
target_compile_options(netinterop PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8;/Zc:__cplusplus>)
set_target_properties(netinterop PROPERTIES PREFIX ""
  OUTPUT_NAME_RELEASE "netinterop.release"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
