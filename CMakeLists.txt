cmake_minimum_required(VERSION 3.5)

project(angelscript-llvm LANGUAGES CXX)

add_library(angelscript-llvm
    src/angelscript-llvm/jit.cpp
    src/angelscript-llvm/detail/builder.cpp
    src/angelscript-llvm/detail/functionbuilder.cpp
    src/angelscript-llvm/detail/jitcompiler.cpp
    src/angelscript-llvm/detail/llvmglobals.cpp
    src/angelscript-llvm/detail/modulebuilder.cpp
    src/angelscript-llvm/detail/modulecommon.cpp
    src/angelscript-llvm/detail/modulemap.cpp
)

# TODO: on GNUC, -fno-strict-aliasing should be passed
#       this is because the asBC_* macros rely on UB.

target_compile_features(angelscript-llvm PUBLIC cxx_std_17)
target_include_directories(angelscript-llvm PRIVATE include/)
target_link_libraries(angelscript-llvm
    angelscript pthread
    fmt
    LLVM
)

add_subdirectory(3rd)
add_subdirectory(samples/simple)
