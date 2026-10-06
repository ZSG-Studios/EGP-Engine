cmake_minimum_required(VERSION 3.20)
project(egp_toolchain_check LANGUAGES CXX)
add_executable(egp_toolchain_check check.cpp)
target_compile_features(egp_toolchain_check PRIVATE cxx_std_17)
