cmake_minimum_required(VERSION 3.5)

project(angelscript-llvm LANGUAGES CXX)

add_library(angelscript-llvm
    src/angelscript-llvm/jit.cpp
)

# TODO: on GNUC, -fno-strict-aliasing should be passed
#       this is because the asBC_* macros rely on UB.

target_compile_features(angelscript-llvm PUBLIC cxx_std_17)
target_include_directories(angelscript-llvm PRIVATE include/)
target_link_libraries(angelscript-llvm angelscript pthread fmt)

add_subdirectory(3rd)
add_subdirectory(samples/simple)
