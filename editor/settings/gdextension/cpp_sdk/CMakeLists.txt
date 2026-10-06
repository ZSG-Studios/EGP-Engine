cmake_minimum_required(VERSION 3.20)
project(egp_cpp_sdk LANGUAGES CXX)
math(EXPR EGP_TARGET_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
if(NOT EGP_TARGET_BITS EQUAL @BITS@)
    message(FATAL_ERROR "This bundled SDK targets @BITS@-bit extensions. Use an EGP editor built for your target architecture.")
endif()
file(GLOB_RECURSE EGP_CPP_SOURCES CONFIGURE_DEPENDS src/*.cpp gen/src/*.cpp)
set(EGP_CPP_CACHE_ROOT "${EGP_CPP_CACHE}/${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_GENERATOR_PLATFORM}")
set(EGP_CPP_LIBRARY "${EGP_CPP_CACHE_ROOT}/${CMAKE_BUILD_TYPE}/${CMAKE_STATIC_LIBRARY_PREFIX}egp_godot_cpp${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(EGP_CPP_CACHE AND EXISTS "${EGP_CPP_LIBRARY}")
    add_library(egp_godot_cpp STATIC IMPORTED GLOBAL)
    set_target_properties(egp_godot_cpp PROPERTIES IMPORTED_LOCATION "${EGP_CPP_LIBRARY}")
    set(EGP_CPP_SCOPE INTERFACE)
else()
    add_library(egp_godot_cpp STATIC ${EGP_CPP_SOURCES})
    set(EGP_CPP_SCOPE PUBLIC)
    set_target_properties(egp_godot_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
    if(EGP_CPP_CACHE)
        # Compile/link in this project's build directory. Concurrent cold builds
        # must never link directly into the same shared archive.
        add_custom_command(TARGET egp_godot_cpp POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DSOURCE_FILE=$<TARGET_FILE:egp_godot_cpp>"
                "-DDESTINATION=${EGP_CPP_CACHE_ROOT}/$<CONFIG>/$<TARGET_FILE_NAME:egp_godot_cpp>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/publish_cache.cmake"
            VERBATIM)
    endif()
endif()
target_include_directories(egp_godot_cpp ${EGP_CPP_SCOPE} "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/gen/include")
target_compile_features(egp_godot_cpp ${EGP_CPP_SCOPE} cxx_std_17)
target_compile_definitions(egp_godot_cpp ${EGP_CPP_SCOPE} GDEXTENSION THREADS_ENABLED @PRECISION_DEFINE@
    "$<$<CONFIG:Debug>:DEBUG_ENABLED;HOT_RELOAD_ENABLED>")
if(MSVC)
    target_compile_definitions(egp_godot_cpp ${EGP_CPP_SCOPE} TYPED_METHOD_BIND NOMINMAX)
    target_compile_options(egp_godot_cpp ${EGP_CPP_SCOPE} /utf-8 /Zc:__cplusplus)
    if(NOT EGP_CPP_SCOPE STREQUAL "INTERFACE")
        target_compile_options(egp_godot_cpp PRIVATE /MP4)
    endif()
endif()
