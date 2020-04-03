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

if(NOT DEFINED ANGELSCRIPT_SDK_DIRECTORY)
    message(
        FATAL_ERROR
        "CMake variable ANGELSCRIPT_SDK_DIRECTORY must be specified to the sdk/ directory of your AS installation"
    )
endif()

if(NOT IS_DIRECTORY "${ANGELSCRIPT_SDK_DIRECTORY}/angelscript/source")
    message(
        FATAL_ERROR
        "CMake variable ANGELSCRIPT_SDK_DIRECTORY does not appear to be valid: (path)/angelscript/source is not a directory"
    )
endif()

target_compile_features(angelscript-llvm PUBLIC cxx_std_17)
target_include_directories(angelscript-llvm PRIVATE include/ ${ANGELSCRIPT_SDK_DIRECTORY}/angelscript/source)
target_link_libraries(angelscript-llvm
    angelscript pthread
    fmt
    LLVM
)

add_subdirectory(3rd)
add_subdirectory(samples/simple)
